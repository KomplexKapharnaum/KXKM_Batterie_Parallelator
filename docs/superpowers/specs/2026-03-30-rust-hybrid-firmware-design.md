# Firmware Rust Hybrid no_std — Design Spec

**Projet:** KXKM Batterie Parallelator (BMU)
**Date:** 2026-03-30
**Status:** Draft

---

## 1. Objectif

Implementer une version hybride du firmware BMU :
- **Couche critique (Rust `no_std`)** : drivers I2C, INA237, TCA9535, state machine protection
- **Couche connectivite (C/ESP-IDF)** : WiFi, HTTP server, MQTT, SD, NTP — via FFI

Le Rust apporte : memory safety sans runtime, pas de data races (ownership), pas de null pointer, pas de buffer overflow — ideal pour du code safety-critical batterie.

## 2. Architecture

```
firmware-rs/
├── Cargo.toml                          # Workspace root
├── rust-toolchain.toml                 # esp nightly toolchain
├── .cargo/config.toml                  # target = xtensa-esp32s3-none-elf
├── crates/
│   ├── bmu-i2c/                        # no_std I2C bus driver (esp-hal)
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── bmu-ina237/                     # no_std INA237 driver
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── bmu-tca9535/                    # no_std TCA9535 driver
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   ├── bmu-protection/                 # no_std state machine F01-F08
│   │   ├── Cargo.toml
│   │   └── src/lib.rs
│   └── bmu-ffi/                        # C FFI bridge (exports extern "C" functions)
│       ├── Cargo.toml
│       ├── src/lib.rs
│       └── cbindgen.toml               # Genere bmu_ffi.h pour le C
├── firmware-idf/                       # Le projet ESP-IDF C existant
│   ├── components/
│   │   └── bmu_rust/                   # Wrapper ESP-IDF qui link la staticlib Rust
│   │       ├── CMakeLists.txt
│   │       ├── include/bmu_ffi.h       # Genere par cbindgen
│   │       └── bmu_rust_glue.cpp       # Appels Rust depuis app_main
│   └── main/
│       └── main.cpp                    # app_main utilise bmu_rust pour protection
└── tests/
    └── test_protection.rs              # Tests Rust natifs (cargo test, pas d'ESP32)
```

## 3. Crates Rust

### 3.1 bmu-i2c (no_std)

```toml
[package]
name = "bmu-i2c"
version = "0.1.0"
edition = "2021"

[dependencies]
esp-hal = { version = "0.23", features = ["esp32s3"] }
embedded-hal = "1.0"
defmt = "0.3"
```

API :
```rust
pub struct BmuI2cBus {
    i2c: I2c<'static, Blocking>,
}

impl BmuI2cBus {
    /// Init I2C_NUM_1 on GPIO40(SCL)/GPIO41(SDA), 50kHz
    pub fn new(i2c: I2c<'static, Blocking>) -> Self;
    pub fn probe(&mut self, addr: u8) -> bool;
    pub fn scan(&mut self) -> Vec<u8, 32>; // heapless::Vec
    pub fn write_read(&mut self, addr: u8, write: &[u8], read: &mut [u8]) -> Result<(), I2cError>;
}
```

### 3.2 bmu-ina237 (no_std)

```toml
[dependencies]
embedded-hal = "1.0"
defmt = "0.3"
```

Driver generique sur `embedded_hal::i2c::I2c` trait — testable sur host sans hardware.

```rust
pub struct Ina237<I2C> {
    i2c: I2C,
    addr: u8,
    current_lsb: f32,
}

impl<I2C: I2c> Ina237<I2C> {
    pub fn new(i2c: I2C, addr: u8, shunt_ohm: f32, max_current_a: f32) -> Result<Self, Error>;
    pub fn read_voltage_mv(&mut self) -> Result<f32, Error>;
    pub fn read_current_a(&mut self) -> Result<f32, Error>;
    pub fn read_all(&mut self) -> Result<(f32, f32, f32), Error>; // (v_mv, i_a, p_w)
    pub fn read_temperature_c(&mut self) -> Result<f32, Error>;
    pub fn set_bus_alerts(&mut self, ov_mv: u16, uv_mv: u16) -> Result<(), Error>;
}
```

### 3.3 bmu-tca9535 (no_std)

```rust
pub struct Tca9535<I2C> {
    i2c: I2C,
    addr: u8,
    out_p0: u8,
    out_p1: u8,
}

impl<I2C: I2c> Tca9535<I2C> {
    pub fn new(i2c: I2C, addr: u8) -> Result<Self, Error>;
    pub fn switch_battery(&mut self, channel: u8, on: bool) -> Result<(), Error>;
    pub fn set_led(&mut self, channel: u8, red: bool, green: bool) -> Result<(), Error>;
    pub fn read_alert(&mut self, channel: u8) -> Result<bool, Error>;
    pub fn all_off(&mut self) -> Result<(), Error>;
}
```

