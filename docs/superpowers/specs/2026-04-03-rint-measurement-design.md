# Internal Resistance Measurement — Design Spec

## Goal

Add active internal resistance measurement to the BMU via controlled MOSFET pulse-OFF sequences on LiFePO4/Li-ion 24–30 V batteries. Expose R_ohmic (fast) and R_polarization (slow) per battery through context-appropriate output channels.

## Battery Chemistry

LiFePO4 and Li-ion, 24–30 V nominal. Ohmique response settles in ~5–20 ms. Polarization (R₁ transfer of charge) settles in ~500 ms–1 s.

---

## Architecture

New ESP-IDF component `bmu_rint` in `firmware-idf/components/bmu_rint/`. It owns the measurement sequence, guards, result caching, and output routing. It depends on `bmu_ina237` (V/I reads), `bmu_tca9535` (switch control), and `bmu_protection` (state queries).

The component does NOT own its scheduling — triggers call into it.

### Component Interface

```c
// bmu_rint.h

typedef struct {
    float r_ohmic_mohm;       // R₀ ohmique (mΩ) — measured at +100 ms
    float r_total_mohm;       // R₀ + R₁ total (mΩ) — measured at +1 s
    float v_load_mv;          // V₁ under load (mV)
    float v_ocv_fast_mv;      // V₂ at +100 ms (mV)
    float v_ocv_stable_mv;    // V₃ at +1 s (mV)
    float i_load_a;           // I₁ at measurement start (A)
    int64_t timestamp_ms;     // Measurement time (epoch ms)
    bool valid;               // Measurement succeeded
} bmu_rint_result_t;

typedef enum {
    BMU_RINT_TRIGGER_OPPORTUNISTIC,  // Natural disconnection event
    BMU_RINT_TRIGGER_PERIODIC,       // Scheduled measurement
    BMU_RINT_TRIGGER_ON_DEMAND,      // Operator request (Web/BLE)
} bmu_rint_trigger_t;

// Initialize component (call once at boot)
esp_err_t bmu_rint_init(void);

// Run active measurement on one battery — blocks ~1.2 s
// Returns ESP_OK on success, ESP_ERR_INVALID_STATE if guards fail
esp_err_t bmu_rint_measure(uint8_t battery_idx, bmu_rint_trigger_t trigger);

// Run measurement on all eligible batteries sequentially
esp_err_t bmu_rint_measure_all(bmu_rint_trigger_t trigger);

// Get cached result for a battery (last valid measurement)
bmu_rint_result_t bmu_rint_get_cached(uint8_t battery_idx);

// Opportunistic hook — call from protection state machine on natural OFF events
void bmu_rint_on_disconnect(uint8_t battery_idx, float v_before_mv, float i_before_a);
```

---

## Measurement Sequence

For one battery (active measurement):

| Step | Time | Action | Detail |
|------|------|--------|--------|
| 1 | t₀ | Read V₁, I₁ | Atomic `bmu_ina237_read_voltage_current()` |
| 2 | t₀ | Guard check | I₁ > 0.5 A (need meaningful load) |
| 3 | t₁ | MOSFET OFF | `bmu_tca9535_switch_battery(handle, ch, false)` |
| 4 | t₁ + 100 ms | Read V₂ | R_ohmic settles |
| 5 | t₁ + 1000 ms | Read V₃ | R_total settles |
| 6 | t₁ + 1000 ms | MOSFET ON | Reconnect battery |
| 7 | t₁ + 1100 ms | Verify I₃ | Confirm current resumes |

**Computation:**

```
R_ohmic  = (V₂ - V₁) / I₁    [mΩ]  — fast ohmique response
R_total  = (V₃ - V₁) / I₁    [mΩ]  — ohmique + polarization
R_polar  = R_total - R_ohmic  [mΩ]  — polarization component only
```

**Validation criteria for a valid measurement:**
- I₁ > 0.5 A (sufficient load for meaningful ΔV)
- V₂ > V₁ (voltage must rise when disconnected — sanity check)
- R_ohmic > 0 and R_ohmic < 500 mΩ (plausible range for 24–30 V packs)
- R_total >= R_ohmic (polarization cannot be negative)

Invalid measurements are discarded with a debug log. The `valid` flag stays false.

---

## Triggers

### A) Opportunistic

Hook into `bmu_protection` when the state machine disconnects a battery for natural reasons (overvoltage, overcurrent, imbalance). The protection code already reads V and I before switching OFF.

