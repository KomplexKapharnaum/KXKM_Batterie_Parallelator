# Validation Terrain — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Flash the ESP32-S3-BOX-3, validate BLE SOH + R_int measurement on real batteries, build the KMP app, and test REST communication with kxkm-ai services.

**Architecture:** Firmware validation via serial monitor + BLE client. App build via Gradle/Xcode. Services validation via Docker Compose on kxkm-ai + curl smoke tests.

**Tech Stack:** ESP-IDF 5.4, idf.py, nRF Connect (BLE), Gradle 8.2, Xcode 16, Docker Compose, curl

---

## File Structure

No new files created. This plan validates existing code on real hardware and infrastructure.

| Asset | Location | Action |
|-------|----------|--------|
| Firmware | `firmware-idf/` | Build, flash, monitor |
| App Android | `kxkm-bmu-app/androidApp/` | Gradle build + install |
| App iOS | `kxkm-bmu-app/iosApp/` | Xcode build |
| SOH Scoring | `services/soh-scoring/` | Docker deploy on kxkm-ai |
| SOH LLM | `services/soh-llm/` | Docker deploy on kxkm-ai |

---

### Task 1: Flash firmware sur BOX-3

**Files:**
- Read: `firmware-idf/sdkconfig.defaults`
- Read: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Vérifier la connexion USB**

Brancher le BOX-3 via USB-C. Vérifier le port série :

```bash
ls /dev/cu.usbmodem*
```

Expected: `/dev/cu.usbmodem3101` (ou similaire). Si rien, vérifier le câble USB-C (data, pas charge-only).