### 3.4 bmu-protection (no_std)

```rust
pub enum BatteryState { Connected, Disconnected, Reconnecting, Error, Locked }

pub struct ProtectionConfig {
    pub min_voltage_mv: u16,    // 24000
    pub max_voltage_mv: u16,    // 30000
    pub max_current_ma: u16,    // 10000
    pub voltage_diff_mv: u16,   // 1000
    pub reconnect_delay_ms: u32,// 10000
    pub nb_switch_max: u8,      // 5
    pub overcurrent_factor: u16,// 2000 (= 2.0x)
}

pub struct Protection<const N: usize> {
    config: ProtectionConfig,
    voltages: [f32; N],
    nb_switch: [u8; N],
    reconnect_time: [u64; N],
    state: [BatteryState; N],
}

impl<const N: usize> Protection<N> {
    pub fn new(config: ProtectionConfig) -> Self;

    /// Core state machine — called once per battery per loop tick
    pub fn check_battery(&mut self, idx: usize, v_mv: f32, i_a: f32, now_ms: u64)
        -> BatteryAction;

    pub fn get_state(&self, idx: usize) -> BatteryState;
    pub fn reset_switch_count(&mut self, idx: usize);
}

pub enum BatteryAction {
    None,                    // CONNECTED, no action
    SwitchOn,               // RECONNECTING
    SwitchOff,              // DISCONNECTED
    EmergencyOff,           // ERROR (double-off)
    PermanentLock,          // LOCKED
}
```

**Avantage Rust :** La state machine est pure — pas d'I/O, pas de mutex. Elle prend des lectures en entree et retourne des actions. Le code appelant (FFI ou main) execute les actions I/O. Testable a 100% sur host sans hardware.

### 3.5 bmu-ffi (C bridge)

```rust
#[no_mangle]
pub extern "C" fn bmu_protection_init(config: *const BmuProtectionConfig) -> *mut BmuProtection;

#[no_mangle]
pub extern "C" fn bmu_protection_check(
    ctx: *mut BmuProtection,
    idx: u8,
    v_mv: f32,
    i_a: f32,
    now_ms: u64,
) -> BmuAction;

#[no_mangle]
pub extern "C" fn bmu_ina237_init(
    i2c_handle: *mut c_void, // opaque ESP-IDF i2c_master_dev_handle_t
    addr: u8,
    shunt_uohm: u32,
    max_current_ma: u16,
) -> *mut BmuIna237;
```

Le C (ESP-IDF) appelle ces fonctions. Les drivers Rust utilisent le bus I2C via un trait adapter qui wrape les fonctions C ESP-IDF.

## 4. Integration ESP-IDF ↔ Rust

```
app_main() [C/ESP-IDF]
    │
    ├── bmu_i2c_init() [C — ESP-IDF i2c_master driver]
    ├── bmu_rust_init() [FFI → Rust: init protection + drivers]
    │
    └── loop:
        ├── bmu_rust_read_battery(idx) [FFI → Rust: INA237 read via I2C adapter]
        ├── bmu_rust_check_protection(idx, v, i) [FFI → Rust: pure state machine]
        ├── bmu_rust_execute_action(action) [FFI → Rust: TCA9535 switch/LED]
        │
        ├── bmu_web_handle() [C — esp_http_server, reads state via FFI]
        ├── bmu_mqtt_publish() [C — esp_mqtt]
        └── bmu_sd_log() [C — VFS/FAT]
```

## 5. Toolchain

```toml
# rust-toolchain.toml
[toolchain]
channel = "esp"
components = ["rust-src"]
targets = ["xtensa-esp32s3-none-elf"]
```

Build : `cargo build --release --target xtensa-esp32s3-none-elf`
Produit : `target/.../libbmu_ffi.a` → linke par ESP-IDF CMake.

## 6. Tests

- **Rust unit tests** (`cargo test`) : state machine pure, drivers avec mock I2C (`embedded-hal-mock`)
- **Integration** : ESP-IDF C compile + link la staticlib Rust, flash sur BOX-3
- Les tests Rust tournent sur host (x86) sans ESP32 — feedback rapide

## 7. Avantages vs C++ pur

| Aspect | C++ ESP-IDF | Rust no_std hybrid |
|--------|-------------|-------------------|
| Memory safety | Manuel (mutex, bounds check) | Garanti par compilateur |
| Data races | FreeRTOS mutex, facile a oublier | Ownership, compile-time |
| Null dereference | Possible | Impossible (Option/Result) |
| Buffer overflow | Possible | Impossible (slice bounds) |
| Test protection logic | Requiert stubs I2C | Pure function, trivial mock |
| WiFi/HTTP/MQTT | Natif ESP-IDF | Via FFI (meme code C) |
| Build complexity | Simple (idf.py) | Cargo + ESP-IDF link |
