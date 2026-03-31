# Open Source Survey — BMS, INA237, VE.Direct, BLE, TinyML

**Date :** 2026-03-31
**Auteur :** Claude Code (recherche pour Clement Saillant / L'electron rare)
**Contexte :** Veille technologique pour le projet KXKM BMU (Battery Management Unit ESP32-S3)

---

## 1. BMS Open Source sur ESP32

### 1.1 diyBMSv4ESP32

- **URL :** https://github.com/stuartpittaway/diyBMSv4ESP32
- **Licence :** CC BY-NC-SA 2.0 UK (non-commercial)
- **Stars :** ~226 | **Forks :** 87 | **Commits :** 1055+
- **Derniere activite :** Mars 2026 (76 releases, tres actif)
- **Description :** BMS modulaire pour packs lithium-ion. Modules ATTINY par cellule serie + controleur central ESP32. Interface web, PlatformIO.
- **Pertinence KXKM :** Haute. Architecture modulaire similaire (controleur central ESP32 + modules I2C). Web UI avec WebSocket. PlatformIO. Gestion multi-cellules.
- **A retenir :**
  - Architecture controleur/module avec communication serie isolee
  - Interface web JavaScript bien structuree
  - Gestion OTA et versioning firmware
  - 76 releases = workflow release mature
- **Limite :** Licence NC interdit reutilisation commerciale directe. Oriente cellules serie, pas parallele.

### 1.2 Libre Solar BMS Firmware

- **URL :** https://github.com/LibreSolar/bms-firmware
- **Licence :** Apache-2.0
- **Stars :** ~216
- **Description :** Firmware BMS sur Zephyr RTOS. Supporte bq769x0, bq769x2, ISL94202. MOSFET back-to-back N-channel pour deconnexion.
- **Pertinence KXKM :** Moyenne. Pas ESP32 (Zephyr/nRF), mais excellente reference d'architecture BMS professionnelle.
- **A retenir :**
  - Architecture propre separation hardware/logique (Zephyr devicetree)
  - Gestion MOSFET back-to-back avec 4 en parallele pour fort courant
  - CI/CD avec GitHub Actions
  - Documentation API complete
- **Limite :** Pas ESP32, pas INA237/TCA9535.

### 1.3 SmartBMS (Green-bms)

- **URL :** https://github.com/Green-bms/SmartBMS
- **Licence :** CC BY-SA 4.0
- **Stars :** ~703 | **Forks :** 164
- **Derniere activite :** 2022 (stable, plus de dev actif)
- **Description :** BMS open source pour LiFePO4/Li-ion/NCM. Protection sur/sous-tension, balancing resistif, Bluetooth, monitoring SOC.
- **Pertinence KXKM :** Moyenne. OSHWA certifie (IT000007). Bonne reference hardware mais ATtiny/Arduino Mega, pas ESP32.
- **A retenir :**
  - Certification OSHWA — modele a suivre pour open hardware
  - App Android pour monitoring BLE
  - Balancing resistif 1A — reference pour algorithme

### 1.4 ESP32-BMS (willburk97)

- **URL :** https://github.com/willburk97/ESP32-BMS
- **Licence :** Non specifiee
- **Stars :** ~25
- **Description :** ESP32 unique pour monitoring et balancing multi-cellules Li-ion. Opto-coupleurs + MOSFET IRFZ44N. WiFi via Adafruit IO.
- **Pertinence KXKM :** Faible-Moyenne. Petit projet mais montre une approche simple ESP32 + MOSFET switching.
- **A retenir :**
  - Isolation opto-coupleur pour circuits de decharge
  - Interface RJ11 pour onduleur — idee pour connectivite terrain

### 1.5 12V Battery Monitor (tipih)

- **URL :** https://github.com/tipih/12VBatteryMonitor
- **Licence :** MIT
- **Stars :** ~5
- **Derniere activite :** Janvier 2026
- **Description :** Monitoring batterie 12V avec ESP32, INA226, DS18B20. SOC par OCV + coulomb counting. SOH par apprentissage resistance interne.
- **Pertinence KXKM :** Haute. Utilise INA226 (proche INA237), estimation SOC/SOH, MQTT + BLE, Home Assistant discovery.
- **A retenir :**
  - **SOC via OCV + coulomb counting** — algorithme directement applicable au BMU
  - **SOH via apprentissage Rint** — methode legere pour microcontroleur
  - Telemetrie adaptative (cadence selon mode)
  - Interface BLE pour configuration runtime
  - ArduinoOTA

---

## 2. Drivers INA237 / INA226 pour ESP-IDF

### 2.1 ZivLow/ina226

- **URL :** https://github.com/ZivLow/ina226
- **Licence :** Apache-2.0
- **Stars :** ~8
- **Derniere activite :** Novembre 2024
- **ESP-IDF :** v5.2+
- **Registry :** Disponible sur ESP Component Registry (`zivlow/ina226` v0.0.5)
- **Description :** Composant ESP-IDF natif pour INA226. C++ (96.7%). Adresse par defaut 0x40.
- **Pertinence KXKM :** Haute. Composant ESP-IDF natif, meme famille INA que le BMU. Migration INA237 necessitera adaptation registres (INA237 a des registres legerement differents).
- **A retenir :**
  - API C++ propre, bonne base pour fork INA237
  - Integration ESP Component Manager

### 2.2 k0i05/esp_ina226

- **URL :** https://components.espressif.com/components/k0i05/esp_ina226
- **Licence :** Non verifiee (probablement MIT ou Apache)
- **Stars :** N/A (composant registry)
- **Version :** v1.2.6
- **Description :** Composant ESP-IDF pour INA226 publie sur le registre officiel Espressif.
- **Pertinence KXKM :** Haute. Version la plus recente sur le registry, probablement le plus maintenu.

### 2.3 UncleRus/esp-idf-lib (collection)

- **URL :** https://github.com/UncleRus/esp-idf-lib
- **Licence :** Multiple (BSD-3-Clause, MIT, ISC selon composant)
- **Stars :** ~1600
- **Description :** Bibliotheque de 50+ composants ESP-IDF. Inclut INA219, INA260, INA3221 (pas INA226/237 directement), TCA6424A, TCA9548, PCF8574/8575, MCP23x17, LC709203F, MAX1704x.
- **Pertinence KXKM :** Tres haute. Le `i2cdev` thread-safe est une reference pour notre I2CMutex. Les drivers INA existants montrent le pattern a suivre.
- **A retenir :**
  - **i2cdev** : utilitaires I2C thread-safe — reference pour notre I2CLockGuard
  - INA219/INA260/INA3221 : patterns de driver INA reutilisables
  - TCA6424A : driver GPIO expander TI similaire au TCA9535
  - LC709203F / MAX1704x : fuel gauges pour reference SOC
  - Architecture mature avec tests et documentation

### 2.4 Autres drivers INA

| Projet | URL | Notes |
|--------|-----|-------|
| cybergear-robotics/ina226 | https://github.com/cybergear-robotics/ina226 | ESP-IDF, peu documente |
| FCam1/ESP32-INA226 | https://github.com/FCam1/ESP32-INA226 | Exemple complet avec i2cdev |
| TaraHoleInIt/tarableina226 | https://github.com/TaraHoleInIt/tarableina226 | ESP-IDF SDK natif |

> **Note :** Aucun driver INA237 ESP-IDF natif n'a ete trouve. Le INA237 differe du INA226 par ses registres (40-bit energy/charge accumulators, alert system different). Un fork de ZivLow/ina226 ou k0i05/esp_ina226 avec adaptation registres INA237 serait la voie la plus rapide.

---

## 3. Drivers TCA9535 pour ESP-IDF

### 3.1 UncleRus/esp-idf-lib — tca95x5

- **URL :** https://esp-idf-lib.readthedocs.io/en/latest/groups/tca95x5.html
- **Source :** https://github.com/UncleRus/esp-idf-lib (composant `tca95x5`)
- **Licence :** Variable (voir esp-idf-lib)
- **Stars :** ~1600 (repo parent)
- **Description :** Driver pour TCA9535/TCA9555 remote 16-bit I/O expanders. Thread-safe. Partie de la collection esp-idf-lib.
- **Pertinence KXKM :** Tres haute. Exactement notre composant. Migration ESP-IDF du BMU pourrait directement utiliser ce driver.
- **A retenir :**
  - Thread-safe (utilise i2cdev)
  - Supporte TCA9535 ET TCA9555 (pin-compatible)
  - Documentation complete avec exemples

### 3.2 Gordon01/esp32-tca9535

- **URL :** https://github.com/Gordon01/esp32-tca9535
- **Licence :** Non specifiee
- **Stars :** Faible
- **Description :** Exemple de communication avec TCA9535 depuis ESP32. Code minimal de reference.
- **Pertinence KXKM :** Faible. Exemple simple, pas une lib reutilisable.

### 3.3 ESP32_IO_Expander (Espressif)

- **URL :** https://github.com/esp-arduino-libs/ESP32_IO_Expander
- **Licence :** Apache-2.0 (Espressif)
- **Description :** Bibliotheque Arduino officielle Espressif pour expanders I/O (TCA95xx, CH422G, HT8574, etc.).
- **Pertinence KXKM :** Moyenne. Arduino seulement, mais code de reference Espressif officiel.

### 3.4 RobTillaart/TCA9555

- **URL :** https://github.com/RobTillaart/TCA9555
- **Licence :** MIT
- **Stars :** Non verifie
- **Description :** Bibliotheque Arduino pour TCA9555/TCA9535. API haut niveau, bien testee.
- **Pertinence KXKM :** Faible (Arduino), mais bonne reference API pour notre propre driver ESP-IDF.

---

## 4. LVGL Battery Dashboard sur ESP32-S3

### 4.1 Project Aura (21cncstudio)

- **URL :** https://github.com/21cncstudio/project_aura
- **Licence :** GPL-3.0 (licence commerciale disponible)
- **Stars :** ~531 | **Commits :** 502
- **Derniere activite :** Fevrier 2026 (tres actif)
- **Description :** Station de qualite d'air ESP32-S3 avec UI LVGL tactile, dashboard web, MQTT, Home Assistant.
- **Pertinence KXKM :** Haute. Reference UI/UX pour dashboard embarque ESP32-S3 avec LVGL.
- **A retenir :**
  - **Ecran 4.3" tactile capacitif 800x480** — reference pour notre display
  - Themes LVGL avec mode nuit
  - Dashboard web avec graphiques historiques (1h/3h/24h)
  - OTA via navigateur
  - Detection hardware automatique
  - Safe boot avec rollback auto
  - 8 langues supportees
  - **Architecture ESP-IDF + LVGL + web dashboard** tres proche de notre besoin

### 4.2 lv_port_esp32 (LVGL officiel)

- **URL :** https://github.com/lvgl/lv_port_esp32
- **Licence :** MIT
- **Stars :** Eleve (projet officiel LVGL)
- **Description :** Port officiel LVGL pour ESP32 avec drivers display et touchpad.
- **Pertinence KXKM :** Moyenne. Point de depart pour integration LVGL, mais LVGL v7 (ancien).

### 4.3 hello-lvgl-esp-idf (ryanfkeller)

- **URL :** https://github.com/ryanfkeller/hello-lvgl-esp-idf
- **Licence :** Non verifiee
- **Description :** Tutoriel tres commente pour systeme de menu LVGL sur ESP32 avec ESP-IDF. Basé sur l'exemple Espressif spi_lcd_touch.
- **Pertinence KXKM :** Moyenne. Bon tutoriel de demarrage LVGL + ESP-IDF.

### 4.4 ESPHome LVGL Energy Dashboard

- **URL :** https://esphome.io/cookbook/lvgl/
- **Licence :** N/A (documentation ESPHome)
- **Description :** Exemples de dashboards energie avec widgets LVGL (gauges, meters, arcs) pour monitoring solaire/batterie/reseau.
- **Pertinence KXKM :** Moyenne. Patterns UI (gauge, meter, arc) replicables en code C++ LVGL natif.
- **A retenir :**
  - Gauge energie avec meter + arc widgets
  - Patterns pour affichage SOC batterie
  - Layout responsive pour petits ecrans

---

## 5. Victron VE.Direct — Parsers ESP32

### 5.1 VictronVEDirectArduino (winginitau)

- **URL :** https://github.com/winginitau/VictronVEDirectArduino
- **Licence :** MIT
- **Stars :** ~98
- **Description :** Bibliotheque Arduino legere pour lecture VE.Direct. Teste avec BMV-700. Lit V, P, I, SOC, alarmes.
- **Pertinence KXKM :** Haute. Le plus populaire, MIT, code simple a porter en ESP-IDF.
- **A retenir :**
  - Parser TEXT mode complet avec checksum
  - Architecture extensible pour statistiques additionnelles
  - Fonction diagnostic "full dump"

### 5.2 VictronVEDirectESP (ecoPlanos)

- **URL :** https://github.com/ecoPlanos/VictronVEDirectESP
- **Licence :** MIT
- **Stars :** ~0 (fork peu connu)
- **Derniere activite :** Octobre 2023
- **Description :** Fork de VictronVEDirectArduino adapte pour esp-idf. C++ pur.
- **Pertinence KXKM :** Haute. Deja adapte ESP-IDF, le plus proche de notre stack.
- **Limite :** Peu maintenu, 0 stars.

### 5.3 SensESP/VEDirect

- **URL :** https://github.com/SensESP/VEDirect
- **Licence :** Apache-2.0
- **Stars :** ~10
- **Derniere activite :** Aout 2024 (v3.0.0)
- **Description :** Parser VE.Direct TEXT mode pour framework SensESP. Integration Signal K / NMEA 2000.
- **Pertinence KXKM :** Moyenne. Bien structure mais dependance SensESP framework.

### 5.4 esphome-victron-vedirect (krahabb)

- **URL :** https://github.com/krahabb/esphome-victron-vedirect
- **Licence :** MIT
- **Stars :** ~9
- **Derniere activite :** Novembre 2025 (v2025.11.1)
- **Description :** Composant ESPHome avec support TEXT + HEX protocol. Supporte BMV, MPPT, chargeurs, onduleurs, Multi RS.
- **Pertinence KXKM :** Moyenne. Le seul a supporter le protocole HEX (bidirectionnel). Code ESPHome mais logique HEX reutilisable.
- **A retenir :**
  - **Seul projet open source avec support HEX protocol complet**
  - Mappings registres pre-definis pour BMV, MPPT, chargeurs, onduleurs
  - Configuration runtime via registres HEX

### 5.5 VictronSolarDisplayEsp (wytr)

- **URL :** https://github.com/wytr/VictronSolarDisplayEsp
- **Licence :** Non verifiee
- **Description :** Display ESP32-S3 pour Victron SmartSolar avec decryptage BLE, portail captif WiFi, configuration web mobile.
- **Pertinence KXKM :** Moyenne. Reference pour affichage Victron sur ESP32-S3, approche BLE au lieu de VE.Direct filaire.

---

## 6. NimBLE GATT Battery Service

### 6.1 ESP-IDF Official — bleprph example

- **URL :** https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/bleprph
- **Licence :** Apache-2.0
- **Description :** Exemple officiel Espressif de serveur GATT NimBLE avec Battery Service integre. API `ble_svc_bas_init()`.
- **Pertinence KXKM :** Tres haute. Reference officielle, directement utilisable.
- **A retenir :**
  - `ble_svc_bas_init()` initialise le Battery Service (UUID 0x180F)
  - Battery Level characteristic UUID 0x2A19
  - Code minimal, bien documente

### 6.2 ESPP — BleGattServer + BatteryService

- **URL :** https://esp-cpp.github.io/espp/ble/battery_service.html
- **Source :** https://github.com/esp-cpp/espp
- **Licence :** MIT
- **Stars :** ~90
- **Derniere activite :** Mars 2026 (v1.0.36)
- **Description :** Abstraction C++ haut niveau pour NimBLE. Classe `BatteryService` avec Device Info integre. Composant `ble_gatt_server`.
- **Pertinence KXKM :** Tres haute. C++ moderne, MIT, actif. Inclut BatteryService + DeviceInfoService prets a l'emploi.
- **A retenir :**
  - `BatteryService` classe C++ complete
  - Integration avec task management, logger
  - Support ESP32-S3-BOX via esp-box-emu
  - Publie sur ESP Component Registry (namespace `espp`)

### 6.3 ESP32-NimbleBLE-For-Dummies

- **URL :** https://github.com/Zeni241/ESP32-NimbleBLE-For-Dummies
- **Licence :** Non verifiee
- **Description :** Tutoriel NimBLE simplifie pour debutants.
- **Pertinence KXKM :** Faible. Pedagogique mais pas production-ready.

---

## 7. Battery SOH/RUL Prediction — TinyML

### 7.1 esp-tflite-micro (Espressif officiel)

- **URL :** https://github.com/espressif/esp-tflite-micro
- **Licence :** Apache-2.0
- **Stars :** ~617
- **Description :** TensorFlow Lite Micro pour chipsets Espressif. Optimisations ESP-NN avec acceleration 42x sur ESP32-S3 a 240MHz.
- **Pertinence KXKM :** Haute. Infrastructure officielle pour deployer des modeles ML sur notre ESP32-S3.
- **A retenir :**
  - Person Detection en 54ms sur ESP32-S3 (avec ESP-NN)
  - Exemples : hello_world, micro_speech, person_detection
  - Compatible ESP-IDF 4.4+
  - **Un modele SOH/RUL entraine sur PC pourrait etre deploye via ce framework**

### 7.2 Recherche academique — TinyML Battery RUL (2025)

- **Publication :** "Embedded Sensor Data Fusion and TinyML for Real-Time RUL Estimation of UAV Li-Polymer Batteries" (MDPI Sensors, 2025)
- **URL :** https://www.mdpi.com/1424-8220/25/12/3810
- **Description :** Framework TinyML pour estimation RUL temps reel. FFNN optimise via Edge Impulse + EON Compiler. Deploye sur RP2040.
- **Resultats :** Inference 2ms, 1.2 KB RAM, 11 KB flash (modele int8 quantise).
- **Pertinence KXKM :** Tres haute. Prouve la faisabilite d'estimation RUL sur MCU contraint.
- **A retenir :**
  - **Modele int8 : 2ms inference, 1.2 KB RAM** — largement dans le budget ESP32-S3
  - Pipeline : voltage + temps decharge + capacite → FFNN → RUL
  - Edge Impulse pour entrainement et optimisation
  - Applicable aux batteries LiPo 24-30V du BMU

### 7.3 Recherche academique — SOH avec LSTM compresse

- **Publication :** "Tiny Machine Learning Battery State-of-Charge Estimation Hardware Accelerated" (MDPI Applied Sciences, 2024)
- **URL :** https://www.mdpi.com/2076-3417/14/14/6240
- **Description :** LSTM compresse par decomposition Kronecker. INA219 + TMP36 pour mesures live. Deploye sur Arduino Nano 33 BLE Sense.
- **Pertinence KXKM :** Haute. LSTM compresse = approche viable pour SOH sur MCU avec nos INA237.

### 7.4 awesome-tinyml (umitkacar)

- **URL :** https://github.com/umitkacar/awesome-tinyml
- **Licence :** N/A (liste curatee)
- **Stars :** Eleve
- **Description :** Liste curatee de ressources TinyML : inference on-device, quantification, ML embarque, IA ultra-basse consommation.
- **Pertinence KXKM :** Moyenne. Point d'entree pour exploration TinyML.

---

## 8. ESP32-S3-BOX-3 Custom Firmware

### 8.1 esp-box (Espressif officiel)

- **URL :** https://github.com/espressif/esp-box
- **Licence :** Apache-2.0
- **Stars :** ~1200
- **Description :** Plateforme AIoT officielle Espressif. ESP32-S3-BOX-3 (actif), BOX/BOX-Lite (EOL). Voice recognition, LVGL, Matter, Home Assistant.
- **Pertinence KXKM :** Haute. Si le BMU utilise un BOX-3 comme interface, ce repo est la reference.
- **A retenir :**
  - ESP-IDF v5.1+, Arduino, PlatformIO
  - LVGL + SquareLine Studio
  - Double micro pour interaction vocale far-field
  - 200+ commandes vocales personnalisables
  - Integration Matter + Home Assistant + ESP-RainMaker

### 8.2 ESP32-S3-Box3-Custom-ESPHome (BigBobbas)

- **URL :** https://github.com/BigBobbas/ESP32-S3-Box3-Custom-ESPHome
- **Licence :** Non specifiee
- **Stars :** ~304
- **Derniere activite :** Avril 2025
- **Description :** Firmware ESPHome custom pour BOX-3. Touchscreen, voice assistant, media player, capteurs radar/temp/humidite.
- **Pertinence KXKM :** Moyenne. ESPHome (pas ESP-IDF natif) mais montre les possibilites du hardware BOX-3.

### 8.3 ESP32-S3-Box-3-Firmware (nirnachmani)

- **URL :** https://github.com/nirnachmani/ESP32-S3-Box-3-Firmware
- **Licence :** Non verifiee
- **Description :** Firmware combine conversation continue + capteurs dock + media player. Fusion de projets jaymunro et gnumpi.
- **Pertinence KXKM :** Faible. Oriente assistant vocal, pas monitoring industriel.

### 8.4 ESPP — esp-box-emu

- **URL :** https://github.com/esp-cpp/espp (reference a esp-box-emu)
- **Licence :** MIT
- **Stars :** ~90
- **Description :** Systeme d'emulation multiplateforme utilisant le hardware ESP32-S3-BOX. Construit sur les composants ESPP.
- **Pertinence KXKM :** Faible-Moyenne. Montre l'utilisation avancee du BOX-3 hors cas d'usage standard.

---

## Synthese et recommandations

### Priorite haute — integration directe possible

| Projet | Usage BMU | Action |
|--------|-----------|--------|
| UncleRus/esp-idf-lib (tca95x5) | Driver TCA9535 ESP-IDF natif | Evaluer migration directe |
| ZivLow/ina226 ou k0i05/esp_ina226 | Base pour driver INA237 ESP-IDF | Fork + adaptation registres INA237 |
| ESP-IDF bleprph + ESPP BatteryService | BLE Battery Service | Integrer `ble_svc_bas_init()` ou ESPP |
| esp-tflite-micro | Inference SOH/RUL sur ESP32-S3 | POC avec modele FFNN quantise |
| VictronVEDirectArduino | Parser VE.Direct TEXT mode | Porter en ESP-IDF (MIT, code simple) |

### Priorite moyenne — reference architecturale

| Projet | Lecon pour BMU |
|--------|----------------|
| diyBMSv4ESP32 | Workflow release, web UI, architecture modulaire |
| Project Aura | Dashboard LVGL + web, OTA, safe boot, detection hardware auto |
| esphome-victron-vedirect | Protocol HEX bidirectionnel Victron |
| 12VBatteryMonitor | Algorithmes SOC (OCV + coulomb) et SOH (Rint learning) |
| Libre Solar BMS | Architecture BMS professionnelle, MOSFET back-to-back |

### Priorite basse — veille

| Projet | Interet |
|--------|---------|
| SmartBMS | Certification OSHWA, app Android BLE |
| esp-box officiel | Si migration vers hardware BOX-3 |
| awesome-tinyml | Exploration TinyML future |

### Lacunes identifiees

1. **Pas de driver INA237 ESP-IDF existant** — il faudra creer le notre (fork INA226 + adaptation)
2. **Pas de BMS open source pour batteries en parallele** — tous sont orientes serie. Notre architecture est unique.
3. **Pas de modele TinyML pre-entraine pour SOH batteries 24-30V** — entrainement custom necessaire avec nos donnees terrain

---

*Recherche effectuee le 2026-03-31 avec Claude Code. URLs verifiees a cette date.*