- [ ] **Step 2: Activer ESP-IDF et build**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/firmware-idf
source ~/esp/esp-idf/export.sh
idf.py build 2>&1 | tail -5
```

Expected: `Project build complete` avec `kxkm-bmu.bin`, 19-20% free.

- [ ] **Step 3: Flash + monitor**

```bash
idf.py --port /dev/cu.usbmodem* flash monitor
```

Expected dans le serial output :
```
I (xxx) MAIN:   KXKM BMU — ESP-IDF v5.4 (BOX-3)
I (xxx) MAIN: BLE active — 'kryole'
I (xxx) bmu_rint: bmu_rint init OK
I (xxx) bmu_rint: Periodic task started (interval=30 min)
```

- [ ] **Step 4: Vérifier WiFi + MQTT**

Grep dans le monitor :
```
I (xxx) wifi: connected to 'Les cils'
I (xxx) bmu_mqtt: MQTT connected to mqtt://192.168.0.160:1883
```

Si WiFi échoue, vérifier SSID/password dans `sdkconfig.defaults` :
```
CONFIG_BMU_WIFI_SSID="Les cils"
CONFIG_BMU_WIFI_PASSWORD="mascarade"
```

- [ ] **Step 5: Vérifier I2C scan + topology**

```
I (xxx) MAIN: I2C scan: N devices
I (xxx) MAIN: INA237: N
I (xxx) MAIN: TCA9535: N
```

Si `N=0` : pas de batteries connectées (normal en labo sans le PCB BMU). Le firmware continue sans sensors.

---

### Task 2: Valider BLE SOH + R_int via nRF Connect

**Prérequis:** nRF Connect installé sur smartphone (iOS/Android).

- [ ] **Step 1: Scanner et connecter**

Ouvrir nRF Connect → Scanner → Chercher device "kryole" → Connect.

Expected: connexion BLE réussie, services GATT listés.

- [ ] **Step 2: Découvrir le service batterie**

Chercher le service avec UUID base `4B584B4D-xxxx-4B4D-424D-55424C450000`.

Characteristics attendues :
| UUID suffix | Type | Description |
|---|---|---|
| 0x0010–0x002F | READ+NOTIFY | Battery 0–31 (17 bytes chacune) |
| 0x0038 | WRITE | RINT Trigger |
| 0x0039 | READ+NOTIFY | RINT Result (12 bytes/batterie) |
| 0x003A | READ+NOTIFY | SOH Result (7 bytes/batterie) |

- [ ] **Step 3: Souscrire aux notifications batterie**

Activer NOTIFY sur characteristic 0x0010 (battery 0).

Expected: notifications 1 Hz avec 17 bytes. Décoder (little-endian) :
- Bytes 0-3: voltage_mv (int32)
- Bytes 4-7: current_ma (int32)
- Byte 8: state (0=CONNECTED)
- Bytes 9-12: ah_discharge_mah
- Bytes 13-16: ah_charge_mah
- Byte 17: nb_switch

Si nb_ina=0 (pas de batteries), les valeurs seront 0.

- [ ] **Step 4: Lire SOH (0x003A)**

READ characteristic 0x003A.

Expected: 7 bytes par batterie. Décoder :
- Byte 0: soh_pct (0-100)
- Bytes 1-2: r_ohmic_mohm_x10 (uint16 LE)
- Bytes 3-4: r_total_mohm_x10 (uint16 LE)
- Byte 5: rint_valid (0 ou 1)
- Byte 6: soh_confidence (0-100)

Si pas encore de mesure R_int : rint_valid=0, r_ohmic=0, r_total=0.

- [ ] **Step 5: Trigger mesure R_int (0x0038)**

WRITE `0xFF` sur characteristic 0x0038 (mesurer toutes les batteries).

Vérifier dans le serial monitor :
```
I (xxx) bmu_rint: BAT[1] pulse OFF — V1=XXXXXmV I1=X.XXA
I (xxx) bmu_rint: BAT[1] R_ohm=XX.XmΩ R_tot=XX.XmΩ
```

Si guard échoue (pas assez de batteries) :
```
W (xxx) bmu_rint: BAT[1] guard check failed
```

- [ ] **Step 6: Lire résultat R_int (0x0039)**

READ characteristic 0x0039 après le trigger.

Expected: 12 bytes par batterie. Décoder :
- Bytes 0-1: r_ohmic_mohm_x10 (uint16 LE)
- Bytes 2-3: r_total_mohm_x10 (uint16 LE)
- Bytes 4-5: v_load_mv (uint16 LE)
- Bytes 6-7: v_ocv_mv (uint16 LE)
- Bytes 8-9: i_load_ma (int16 LE)
- Byte 10: valid (0 ou 1)

---

### Task 3: Déployer services ML sur kxkm-ai

- [ ] **Step 1: SSH sur kxkm-ai**

```bash
ssh kxkm@kxkm-ai
```

- [ ] **Step 2: Cloner/pull le repo**

```bash
cd ~/KXKM_Batterie_Parallelator
git pull origin main
```

- [ ] **Step 3: Configurer les variables d'environnement**

```bash
cat > ~/KXKM_Batterie_Parallelator/.env << 'EOF'
INFLUXDB_TOKEN=<votre_token_influxdb>
EOF
```

- [ ] **Step 4: Créer le réseau Docker (si absent)**

```bash
docker network create kxkm 2>/dev/null || true
docker network create bmu-net 2>/dev/null || true
```

- [ ] **Step 5: Déployer SOH Scoring (port 8400)**

```bash
cd ~/KXKM_Batterie_Parallelator
docker compose -f services/soh-scoring/docker-compose.yml up -d --build
```

Vérifier :
```bash
docker ps | grep soh
curl -s http://localhost:8400/health | python3 -m json.tool
```

Expected :
```json
{
    "status": "ok",
    "models_loaded": true
}
```

Note : au premier démarrage, les modèles ne sont pas encore entraînés. Le health check peut retourner `"models_loaded": false`. C'est normal.

- [ ] **Step 6: Smoke test API scoring**

```bash
curl -s http://localhost:8400/api/soh/batteries | python3 -m json.tool
```

Expected : JSON avec liste de batteries (vide si pas encore de données InfluxDB).

- [ ] **Step 7: Déployer SOH LLM (port 8401)**

```bash
docker compose -f services/soh-llm/docker-compose.soh-llm.yml up -d --build
```

Vérifier :
```bash
curl -s http://localhost:8401/health | python3 -m json.tool
```

Note : nécessite le modèle Qwen2.5-7B téléchargé et le LoRA adapter entraîné. Au premier démarrage sans modèle, le service démarre mais l'inférence échoue. Prévoir l'entraînement (Phase 3 du plan SOH-LLM) avant de tester.

- [ ] **Step 8: Vérifier les ports depuis le réseau local**

Depuis GrosMac :
```bash
curl -s http://kxkm-ai:8400/health
curl -s http://kxkm-ai:8401/health
```

Si timeout : vérifier le firewall kxkm-ai et les règles Tailscale.

---

### Task 4: Build app Android

- [ ] **Step 1: Vérifier le JDK + SDK**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/kxkm-bmu-app
java -version    # JDK 17+ requis
./gradlew --version
```

