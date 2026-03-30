# ML Battery Health Prediction -- Integration Spec

**Project**: KXKM Batterie Parallelator
**Repository**: [KomplexKapharnaum/KXKM_Batterie_Parallelator](https://github.com/KomplexKapharnaum/KXKM_Batterie_Parallelator) (branch: `object-orriented`)
**Author**: Clement Saillant / Komplex Kapharnaum
**Date**: 2026-03-27
**Status**: Draft

---

## 1. Objective

Predict **State of Health (SOH)** and **Remaining Useful Life (RUL)** for up to 16 lead-acid or LiFePO4 batteries managed by the Parallelator, using telemetry already collected by INA226 sensors and logged to SD card / InfluxDB.

**Success criteria**:
- SOH estimation within 5% accuracy (MAPE < 5%) on held-out Parallelator data
- Real-time SOH inference on target MCU (ESP32, Teensy 4.1, STM32, etc.) in under 100 ms per channel
- RUL prediction (cycle-level) via cloud model with MAPE < 3%
- Zero impact on existing firmware timing (INA read loop, SD logging, WebSocket serving)

---

## 2. Data Pipeline

### 2.1 Existing Data Sources

| Source | Format | Fields per channel | Count |
|--------|--------|--------------------|-------|
| SD card logs | CSV (`;` separator) | Temps, Volt_N, Current_N, Switch_N, AhCons_N, AhCharge_N, TotCurrent, TotCharge, TotCons | 43 files |
| InfluxDB | Line protocol | voltage, current, temperature (per batteryId) | Continuous |

**Log file naming**: `datalog_{device}_{seq}.csv` -- devices: `gocab` (12 files), `k-led1` (11), `k-led2` (11), `tender` (9).

**Sample rate**: ~1 second intervals (observed from timestamps).

### 2.2 Feature Extraction

From raw voltage/current time series, extract per-channel features:

| Feature | Description | Derivation |
|---------|-------------|------------|
| `V_mean`, `V_std` | Mean/std voltage over sliding window | Rolling stats on Volt_N |
| `I_mean`, `I_std` | Mean/std current over sliding window | Rolling stats on Current_N |
| `dV/dt` | Voltage rate of change | Finite difference on Volt_N |
| `dI/dt` | Current rate of change | Finite difference on Current_N |
| `R_internal` | Estimated internal resistance | delta_V / delta_I at load transitions |
| `Ah_discharge` | Cumulative discharge capacity | AhCons_N (already logged) |
| `Ah_charge` | Cumulative charge capacity | AhCharge_N (already logged) |
| `coulombic_eff` | Charge/discharge efficiency | AhCharge / AhCons ratio per cycle |
| `capacity_fade` | Capacity loss vs nominal | (nominal_Ah - measured_Ah) / nominal_Ah |
| `V_under_load` | Voltage sag under load | Min voltage during discharge bursts |
| `rest_voltage` | Open-circuit voltage after rest | Volt_N when Current_N ~ 0 for > 60s |
| `cycle_count` | Number of charge/discharge cycles | Zero-crossing detection on current |
| `temperature` | Battery temperature (if available via InfluxDB) | Direct from InfluxDB |

### 2.3 Processing Pipeline

```raw
SD CSV files (43)
    |
    v
[scripts/ml/parse_csv.py]  -- Parse semicolon-separated CSVs, align timestamps
    |
    v
[scripts/ml/extract_features.py]  -- Sliding window features, cycle detection
    |
    v
features.parquet  -- Tabular dataset, one row per (device, channel, window)
    |
    v
[Merge with NASA Battery Dataset]  -- Augment with public degradation data
    |
    v
train.parquet / val.parquet / test.parquet  -- 70/15/15 split by device
```

---

## 3. Model Selection

### 3.1 Candidates

| Model | Architecture | Params (est.) | MAPE (literature) | ESP32 viable | Notes |
|-------|-------------|---------------|--------------------|--------------|----|
| **FPNN** | Feature-based polynomial neural net | ~5K | 0.88% | Yes (INT8, any MCU with I2C) | Best accuracy-to-size ratio for SOH |
| **SambaMixer** | Mamba SSM blocks | ~500K-2M | SOTA on NASA | Cloud only | Best for RUL |
| **LSTM** | 2-layer LSTM | ~50K-200K | 1-3% | Teensy 4.1 / STM32H7 only | Baseline, well-understood |
| **1D-CNN** | Temporal convolutions | ~10K-50K | 1-5% | Yes (INT8, most MCUs) | Good for fixed-window patterns |
| **Linear regression** | OLS on engineered features | ~20 | 5-10% | Yes (any MCU) | Absolute baseline |

### 3.2 Recommendation

**Two-tier architecture**:

1. **Edge (MCU via I2C)**: Start with **FPNN** (Feature-based Polynomial Neural Network)
   - Tiny parameter count (~5K params = ~10 KB INT8)
   - Operates on pre-computed features from INA226 I2C sensors, not raw sequences
   - Literature MAPE of 0.88% on standard benchmarks
   - MCU-agnostic: runs on ESP32, Teensy 4.1, STM32F4/H7, or any MCU with I2C + ~50KB free RAM
   - The I2C bus (INA226 sensors + TCA9535 GPIO expanders) is the common hardware layer across all board revisions
   - Fallback: 1D-CNN if FPNN proves too sensitive to feature engineering quality

2. **Cloud (mascarade on KXKM-AI)**: **SambaMixer** for lifecycle prediction
   - Mamba SSM architecture handles long sequences efficiently (O(n) vs O(n^2))
   - Full historical data from InfluxDB, not constrained by ESP32 memory
   - Anomaly detection via reconstruction error
   - RUL prediction with confidence intervals

---

## 4. Edge vs Cloud Split

### 4.1 Edge -- MCU (I2C as common hardware layer)

The intelligence layer is **MCU-agnostic**. The INA226 current/voltage sensors and TCA9535/TCA9555 GPIO expanders communicate over I2C, which is available on all target platforms. The ML model consumes features extracted from I2C sensor readings.

**Supported MCU targets**:

| MCU | RAM | FPU | Inference lib | Notes |
|-----|-----|-----|--------------|-------|
| ESP32-WROVER | 520 KB SRAM + 4MB PSRAM | Soft-float (S3: vector) | TFLite Micro / ESP-DL | Current production board |
| Teensy 4.1 | 1 MB SRAM + 8 MB PSRAM | ARM Cortex-M7 FPU | TFLite Micro / CMSIS-NN | Fastest inference, K32-lib native |
| STM32F4/H7 | 192 KB - 1 MB | ARM Cortex-M4/M7 FPU | TFLite Micro / STM32Cube.AI | V3 boards have STM32F070 coprocessor |
| RP2040 | 264 KB | Soft-float | TFLite Micro | Budget option, dual-core |

**Common constraints across all targets**:
- Available RAM budget for ML: ~50-80 KB (after firmware overhead)
- Must not block INA I2C read loop (current: ~1s cycle for 16 channels)
- I2C bus shared with INA226 (0x40-0x4F) + TCA9535 (0x20-0x27) — ML task must yield during sensor reads

**Edge model responsibilities**:
- Real-time SOH estimation per channel (run every 60s or on-demand)
- Simple anomaly flag: voltage/current outside learned bounds
- Publish SOH + flags via WebSocket/MQTT to mascarade mesh

**Deployment format**: TFLite Micro (portable) or platform-specific (ESP-DL, STM32Cube.AI, CMSIS-NN)

**Memory budget** (same across all targets):
```raw
Model weights (INT8):     ~10 KB
Inference scratch buffer: ~15 KB
Feature buffer (16 ch):   ~8 KB
Total ML footprint:       ~33 KB
```

### 4.2 Cloud -- mascarade on KXKM-AI

**Resources**: RTX 4090 (24 GB VRAM), 62 GB RAM, Unsloth venv

**Cloud model responsibilities**:
- Full lifecycle RUL prediction (days/cycles remaining)
- Fleet-level anomaly detection across all Parallelator devices
- Capacity fade trend analysis and alerting
- Model retraining on new data (incremental)
- Serve predictions via MCP tool interface

---

## 5. Integration Architecture

### 5.1 MCU Firmware Changes (I2C-based, platform-agnostic)

New files to add to `src/`:

```raw
src/
  SOHEstimator.h        -- SOH inference wrapper (TFLite Micro or ESP-DL)
  SOHEstimator.cpp      -- Feature computation + model inference
  models/
    soh_fpnn_int8.tflite  -- Quantized model binary (included as PROGMEM)
```

**Integration points in existing code**:

```cpp
// In BatteryParallelator main loop (or dedicated FreeRTOS task):
//   Every 60 seconds:
//     1. Collect last 60 samples of (voltage, current) per channel from ring buffer
//     2. Compute features: V_mean, V_std, I_mean, I_std, dV/dt, R_internal
//     3. Run SOHEstimator::predict(channel, features) -> float soh_percent
//     4. Publish via WebSocket: {"type":"soh","ch":N,"soh":XX.X,"anomaly":false}
//     5. Log to InfluxDB: battery_soh,channel=N value=XX.X

// New ring buffer in INA_NRJ_lib (or BatteryManager):
struct SampleRing {
    float voltage[60];
    float current[60];
    uint8_t head;
};
SampleRing channelSamples[16];
```

**FreeRTOS task** (pinned to core 0, low priority):
```cpp
void sohEstimationTask(void* pvParameters) {
    SOHEstimator estimator;
    estimator.loadModel();  // Load from PROGMEM
    while (true) {
        for (int ch = 0; ch < nbBatteries; ch++) {
            float soh = estimator.predict(ch, channelSamples[ch]);
            // Publish result
        }
        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60s
    }
}
```

### 5.2 mascarade MCP Integration

New MCP tool registered in mascarade-core:

```typescript
// tools/battery_health.ts
{
  name: "battery_health",
  description: "Query battery SOH predictions and RUL for Parallelator fleet",
  inputSchema: {
    type: "object",
    properties: {
      device: { type: "string", description: "Device name (gocab, k-led1, k-led2, tender)" },
      channel: { type: "integer", minimum: 1, maximum: 16 },
      query: {
        type: "string",
        enum: ["soh", "rul", "anomalies", "fleet_summary", "capacity_trend"]
      },
      time_range: { type: "string", description: "ISO 8601 duration, e.g. P7D for last 7 days" }
    },
    required: ["query"]
  }
}
```

**Data flow**:
```raw
ESP32 Parallelator
    |-- WebSocket/MQTT --> mascarade P2P mesh
    |                          |
    |                          v
    |                   InfluxDB (Tower)
    |                          |
    |                          v
    |                   SambaMixer model (KXKM-AI)
    |                          |
    |                          v
    |                   battery_health MCP tool
    |                          |
    v                          v
SD card logs              crazy_life dashboard
```

### 5.3 crazy_life Dashboard Widget

New component in the cockpit:

```raw
BatteryFleetHealth/
  FleetOverview.tsx       -- Grid of 16 cells with SOH color coding
  ChannelDetail.tsx       -- Time series chart for selected channel
  RULForecast.tsx         -- Remaining life prediction with confidence band
  AnomalyLog.tsx          -- Recent anomaly events
```

**Color coding**: SOH > 80% green, 60-80% yellow, < 60% red.

---

## 6. Training Pipeline

### 6.1 Seed Data

- **43 CSV log files** from Parallelator SD cards
  - 4 device classes: gocab (6-8 channels), k-led1 (8 ch), k-led2 (8 ch), tender (8 ch)
  - Fields: Voltage, Current, Switch state, Ah consumption, Ah charge, totals
  - Sampling: ~1 Hz
  - Limitation: no ground-truth SOH labels (must derive from capacity fade)

### 6.2 Data Augmentation

**NASA Battery Dataset** (public, PCoE):
- 28 Li-ion cells, charge/discharge/impedance cycles until failure
- Ground-truth capacity measurements per cycle
- Download: https://www.nasa.gov/content/prognostics-center-of-excellence-data-set-repository
- Use for: pre-training the SambaMixer model, validating feature extraction pipeline

**Synthetic augmentation**:
- Add Gaussian noise to voltage/current (simulate sensor noise)
- Time-stretch/compress cycles (simulate different load profiles)
- Inject synthetic degradation curves (linear, knee-point, sudden)

### 6.3 Training on KXKM-AI

```bash
# On KXKM-AI (RTX 4090, 62 GB RAM)
ssh kxkm@kxkm-ai

# Activate environment
source ~/venv/bin/activate

# Install dependencies (one-time)
pip install pytorch-lightning tensorboard scikit-learn pandas pyarrow

# Step 1: Parse and extract features
python scripts/ml/parse_csv.py --input "log SD/" --output data/raw.parquet
python scripts/ml/extract_features.py --input data/raw.parquet --output data/features.parquet

# Step 2: Train edge model (FPNN)
python scripts/ml/train_fpnn.py \
    --data data/features.parquet \
    --epochs 200 \
    --batch-size 64 \
    --lr 1e-3 \
    --output models/soh_fpnn.pt

# Step 3: Train cloud model (SambaMixer)
python scripts/ml/train_sambamixer.py \
    --data data/features.parquet \
    --nasa-data data/nasa_battery/ \
    --epochs 100 \
    --batch-size 32 \
    --output models/rul_sambamixer.pt

# Step 4: Quantize edge model
python scripts/ml/quantize.py \
    --model models/soh_fpnn.pt \
    --format tflite \
    --quantization int8 \
    --output models/soh_fpnn_int8.tflite

# Step 5: Convert to C header for ESP32
xxd -i models/soh_fpnn_int8.tflite > src/models/soh_fpnn_int8.h
```

### 6.4 Quantization Strategy

| Step | Tool | Input | Output | Target |
|------|------|-------|--------|--------|
| Float32 training | PyTorch | features.parquet | soh_fpnn.pt | KXKM-AI |
| Post-training quantization | TFLite converter | soh_fpnn.pt | soh_fpnn_int8.tflite | ESP32 |
| C array embedding | xxd | .tflite | .h (PROGMEM) | ESP32 flash |

Alternative path: ESP-DL (Espressif's framework) with `esp-dl` quantization tools if TFLite Micro proves too heavy on ESP32 classic.

---

## 7. Milestones

### Phase 1: Data Collection + Feature Extraction (2 weeks)

- [ ] Write `scripts/ml/parse_csv.py` -- parse all 43 CSV files, handle missing channels, normalize timestamps
- [ ] Write `scripts/ml/extract_features.py` -- sliding window features, cycle detection, internal resistance estimation
- [ ] Download and preprocess NASA PCoE battery dataset
- [ ] Validate feature distributions, produce exploratory analysis notebook
- [ ] Define pseudo-SOH labels from capacity fade in Parallelator data
- **Deliverable**: `data/features.parquet` with labeled training data

### Phase 2: Train Baseline Model (2 weeks)

- [ ] Implement FPNN architecture in PyTorch (`scripts/ml/train_fpnn.py`)
- [ ] Implement SambaMixer architecture (`scripts/ml/train_sambamixer.py`)
- [ ] Train FPNN on combined Parallelator + NASA data
- [ ] Train SambaMixer for RUL prediction
- [ ] Evaluate: MAPE, RMSE, R^2 on held-out test set
- [ ] Compare against linear regression and simple LSTM baselines
- **Deliverable**: Trained models + evaluation report

### Phase 3: Quantize + Deploy on ESP32 (2 weeks)

- [ ] Quantize FPNN to INT8 via TFLite or ESP-DL
- [ ] Validate quantized model accuracy (< 1% degradation from float32)
- [ ] Implement `SOHEstimator.h/.cpp` firmware module
- [ ] Add ring buffer to INA read loop for feature computation
- [ ] Add FreeRTOS SOH estimation task
- [ ] Add WebSocket publish for SOH results
- [ ] Memory budget validation: `scripts/check_memory_budget.sh --env kxkm-v3-16MB --ram-max 75 --flash-max 85`
- **Deliverable**: Firmware with embedded SOH inference, tested on hardware

### Phase 4: mascarade MCP + Dashboard (2 weeks)

- [ ] Deploy SambaMixer model as inference service on KXKM-AI
- [ ] Implement `battery_health` MCP tool in mascarade-core
- [ ] Wire ESP32 SOH data into InfluxDB via existing `InfluxDBHandler`
- [ ] Build `BatteryFleetHealth` widget in crazy_life dashboard
- [ ] End-to-end integration test: ESP32 -> mascarade -> dashboard
- [ ] Documentation and operational runbook
- **Deliverable**: Full pipeline live, fleet health visible in cockpit

### Timeline Summary

```raw
Week 1-2:   Phase 1 -- Data pipeline
Week 3-4:   Phase 2 -- Model training
Week 5-6:   Phase 3 -- Edge deployment
Week 7-8:   Phase 4 -- Cloud integration + dashboard
```

---

## 8. References

### Battery ML Research

1. **FPNN** -- Feature-based Polynomial Neural Network for battery SOH estimation. MAPE 0.88% on benchmark datasets. Lightweight architecture suitable for edge deployment.

2. **SambaMixer** -- Mamba State Space Model for battery RUL prediction. Current SOTA on NASA PCoE dataset. Selective state space layers with O(n) sequence modeling.

3. **GPT4Battery** -- LLM-based approach for State of Health estimation. Demonstrates transfer learning from language models to battery time series. Interesting for few-shot adaptation.

4. **BatteryML** -- Open-source platform for battery lifecycle prediction. Standardized benchmarks, feature extraction utilities, and model comparison framework. GitHub: https://github.com/microsoft/BatteryML

5. **NASA PCoE Battery Dataset** -- 28 Li-ion cells cycled to failure with impedance, charge, and discharge measurements. Standard benchmark for RUL prediction. Public domain.

6. **Attention-based CNN-LSTM** -- Hybrid architecture combining spatial feature extraction (CNN) with temporal modeling (LSTM) and attention mechanism for SOH prediction.

7. **Transfer Learning for Battery Degradation** -- Domain adaptation techniques for applying models trained on lab data (NASA) to field data (Parallelator). Critical for bridging the lab-to-field gap.

8. **TinyML for Predictive Maintenance** -- Survey of quantization and pruning techniques for deploying neural networks on microcontrollers. INT8 quantization typically preserves > 99% accuracy.

9. **Electrochemical Impedance Spectroscopy (EIS) features** -- Extracting internal resistance and diffusion coefficients from voltage/current transients without dedicated EIS hardware.

10. **Incremental Capacity Analysis (ICA)** -- dQ/dV analysis for detecting degradation modes (SEI growth, lithium plating, active material loss) from standard charge/discharge curves.

### Framework and Tools

- **TensorFlow Lite Micro**: https://www.tensorflow.org/lite/microcontrollers
- **ESP-DL** (Espressif Deep Learning): https://github.com/espressif/esp-dl
- **Unsloth**: Training acceleration on KXKM-AI RTX 4090
- **InfluxDB Client for Arduino**: Already integrated in firmware
- **mascarade MCP**: Tool registration and P2P mesh communication

---

## Appendix A: CSV Schema

From `datalog_{device}_{seq}.csv` (semicolon-separated):

```raw
Temps;Volt_1;...;Volt_N;Current_1;...;Current_N;Switch_1;...;Switch_N;
AhCons_1;...;AhCons_N;AhCharge_1;...;AhCharge_N;TotCurrent;TotCharge;TotCons
```

- `Temps`: ISO 8601 timestamp (e.g., `2025-05-23 16:27:34`)
- `Volt_N`: Bus voltage in volts (float, ~28-30V for 24V lead-acid systems)
- `Current_N`: Current in amps (float, positive = discharge, negative = charge)
- `Switch_N`: Relay state (`ON` / `OFF`)
- `AhCons_N`: Cumulative amp-hour discharge (float)
- `AhCharge_N`: Cumulative amp-hour charge (float)
- `TotCurrent`: Total instantaneous current across all channels
- `TotCharge`: Total charge across all channels
- `TotCons`: Total consumption across all channels

Channels vary by device: gocab uses 6-8 channels, k-led and tender use up to 8.

## Appendix B: ESP32 Memory Map (with ML)

```raw
Flash (16 MB):
  Firmware:          ~1.5 MB
  SPIFFS:            ~1.0 MB
  Model (INT8):      ~0.01 MB
  OTA partition:     ~1.5 MB
  Free:              ~12 MB

SRAM (520 KB):
  FreeRTOS + WiFi:   ~180 KB
  INA + TCA drivers: ~20 KB
  WebSocket server:  ~40 KB
  SD Logger:         ~16 KB
  InfluxDB client:   ~30 KB
  Ring buffers (16ch): ~8 KB
  ML inference:      ~25 KB
  Stack + heap:      ~200 KB
  --------------------------------
  Total used:        ~519 KB (estimate, tight)
```

Note: Memory budget is tight on ESP32-WROVER. If ML inference exceeds 30 KB, consider offloading entirely to mascarade cloud. The `check_memory_budget.sh` script must pass with `--ram-max 75`.

---

## 11. ML Governance 2026 Addendum

### 11.1 Reproducibility and Versioning
- Every training run must record: dataset hash, feature extraction script commit, training script commit, hyperparameters, random seed, and produced model artifact name.
- Use explicit model naming: `soh_<arch>_v<major>.<minor>_<YYYYMMDD>`.
- Store a run metadata artifact (`training_metadata.json`) alongside each exported model.
- Keep a feature schema version (`feature_schema_version`) and increment it on any feature definition change.

### 11.2 In-Domain vs Out-of-Domain Evaluation
- Report metrics separately for in-domain Parallelator field logs and out-of-domain public PHM datasets.
- Do not merge in-domain and out-of-domain scores into a single headline metric.
- Minimum reporting set: MAPE, MAE, calibration error, and confidence coverage by domain.
- Track generalization gap explicitly: `OOD_MAPE - ID_MAPE`.

### 11.3 Confidence, Uncertainty, and OOD Signals
- Edge and cloud predictions must include confidence metadata (interval or calibrated score).
- Add an `is_in_distribution` or equivalent OOD flag for each inference result.
- If confidence is below threshold or OOD flag is active, classify prediction as advisory-low-confidence.
- Prediction payloads must include both value and confidence context in dashboards/API outputs.

### 11.4 Drift Monitoring and Retraining Triggers
- Monitor feature drift in production (rolling statistics vs training baseline).
- Define explicit thresholds for drift alerts and retraining triggers.
- Log drift indicators to time-series storage and review periodically.
- Retraining decisions must be based on measured degradation and drift evidence, not ad-hoc intuition.

### 11.5 Safety Policy: Advisory-Only ML
- ML outputs must never override hard protection logic (voltage/current thresholds, topology checks, reconnect protections).
- On inference failure (timeout, NAN, model load error), system behavior falls back to deterministic protection logic only.
- Safety-critical switching decisions remain firmware state-machine responsibilities.
- Any future control-loop coupling with ML requires a dedicated safety review and test campaign.

### 11.6 Governance Checklist Before Deployment
- Reproducibility artifacts generated and archived.
- In-domain and out-of-domain metrics reported separately.
- Confidence/OOD outputs validated on representative scenarios.
- Drift detection verified with synthetic and real replay traces.
- Advisory-only safety fallback tested end-to-end.
- Memory and latency budgets validated on target hardware.

### 11.7 CI/Validation Recommendations
- Add automated checks for metadata completeness in model artifacts.
- Fail CI if domain-split metrics or confidence fields are missing in evaluation reports.
- Keep a regression benchmark set derived from field logs to detect silent quality drift.
- Version and review evaluation notebooks/scripts as part of model PRs.