# Operational R_int + SOH LLM — Design Spec

## Goal

Build a 4-phase system that exposes battery health data in the mobile app, trains local ML models (TSMixer + GNN) for SOH scoring and fleet anomaly detection, fine-tunes Qwen2.5-7B for French diagnostic narratives, and closes the loop with interactive on-demand diagnostics from the app.

## Battery Chemistry

LiFePO4 and Li-ion, 24–30 V nominal, 2–23 batteries in parallel per BMU.

---

## Phase 1: Data Exposure (Firmware + App)

### Firmware: BLE SOH Characteristic

Add characteristic UUID 0x003A to the existing battery GATT service. Packed struct, READ + NOTIFY:

```c
typedef struct __attribute__((packed)) {
    uint8_t  soh_pct;           // SOH 0–100 (from bmu_soh_get_cached * 100)
    uint16_t r_ohmic_mohm_x10;  // R_ohmic * 10 (0.1 mΩ resolution)
    uint16_t r_total_mohm_x10;  // R_total * 10
    uint8_t  rint_valid;        // R_int measurement valid flag
    uint8_t  soh_confidence;    // Model confidence 0–100 (sample count proxy)
} ble_soh_char_t;               // 7 bytes per battery
```

Notified at 1 Hz alongside existing battery characteristics.

### App: KMP Data Model Extension

Extend `BatteryState.kt`:

```kotlin
data class BatteryHealth(
    val sohPercent: Int,           // 0–100
    val rOhmicMohm: Float,        // mΩ
    val rTotalMohm: Float,        // mΩ
    val rintValid: Boolean,
    val sohConfidence: Int,        // 0–100
    val timestamp: Long
)
```

Add to `GattParser.kt`: parse 0x003A (7 bytes per battery) and 0x0039 (12 bytes R_int result per battery).

### App: Dashboard UI

- Per-battery card: SOH gauge (circular, color-coded green/orange/red), R_int values
- Trigger R_int measurement button (BLE write to 0x0038)
- Pull-to-refresh triggers BLE read of 0x0039 + 0x003A

### App: Trigger Flow

1. User taps "Measure R_int" on battery card
2. App writes battery index to BLE 0x0038
3. Firmware runs `bmu_rint_measure()` (~1.2 s)
4. App polls 0x0039 for result, updates UI

---

## Phase 2: ML Scoring Pipeline (kxkm-ai)

### Infrastructure

- Server: kxkm-ai (RTX 4090 24 GB, Tailscale)
- Data source: InfluxDB on kxkm-ai:8086, measurement `rint` + `battery`
- Output: InfluxDB measurement `soh_ml`
- API: REST on kxkm-ai:8400

### ETL: InfluxDB to Feature Windows

Python job (cron every 30 min):

1. Query InfluxDB: last 7 days of `rint` + `battery` measurements per battery
2. Compute windowed features per battery:
   - R_int statistics: mean, std, slope (linear regression), max, min
   - Voltage statistics: mean, std, min, max, mean_under_load
   - Current statistics: mean, std, peak, duty_cycle (% time > 0.5 A)
   - Energy: total Ah discharged, total Ah charged, coulombic efficiency
   - Temperature: mean die temp (INA237), max die temp
   - Cycle proxy: number of disconnect/reconnect events
   - R_int trend: slope of R_ohmic over 7 days (mΩ/day)
3. Output: feature matrix N_batteries x F_features

### Model A: TSMixer (Per-Battery Scoring)

**Architecture:** TSMixer-B (basic), 3 mixing layers, hidden dim 64.

**Input:** 7-day feature window per battery (F features x T time steps, T = number of 30-min samples over 7 days = ~336).

**Output (3 heads):**
- `soh_score`: 0.0–1.0 (health percentage)
- `rul_days`: estimated remaining useful life in days (regression)
- `anomaly_score`: 0.0–1.0 (0 = normal, 1 = critical anomaly)

**Training data:**
- Real: ~3 weeks InfluxDB data, augmented with temporal shifts and noise injection
- Synthetic: simulated degradation profiles for LiFePO4/Li-ion:
  - Calendar aging: R_int linear drift +0.5–2 mΩ/month
  - Cycle aging: R_int exponential rise after N cycles (knee point)
  - Sudden failure: step-change in R_int (connection degradation, cell failure)
  - Temperature stress: accelerated R_int rise at high temp