- [ ] **Step 2: Build shared module**

```bash
./gradlew :shared:build 2>&1 | tail -10
```

Expected : `BUILD SUCCESSFUL`.

Si erreur SQLDelight : vérifier que le schéma `BmuDatabase.sq` est syntaxiquement correct.

- [ ] **Step 3: Run shared tests**

```bash
./gradlew :shared:jvmTest 2>&1 | tail -20
```

Expected : tous les tests passent (GattParser, SohRestClient, SohUseCase, SohCache).

- [ ] **Step 4: Build APK debug**

```bash
./gradlew androidApp:assembleDebug 2>&1 | tail -10
```

Expected : `BUILD SUCCESSFUL`, APK dans `androidApp/build/outputs/apk/debug/`.

- [ ] **Step 5: Installer sur device**

```bash
./gradlew androidApp:installDebug
```

Ou via adb :
```bash
adb install androidApp/build/outputs/apk/debug/androidApp-debug.apk
```

- [ ] **Step 6: Vérifier la connexion BLE**

Lancer l'app → Dashboard → vérifier que le device "kryole" est détecté et connecté. Les cartes batterie doivent afficher voltage/courant/état.

- [ ] **Step 7: Vérifier le SOH badge**

Si des données SOH sont disponibles (batterie connectée + mesure R_int faite), le badge SOH s'affiche sur chaque carte batterie.

- [ ] **Step 8: Tester le trigger R_int**

Ouvrir le détail d'une batterie → appuyer sur "Mesurer R_int". Vérifier dans le serial monitor firmware que la mesure se lance. L'app doit afficher le résultat après ~1.5 s.

---

### Task 5: Build app iOS

- [ ] **Step 1: Ouvrir dans Xcode**

```bash
open kxkm-bmu-app/iosApp/KXKMBmu.xcodeproj
```

Ou si workspace :
```bash
open kxkm-bmu-app/iosApp/KXKMBmu.xcworkspace
```

- [ ] **Step 2: Résoudre les dépendances**

Dans Xcode : Product → Clean Build Folder, puis Build.

Si erreur `No such module 'Shared'` : vérifier que le framework KMP shared est correctement linké. Peut nécessiter :
```bash
cd kxkm-bmu-app
./gradlew :shared:embedAndSignAppleFrameworkForXcode
```

- [ ] **Step 3: Fixer les erreurs Swift unicode**

Les fichiers SwiftUI générés en Phase 4 ont des erreurs `Expected hexadecimal code in braces after unicode escape`. Ouvrir les fichiers suivants et remplacer les `\u00E9` (format Java/Kotlin) par les vrais caractères UTF-8 :

Fichiers à corriger :
- `iosApp/KXKMBmu/Views/SOH/DiagnosticCardView.swift`
- `iosApp/KXKMBmu/Views/SOH/SohDashboardView.swift`
- `iosApp/KXKMBmu/Views/SOH/FleetHealthView.swift`
- `iosApp/KXKMBmu/Views/Notification/SohNotificationDelegate.swift`

Remplacements :
- `\u00E9` → `é`
- `\u00E8` → `è`
- `\u00EA` → `ê`
- `\u00E0` → `à`
- `\u00F4` → `ô`

- [ ] **Step 4: Build + Run sur simulateur**

Xcode → Select iPhone 16 Simulator → Cmd+R.

Note : BLE ne fonctionne pas sur le simulateur. Tester uniquement la navigation et l'UI. Le REST client peut être testé si kxkm-ai est accessible depuis le Mac.

- [ ] **Step 5: Build sur device physique**

Brancher un iPhone → sélectionner comme target → Build & Run. Nécessite un profil de provisioning (Apple Developer account).

---

### Task 6: Test REST app ↔ kxkm-ai

