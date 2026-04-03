# Dual I2C Bus — 32 Batteries

**Date:** 2026-04-02
**Hardware:** ESP32-S3-BOX-3, BMU v2 PCB
**Branche:** `victron` (puis merge main)

## Objectif

Passer de 16 à 32 batteries en ajoutant un 2e bus I2C en bit-bang software sur GPIO38/39. Chaque bus porte 16 INA237 + 4 TCA9535 aux mêmes adresses. VE.Direct déplacé sur GPIO21 RX-only.

## Architecture

```text
Bus 1 — Hardware I2C_NUM_1 (GPIO40 SDA / GPIO41 SCL, 50kHz)
  ├── 16 INA237 (0x40-0x4F)
  ├── 4 TCA9535 (0x20-0x23)
  └── AHT30 (0x38)

Bus 2 — Bit-bang (GPIO38 SDA / GPIO39 SCL, 50kHz)
  ├── 16 INA237 (0x40-0x4F)
  └── 4 TCA9535 (0x20-0x23)

VE.Direct — UART2, GPIO21 RX-only (TX=-1)
```

### Pourquoi bit-bang

- ESP32-S3 a 2 contrôleurs I2C : NUM_0 (écran BOX-3), NUM_1 (DOCK BMU)
- Pas de 3e contrôleur hardware
- Bit-bang à 50kHz sur ESP32-S3 240MHz : trivial, ~30ms pour 16 INA
- Pas de composant supplémentaire (vs TCA9548A mux)

### Pourquoi GPIO21 pour VE.Direct

- VE.Direct TEXT protocol est unidirectionnel (SmartSolar → BMU)
- Seul le RX est nécessaire, TX=-1 (pas de HEX protocol)
- Libère GPIO38/39 pour le bus I2C bit-bang

## Composant firmware : `bmu_i2c_bitbang`

Nouveau composant ESP-IDF, driver I2C software.

### API

```c
typedef struct {
    int sda_gpio;
    int scl_gpio;
    uint32_t freq_hz;
} bmu_i2c_bb_config_t;

typedef struct bmu_i2c_bb_ctx *bmu_i2c_bb_handle_t;

esp_err_t bmu_i2c_bb_init(const bmu_i2c_bb_config_t *cfg,
                           bmu_i2c_bb_handle_t *handle);

esp_err_t bmu_i2c_bb_write_read(bmu_i2c_bb_handle_t handle,
                                 uint8_t addr,
                                 const uint8_t *write_buf,
                                 size_t write_len,
                                 uint8_t *read_buf,
                                 size_t read_len);

esp_err_t bmu_i2c_bb_write(bmu_i2c_bb_handle_t handle,
                            uint8_t addr,
                            const uint8_t *buf,
                            size_t len);
```

### Implémentation

- GPIO open-drain avec pull-up interne + externe 4.7kΩ
- Start/Stop/ACK/NACK/Read/Write byte en bitwise GPIO
- Délai par `esp_rom_delay_us()` (précis à la µs)
- À 50kHz : 10µs high + 10µs low par bit
- Mutex FreeRTOS pour thread safety
- ~200 lignes de code

### Thread safety

Chaque bus a son propre mutex :
- Bus 1 : I2C driver ESP-IDF (thread-safe natif)
- Bus 2 : mutex dans `bmu_i2c_bb_ctx`

## Modifications existantes

### `bmu_i2c` — multi-bus support

Le composant `bmu_i2c` gère actuellement un seul bus.
Étendre pour supporter N bus :

```c
typedef enum {
    BMU_BUS_HARDWARE = 0,  /* I2C_NUM_1, GPIO40/41 */
    BMU_BUS_BITBANG  = 1,  /* GPIO38/39 */
    BMU_BUS_COUNT
} bmu_bus_id_t;

/* Init les deux bus */
esp_err_t bmu_i2c_init_all(i2c_master_bus_handle_t *hw_bus,
                           bmu_i2c_bb_handle_t *bb_bus);

/* Scan un bus spécifique */
int bmu_i2c_scan_bus(bmu_bus_id_t bus);
```

### `bmu_ina237` — bus-aware devices

Chaque `bmu_ina237_t` doit savoir sur quel bus il est.
Ajouter un champ `bus_id` :

```c
typedef struct {
    /* ... champs existants ... */
    bmu_bus_id_t bus_id;
} bmu_ina237_t;
```