- Target labels: SOH from capacity-fade proxy (existing train_fpnn.py approach) + synthetic RUL

**Training:** PyTorch, ~500K params, train on kxkm-ai. Quantize to ONNX for inference.

**Inference:** Every 30 min, write results to InfluxDB `soh_ml`:
```
soh_ml,battery=0 soh_score=0.92,rul_days=180,anomaly_score=0.05 <timestamp>
```

### Model B: GNN (Fleet-Level Scoring)

**Architecture:** Graph Attention Network (GAT), 2 layers, hidden dim 32.

**Graph structure:**
- Nodes: N batteries (2–23), each with TSMixer feature embedding (last hidden state)
- Edges: fully connected (all batteries share the same bus). Edge features: voltage imbalance between pairs, current ratio.

**Output (per graph):**
- `fleet_health`: 0.0–1.0 (aggregate fleet score)
- `outlier_idx`: battery index most anomalous relative to peers
- `outlier_score`: how anomalous the outlier is (0.0–1.0)
- `imbalance_severity`: 0.0–1.0 (voltage/current distribution health)

**Training data:** Same augmented dataset as TSMixer, but structured as graphs. Synthetic scenarios:
- One degraded battery among healthy ones (outlier detection)
- Gradual fleet-wide degradation (calendar aging)
- Imbalanced fleet (mixed chemistry/age/capacity)

**Training:** PyTorch Geometric, ~200K params. Quantize to ONNX.

**Inference:** Every 30 min (after TSMixer), write to InfluxDB:
```
soh_fleet fleet_health=0.88,outlier_idx=3,outlier_score=0.72,imbalance=0.15 <timestamp>
```

### REST API

Endpoint on kxkm-ai:8400:

```
GET /api/soh/batteries          → all battery scores (latest)
GET /api/soh/battery/{id}       → single battery score + history
GET /api/soh/fleet              → fleet score + outlier info
POST /api/soh/predict           → on-demand inference (force refresh)
GET /api/soh/history/{id}?days=30 → time series of SOH/RUL/anomaly
```

Response format:
```json
{
  "battery": 3,
  "soh_score": 0.87,
  "rul_days": 142,
  "anomaly_score": 0.12,
  "r_int_trend_mohm_per_day": 0.08,
  "timestamp": 1743678000
}
```

---

## Phase 3: LLM Diagnostic Narratives (kxkm-ai)

### Base Model

Qwen2.5-7B, fine-tuned with Unsloth (QLoRA, 4-bit). Inference in 4-bit: ~14 GB VRAM. Runs alongside TSMixer+GNN (< 2 GB combined) within 4090 budget.

### Dataset Generation

~2000 training pairs generated in 3 stages:

**Stage 1: Synthetic (1500 examples)**
Use Claude API to generate (battery context JSON → French diagnostic text) pairs. Battery context includes: R_int history, SOH score, anomaly score, fleet health, voltage/current stats, cycle count.

Prompt template for generation:
```
Tu es un expert en batteries LiFePO4/Li-ion pour systèmes de spectacle vivant.
Voici les données d'une batterie: {context_json}
Rédige un diagnostic concis en français (3-5 phrases): état actuel,
tendance, recommandation d'action si nécessaire. Sois précis et technique.
```

Scenarios to cover:
- Healthy battery, normal operation
- Early degradation (R_int rising slowly)
- Accelerated degradation (R_int knee point)
- Connection issue (sudden R_int jump, low SOH confidence)
- Anomaly detected by GNN (outlier in fleet)
- End-of-life (SOH < 60%, RUL < 30 days)
- Post-replacement (new battery, low cycle count)
- Fleet imbalance (one weak battery dragging others)

**Stage 2: Validation (manual, 200 examples)**
Review synthetic examples, correct errors, add domain-specific nuances from KXKM operational experience.

**Stage 3: Terrain refinement (300 examples)**
After Phase 2 runs for 1+ month, generate pairs from real data + real operator feedback. Fine-tune incrementally.

### Fine-tuning

- Method: QLoRA (rank 16, alpha 32) via Unsloth on kxkm-ai
- Epochs: 3–5
- Eval: ROUGE-L + human review on 50 held-out examples
- Output: LoRA adapter (~50 MB), base model stays unchanged

### Inference