**Prérequis:** kxkm-ai services running (Task 3), app installée (Task 4 ou 5).

- [ ] **Step 1: Vérifier la résolution DNS**

L'app utilise `http://kxkm-ai:8400` comme base URL. Vérifier que le device résout `kxkm-ai` :

Sur Android (adb shell) :
```bash
adb shell ping -c 1 kxkm-ai
```

Sur Mac :
```bash
ping -c 1 kxkm-ai
```

Si non résolu : `kxkm-ai` est via Tailscale. Le device mobile doit être sur le même réseau Tailscale, ou utiliser l'IP directe. Modifier `SohRestClient.kt` si besoin :
```kotlin
private val baseUrl: String = "http://<IP_KXKM_AI>:8400"
```

- [ ] **Step 2: Test curl depuis le device**

Simuler la requête app :
```bash
curl -s http://kxkm-ai:8400/api/soh/batteries
curl -s http://kxkm-ai:8400/api/soh/fleet
curl -s http://kxkm-ai:8401/api/diagnostic/0
```

- [ ] **Step 3: Tester dans l'app**

Ouvrir l'app → onglet SOH Dashboard. L'app doit :
1. Se connecter en BLE pour les données real-time
2. Appeler `GET /api/soh/batteries` pour les scores ML
3. Appeler `GET /api/diagnostic/{id}` pour les narratives

Si kxkm-ai n'a pas encore de données : les champs ML/diagnostic restent vides, les données BLE s'affichent normalement. Le mode offline cache les dernières valeurs.

- [ ] **Step 4: Vérifier le fleet view**

Onglet Fleet → le cercle de santé flotte doit s'afficher. Si pas de données ML : affiche "Pas de données" ou valeur par défaut.

- [ ] **Step 5: Tester le diagnostic on-demand**

Sur une carte batterie → bouton "Diagnostic" → l'app appelle `POST /api/diagnostic/{id}` → le texte français s'affiche dans la DiagnosticCard.

Si le LLM n'est pas encore entraîné : l'API retourne une erreur → l'app affiche un message d'erreur graceful.

---

### Task 7: Validation MQTT + InfluxDB telemetry

- [ ] **Step 1: Vérifier les topics MQTT**

Sur kxkm-ai ou depuis GrosMac :
```bash
mosquitto_sub -h 192.168.0.160 -t "bmu/#" -v
```

Expected (avec batteries connectées) :
```
bmu/rint/0 rint,battery=0,trigger=periodic r_ohmic_mohm=15.20,...
bmu/battery/0 battery,idx=0 voltage_mv=26500,current_ma=5200,...
```

Si pas de batteries : aucun message (normal).

- [ ] **Step 2: Vérifier InfluxDB**

```bash
ssh kxkm@kxkm-ai
influx query 'from(bucket: "bmu") |> range(start: -1h) |> filter(fn: (r) => r._measurement == "rint")' --org kxkm
```

Expected : données R_int si mesures effectuées.

- [ ] **Step 3: Vérifier le pipeline complet**

Trigger R_int depuis l'app (BLE 0x0038) → vérifier dans le serial monitor → vérifier MQTT → vérifier InfluxDB → vérifier que le SOH scoring API reflète les nouvelles données au prochain cycle (30 min).

---

## Checklist résumé

| Validation | Status | Bloquant ? |
|-----------|--------|-----------|
| Firmware flash + boot | - [ ] | Oui |
| WiFi + MQTT connecté | - [ ] | Non (fonctionne sans) |
| BLE discovery + notifications | - [ ] | Oui pour app |
| BLE SOH char (0x003A) | - [ ] | Non |
| BLE R_int trigger (0x0038) | - [ ] | Non |
| App Android build | - [ ] | Oui |
| App shared tests pass | - [ ] | Oui |
| App iOS build | - [ ] | Non (Android prioritaire) |
| Swift unicode fix | - [ ] | Oui pour iOS |
| kxkm-ai soh-scoring deploy | - [ ] | Non (app fonctionne offline) |
| kxkm-ai soh-llm deploy | - [ ] | Non (Phase 3 pré-requis) |
| REST app ↔ kxkm-ai | - [ ] | Non |
| MQTT + InfluxDB pipeline | - [ ] | Non |
