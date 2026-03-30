# Phase 7: VE.Direct — Integration Chargeurs Solaires Victron

**Projet:** KXKM Batterie Parallelator (BMU)
**Date:** 2026-03-30
**Status:** Planifie

---

## 1. Objectif

Integrer le protocole VE.Direct de Victron Energy pour lire les donnees des chargeurs solaires MPPT connectes aux batteries du BMU. Permet de visualiser la production solaire, l'etat de charge MPPT, et d'adapter la logique de protection en fonction du contexte de charge.

## 2. Protocole VE.Direct

VE.Direct est un protocole UART proprietaire Victron :
- **UART : 19200 baud, 8N1** (no flow control)
- **Format : texte** — lignes `LABEL\tVALUE\r\n`
- **Trame complete** delimitee par `Checksum\t<byte>`
- Transmission **periodique** (toutes les secondes)
- **Unidirectionnel** en mode TEXT (chargeur → BMU), bidirectionnel en mode HEX (optionnel)

### Champs MPPT typiques (SmartSolar / BlueSolar)

| Label | Description | Unite | Exemple |
|-------|------------|-------|---------|
| V | Tension batterie | mV | 27120 |
| I | Courant charge | mA | 2300 |
| VPV | Tension panneau PV | mV | 41500 |
| PPV | Puissance PV | W | 95 |
| CS | Etat charge (0-9) | enum | 3 (Bulk) |
| MPPT | Etat tracker (0-2) | enum | 2 (Active) |
| ERR | Code erreur | int | 0 |
| H19 | Yield total | 0.01 kWh | 12345 |
| H20 | Yield aujourd'hui | 0.01 kWh | 234 |
| H21 | Puissance max aujourd'hui | W | 145 |
| IL | Courant charge load | mA | 500 |
| LOAD | Etat sortie load | ON/OFF | ON |
| PID | Product ID | hex | 0xA060 |
| SER# | Numero de serie | string | HQ2012ABCDE |
| FW | Version firmware | string | 161 |

### Etats de charge (CS)

| Code | Etat | Description |
|------|------|-------------|
| 0 | Off | Chargeur eteint |
| 2 | Fault | Erreur |
| 3 | Bulk | Charge a courant max |
| 4 | Absorption | Tension constante |
| 5 | Float | Maintien |
| 7 | Equalize (manual) | Equalisation |
| 245 | Starting up | Demarrage |
| 247 | Auto equalize | Equalisation auto |
| 252 | External control | Controle externe |

## 3. Hardware

### Connexion UART via PMOD2

| Signal | GPIO BOX-3 | PMOD2 Pin | Direction |
|--------|-----------|-----------|-----------|
| RX (BMU recoit) | **GPIO44** | IO4 (UART0 RX) | Chargeur → BMU |
| TX (BMU envoie) | **GPIO43** | IO8 (UART0 TX) | BMU → Chargeur (mode HEX, optionnel) |
| GND | GND | GND | Commun |

**Note :** VE.Direct utilise des niveaux 3.3V TTL — compatible directement avec l'ESP32-S3. Le connecteur VE.Direct (JST-PH 4 broches) du chargeur Victron a : GND, TX, RX, +5V.

**Attention :** Le PMOD2 partage ces GPIOs avec le SD card (SPI). Si SD et VE.Direct sont utilises simultanement, il faudra un **second UART** (UART1 ou UART2 sur d'autres GPIOs libres du PMOD1: GPIO42 + GPIO21).

### Multi-chargeurs

Pour connecter plusieurs chargeurs Victron, options :
1. **Multiplexeur UART** (CD4052) sur PMOD — 1 UART physique, switch entre chargeurs
2. **UART supplementaires** — ESP32-S3 a 3 UARTs, UART1/UART2 sur GPIOs libres PMOD1
3. **VE.Direct vers MQTT** (Victron GX / Cerbo) — les donnees arrivent via MQTT au lieu d'UART direct

## 4. Architecture composant

```
firmware-idf/components/
└── bmu_vedirect/
    ├── CMakeLists.txt
    ├── Kconfig                     # UART pins, baud rate, parser config
    ├── include/bmu_vedirect.h      # Parser API + data struct
    └── bmu_vedirect.cpp            # UART RX task + TEXT protocol parser
```

### API

```cpp
// Donnees parsees d'un chargeur MPPT
typedef struct {
    float battery_voltage_v;      // V label (mV → V)
    float battery_current_a;      // I label (mA → A)
    float panel_voltage_v;        // VPV (mV → V)
    uint16_t panel_power_w;       // PPV
    uint8_t charge_state;         // CS (0-252)
    uint8_t mppt_state;           // MPPT (0-2)
    uint8_t error_code;           // ERR
    uint32_t yield_total_wh;      // H19 (0.01kWh → Wh)
    uint32_t yield_today_wh;      // H20 (0.01kWh → Wh)
    uint16_t max_power_today_w;   // H21
    char product_id[8];           // PID
    char serial[16];              // SER#
    bool valid;                   // Checksum OK
    int64_t last_update_ms;       // Timestamp derniere trame valide
} bmu_vedirect_data_t;

esp_err_t bmu_vedirect_init(int uart_num, int tx_gpio, int rx_gpio);
const bmu_vedirect_data_t *bmu_vedirect_get_data(void);
bool bmu_vedirect_is_connected(void);
```

### Parser

Le parser VE.Direct est une state machine simple :
1. Attendre `\r\n`
2. Lire `LABEL\tVALUE`
3. Accumuler dans un buffer de trame
4. Quand `Checksum\t<byte>` recu : valider checksum (somme de tous les octets = 0), copier dans `bmu_vedirect_data_t`

## 5. Integration avec le BMU

### Dashboard (Phase 6)

Ajouter un 5e ecran "Solaire" :

```
┌──────────────────────────────────────┐
│  Solaire / Victron MPPT              │
├──────────────────────────────────────┤
│                                      │
│  PV Tension:  41.5 V                 │
│  PV Puissance: 95 W                  │
│  Courant charge: 2.3 A              │
│                                      │
│  Etat: BULK (charge rapide)          │
│  MPPT: Actif                         │
│                                      │
│  Yield total:  123.45 kWh            │
│  Yield jour:    2.34 kWh             │
│  Pmax jour:    145 W                 │
│                                      │
│  Chargeur: SmartSolar 100/20         │
│  SN: HQ2012ABCDE                     │
└──────────────────────────────────────┘
```

### Cloud (MQTT + InfluxDB)

```
bmu_influx_write("solar", "charger=mppt1",
    "pv_v=41.5,pv_w=95,bat_i=2.3,cs=3,yield_today=234", timestamp);
```

### Protection adaptative (futur)

Avec les donnees VE.Direct, la logique de protection peut s'adapter :
- Si `CS == 3` (Bulk) : le courant de charge est eleve → adapter le seuil overcurrent
- Si `PPV == 0` (nuit) : pas de charge solaire → mode economie
- Si `ERR != 0` : alerte chargeur defaillant

## 6. Estimation

| Tache | Complexite |
|-------|-----------|
| Parser VE.Direct TEXT | Faible (state machine simple) |
| UART RX task FreeRTOS | Faible |
| Integration dashboard | Faible (1 ecran supplementaire) |
| Integration cloud (MQTT/InfluxDB) | Faible |
| Multi-chargeurs | Moyenne (multiplexage UART) |
| Protection adaptative | Haute (modification state machine) |