Les fonctions `bmu_ina237_read_*` routent vers le bon bus.

### `bmu_tca9535` — idem

Ajouter `bus_id` au handle TCA9535.

### `bmu_protection` — 32 batteries

`BMU_MAX_BATTERIES` passe de 16 à 32 dans `bmu_config.h`.

Topology validation étendue :
```c
/* Par bus : nb_tca * 4 == nb_ina */
bool bus1_ok = (nb_tca_bus1 * 4 == nb_ina_bus1);
bool bus2_ok = (nb_tca_bus2 * 4 == nb_ina_bus2) || (nb_ina_bus2 == 0);
bool topology_ok = bus1_ok && bus2_ok;
```

Bus 2 peut être absent (0 devices) — backward compatible.

### `main.cpp` — init séquence

```c
/* Bus 1 : hardware */
bmu_i2c_init(&i2c_bus_hw);

/* Bus 2 : bit-bang */
bmu_i2c_bb_config_t bb_cfg = {
    .sda_gpio = 38,
    .scl_gpio = 39,
    .freq_hz = 50000
};
bmu_i2c_bb_init(&bb_cfg, &i2c_bus_bb);

/* Scan bus 1 */
bmu_ina237_scan_init(i2c_bus_hw, ..., ina_bus1, &nb_ina_bus1);
bmu_tca9535_scan_init(i2c_bus_hw, ..., tca_bus1, &nb_tca_bus1);

/* Scan bus 2 */
bmu_ina237_scan_init_bb(i2c_bus_bb, ..., ina_bus2, &nb_ina_bus2);
bmu_tca9535_scan_init_bb(i2c_bus_bb, ..., tca_bus2, &nb_tca_bus2);

/* Merge : batteries 0-15 = bus1, 16-31 = bus2 */
```

### VE.Direct — GPIO change

Kconfig defaults :
- `CONFIG_BMU_VEDIRECT_RX_GPIO=21`
- `CONFIG_BMU_VEDIRECT_TX_GPIO=-1` (désactivé)

### UI — 32 batteries

Le lazy widget creation supporte déjà jusqu'à 16.
Augmenter les arrays statiques à 32 dans :
- `bmu_ui_main.cpp` : `s_bat_rows[32]`, etc.
- `bmu_ui_soh.cpp` : `s_soh_bars[32]`, etc.
- `bmu_ui_detail.cpp` : pas de changement (1 batterie)

### BLE — 32 characteristics

Le service Battery BLE a des characteristics 0x0010-0x001F (16).
Étendre à 0x0010-0x002F (32).

### MQTT/InfluxDB — indices 0-31

Aucun changement structurel — la boucle itère déjà sur `nb_ina`.

## Backward compatibility

- Si bus 2 n'est pas câblé : 0 devices détectés, fonctionnement normal 16 batteries
- `BMU_MAX_BATTERIES=32` mais les arrays sont créés lazily
- Pas de breaking change sur l'API existante

## Kconfig

```text
menu "BMU I2C Bit-bang (Bus 2)"
    config BMU_I2C_BB_ENABLED
        bool "Enable bit-bang I2C bus 2"
        default n
    config BMU_I2C_BB_SDA_GPIO
        int "SDA GPIO"
        default 38
        depends on BMU_I2C_BB_ENABLED
    config BMU_I2C_BB_SCL_GPIO
        int "SCL GPIO"
        default 39
        depends on BMU_I2C_BB_ENABLED
    config BMU_I2C_BB_FREQ_HZ
        int "Frequency (Hz)"
        default 50000
        depends on BMU_I2C_BB_ENABLED
endmenu
```

## Hardware requis

- Pull-ups 4.7kΩ sur GPIO38 et GPIO39
- Connecteur I2C pour bus 2 (même pinout que DOCK)
- 16 INA237 + 4 TCA9535 (même schéma PCB que bus 1)
- VE.Direct RX câblé sur GPIO21

## Tests

- Test bit-bang timing : vérifier waveform au scope
- Test scan bus 2 : detect INA/TCA
- Test protection 32 batteries : étendre les tests existants
- Test backward compat : bus 2 absent = fonctionnement 16 bat

## Hors scope

- Plus de 32 batteries (TCA9548A mux si besoin futur)
- Hot-plug bus 2 (reboot requis)
- I2C recovery sur bus bit-bang (à ajouter si problèmes terrain)