**Integration point:** In `bmu_protection_check_battery()`, when transitioning to `BMU_STATE_DISCONNECTED`:
1. Capture V₁, I₁ that triggered the disconnect
2. Call `bmu_rint_on_disconnect(idx, v1, i1)`
3. The rint component reads V₂ at +100 ms and V₃ at +1 s from the natural OFF event
4. No additional MOSFET action — the battery is already OFF

**Output:** MQTT → InfluxDB only (silent background data).

### B) Periodic

FreeRTOS timer task. Configurable interval via Kconfig + NVS override.

**Defaults:**
- Interval: 30 minutes (`CONFIG_BMU_RINT_PERIOD_MIN`, default 30)
- Enabled: true (`CONFIG_BMU_RINT_PERIODIC_ENABLED`)

**Sequence:** Iterate all connected batteries, measure one at a time with full active pulse sequence. Skip batteries with fewer than 2 total connected.

**Output:** MQTT → InfluxDB + NVS persist (last known R_int per battery).

### C) On-demand

Two entry points:

**Web API:**
- `POST /api/rint/measure` — measure all eligible batteries, return JSON results
- `POST /api/rint/measure/:id` — measure one battery by index
- `GET /api/rint/results` — return cached results for all batteries
- `GET /api/rint/results/:id` — return cached result for one battery

Auth token required (existing `WebServerHandler` auth middleware).

**BLE:**
- Add characteristic `RINT_TRIGGER` (UUID 0x0038) — write 0xFF to trigger all, write battery index (0x00–0x1F) to trigger one
- Add characteristic `RINT_RESULT` (UUID 0x0039) — notify with packed result struct

**Output:** Display LVGL + Web UI + MQTT + NVS (all channels).

---

## Safety Guards

Before any active measurement (periodic or on-demand), ALL guards must pass:

| Guard | Condition | Rationale |
|-------|-----------|-----------|
| Min connected | nb_connected >= 2 | At least 1 battery remains online |
| No error state | No battery in ERROR or LOCKED | System must be stable |
| Target connected | battery_state[idx] == CONNECTED | Cannot measure a disconnected battery |
| Min load current | I₁ > 0.5 A | Need measurable ΔV |
| No concurrent measurement | rint_mutex not held | One measurement at a time |
| Protection not in cooldown | No reconnect pending for target | Avoid interfering with reconnect logic |

If any guard fails, return `ESP_ERR_INVALID_STATE` with debug log. Do NOT retry automatically.

**Abort during measurement:** If `bmu_protection` raises an alert on any other battery while one is being measured, the rint component must:
1. Immediately re-enable the measured battery's MOSFET (ON)
2. Mark measurement as invalid
3. Return control to the protection loop

Implementation: register a callback with `bmu_protection` for alert events, checked between V₂ and V₃ reads.

---

## Output Channels

### MQTT / InfluxDB

Measurement: `rint`, tags: `battery=<idx>,trigger=<opportunistic|periodic|on_demand>`

Fields:
```
r_ohmic_mohm=<float>,r_total_mohm=<float>,r_polar_mohm=<float>,v_load_mv=<float>,v_ocv_fast_mv=<float>,v_ocv_stable_mv=<float>,i_load_a=<float>
```

### NVS Persistence

Key pattern: `rint_<idx>` — store last valid `bmu_rint_result_t` per battery. Loaded at boot to pre-fill cache. Overwritten on each valid measurement (periodic + on-demand only).

### Display LVGL (on-demand only)

Show on existing debug screen (`bmu_ui_debug`):
- Per battery: `R₀: XX.X mΩ  R₁: XX.X mΩ` 
- Measurement timestamp
- Color coding: green < 50 mΩ, orange 50–100 mΩ, red > 100 mΩ (thresholds configurable via Kconfig)

### Web API (on-demand only)

`GET /api/rint/results` response:
```json
{
  "batteries": [
    {
      "index": 0,
      "r_ohmic_mohm": 12.3,
      "r_total_mohm": 18.7,
      "r_polar_mohm": 6.4,
      "v_load_mv": 26543.0,
      "v_ocv_stable_mv": 26612.0,
      "i_load_a": 3.72,
      "timestamp": 1743678000,
      "valid": true
    }
  ]
}
```

---

## Integration with bmu_soh

The existing SOH model uses `r_int` as feature #12. Currently hardcoded to `dV/dI` delta or fallback 0.05 Ω.