Input prompt structure:
```
<|system|>Tu es l'assistant diagnostic batterie du BMU KXKM.
<|user|>Batterie {id}, flotte de {nb} batteries.
SOH: {soh}%, RUL estimé: {rul} jours, anomalie: {anomaly_score}.
R_int ohmique: {r_ohm} mΩ (tendance: {trend} mΩ/jour sur 7j).
R_int total: {r_tot} mΩ. V moyen: {v_avg} mV, I moyen: {i_avg} A.
Cycles: {cycles}. Santé flotte: {fleet_health}%.
<|assistant|>
```

Output: 3–5 phrases en français, diagnostic + tendance + recommandation.

### API

```
GET  /api/diagnostic/{battery_id}    → cached daily diagnostic
POST /api/diagnostic/{battery_id}    → generate fresh diagnostic (on-demand)
GET  /api/diagnostic/fleet           → fleet-level summary
```

Response:
```json
{
  "battery": 3,
  "diagnostic": "Batterie 3 en bon état (SOH 87%). La résistance interne ohmique est stable à 15.2 mΩ, tendance +0.08 mΩ/jour — normal pour l'usage actuel. RUL estimé à 142 jours. Aucune action requise.",
  "severity": "info",
  "generated_at": 1743678000
}
```

Severity levels: `info`, `warning`, `critical`.

### Scheduling

- **Daily digest:** Cron job generates diagnostics for all batteries at 06:00, stores in cache
- **On-demand:** App requests fresh diagnostic via REST, generated in ~2–5 s (Qwen 7B 4-bit on 4090)

---

## Phase 4: App Integration Loop

### Dashboard Enhancements

- **SOH tab:** Per-battery SOH gauge with trend sparkline (7 days)
- **Fleet view:** Aggregate health score, outlier highlight
- **Diagnostic card:** French text from Phase 3 LLM, severity-colored
- **R_int trend chart:** 7-day R_int history per battery
- **Notification:** Push alert when anomaly_score > 0.7 or SOH < 70%

### Data Flow

```
BLE (local):
  0x003A → SOH/R_int (real-time, 1 Hz)
  0x0038 → Trigger R_int (on-demand)
  0x0039 → R_int result

REST (kxkm-ai):
  /api/soh/battery/{id}      → ML scores (TSMixer + GNN)
  /api/diagnostic/{id}       → LLM narrative
  /api/soh/history/{id}      → Trend data for charts
  /api/soh/fleet             → Fleet health
```

### App Architecture Extension

```
BleTransport ──→ BatteryHealth (local, real-time)
RestClient   ──→ MlScores (cloud, 30-min refresh)
RestClient   ──→ Diagnostic (cloud, daily + on-demand)
                      ↓
              ViewModel aggregates all sources
                      ↓
              Dashboard UI (Compose/SwiftUI)
```

### Offline Behavior

- BLE data always available (SOH from edge model, R_int from firmware)
- Cloud scores cached locally (SQLDelight), shown with "last updated" timestamp
- Diagnostic text cached, refreshed when connectivity returns

---

## Phasing Summary

| Phase | Scope | Dependencies | Effort |
|-------|-------|-------------|--------|
| 1 | BLE SOH char + app R_int/SOH display + trigger | None | ~3 days |
| 2 | ETL + TSMixer + GNN + REST API | InfluxDB data | ~2 weeks |
| 3 | Dataset generation + Qwen fine-tune + API | Phase 2 scores | ~1 week |
| 4 | App dashboard + REST integration + notifications | Phases 2+3 | ~1 week |

Phases 1 and 2 can start in parallel. Phase 3 depends on Phase 2 outputs. Phase 4 depends on both 2 and 3.

---

## Kconfig / Config Additions

### Firmware (Phase 1 only)
```
config BMU_BLE_SOH_ENABLED
    bool "Enable BLE SOH characteristic"
    default y
    depends on BMU_RINT_ENABLED
```

### kxkm-ai services (Phases 2–3)
Docker Compose services on kxkm-ai:
- `soh-scoring`: Python container, TSMixer+GNN inference, cron 30 min
- `soh-api`: FastAPI on port 8400, serves REST endpoints
- `soh-llm`: Qwen2.5-7B inference (vLLM or llama.cpp server), port 8401
- `soh-etl`: Feature extraction job, cron 30 min

All services share the same InfluxDB instance (kxkm-ai:8086).