Replace with: `bmu_rint_get_cached(idx).r_total_mohm / 1000.0` (convert mΩ → Ω). If no valid measurement exists, keep the existing dV/dI fallback.

---

## Kconfig Parameters

```
menu "BMU Internal Resistance"

config BMU_RINT_PERIODIC_ENABLED
    bool "Enable periodic R_int measurement"
    default y

config BMU_RINT_PERIOD_MIN
    int "Periodic measurement interval (minutes)"
    default 30
    range 5 1440

config BMU_RINT_MIN_CURRENT_MA
    int "Minimum load current for valid measurement (mA)"
    default 500
    range 100 5000

config BMU_RINT_PULSE_FAST_MS
    int "Delay before V₂ read — ohmique (ms)"
    default 100
    range 50 500

config BMU_RINT_PULSE_TOTAL_MS
    int "Total pulse OFF duration (ms)"
    default 1000
    range 500 5000

config BMU_RINT_R_MAX_MOHM
    int "Maximum plausible R_int (mΩ)"
    default 500
    range 100 2000

config BMU_RINT_DISPLAY_WARN_MOHM
    int "Display warning threshold (mΩ, orange)"
    default 50

config BMU_RINT_DISPLAY_CRIT_MOHM
    int "Display critical threshold (mΩ, red)"
    default 100

endmenu
```

---

## File Structure

```
firmware-idf/components/bmu_rint/
├── CMakeLists.txt
├── Kconfig
├── bmu_rint.cpp              # Measurement sequence, guards, caching
├── bmu_rint_output.cpp       # MQTT/NVS/display output routing
└── include/
    └── bmu_rint.h            # Public API + result struct
```

Modified files:
- `bmu_protection.cpp` — add opportunistic hook on disconnect
- `bmu_web.cpp` — add `/api/rint/*` endpoints
- `bmu_ble_battery_svc.cpp` — add RINT characteristics
- `bmu_ui_debug.cpp` — add R_int display section
- `bmu_soh.cpp` — replace r_int fallback with cached value
- `main.cpp` — init `bmu_rint`, start periodic task

---

## Appendix: EIS Roadmap (future — not implemented)

Electrochemical Impedance Spectroscopy measures complex impedance Z(f) across a frequency range (typically 1 mHz – 10 kHz). It separates:
- **R₀** (ohmic) — high-frequency intercept
- **R_ct** (charge transfer) — semicircle diameter
- **Warburg** (diffusion) — low-frequency tail
- **CPE** (double-layer capacitance) — semicircle shape

### Why EIS is not feasible today

1. **No AC excitation source.** MOSFET switches are binary (ON/OFF), cannot modulate current at controlled frequencies. Would need a programmable current sink/source per battery channel.
2. **ADC bandwidth.** INA237 at 64× averaging = ~35 ms per sample = ~28 Hz effective bandwidth. EIS needs kHz range for full Nyquist plot.
3. **Synchronous detection.** Need phase-locked measurement of V and I at each excitation frequency. INA237 has no sync trigger, reads are asynchronous over I2C.

### What would be needed

| Requirement | Hardware | Estimated cost/battery |
|-------------|----------|----------------------|
| AC current source | DAC + op-amp + MOSFET linear driver | ~5 € |
| High-speed ADC | Dedicated ADC (ADS1256 or similar, 30 kSPS+) | ~8 € |
| Sync sampling | Timer-triggered DMA on ESP32-S3 | Software only |
| DSP processing | FFT or DFT at each frequency point | Software only |

### Simplified EIS alternative

A pragmatic middle ground: **multi-frequency pulse analysis.**

Instead of sine-wave excitation, use the existing MOSFET switch as a step function (ON→OFF). The step response contains all frequencies. Apply FFT to the V(t) transient to extract impedance at multiple frequencies.

Requirements:
- Fast ADC sampling (~1 kHz minimum) during the 1 s pulse window
- INA237 without averaging (VBUS_CT = 50 µs, no AVG) could reach ~500 Hz
- Post-processing: windowed FFT → extract |Z| and phase at discrete frequencies

This could be a firmware-only upgrade (reconfigure INA237 ADC during measurement window) but with lower frequency resolution than true EIS. Worth prototyping after the active pulse measurement is validated.

### Decision gate

Revisit EIS after 3+ months of active R_int data collection. If R_ohmic + R_polar from pulse measurement correlates well with battery degradation (validated against capacity tests), EIS may not be needed. If differentiation between contact degradation and electrochemical aging is required, invest in the hardware path.
