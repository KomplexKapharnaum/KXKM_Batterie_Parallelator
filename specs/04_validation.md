# Plan de validation — KXKM Batterie Parallelator

> Kill_LIFE gate: S3
> Version: 1.0 (2026-03-25)

---

## Vue d'ensemble

Trois niveaux de validation, du plus léger au plus complet :

| Niveau | Nom | Outillage | Couverture |
|--------|-----|-----------|------------|
| **L1** | Tests unitaires natifs | `pio test -e native` (pas d'ESP32) | Logique protection pure (F03–F08) |
| **L2** | Simulation étendue | `g++ sim_s3.cpp && ./sim_s3` (pas d'ESP32) | Tous scénarios firmware F01–F08, F11 |
| **L3** | Banc hardware | BMU + alimentation programmable + ESP32 | Couverture complète F01–F11 |

Statut actuel : **L1 ✅ L2 ✅ L3 ⬜**

---

## L1 — Tests unitaires natifs (`pio test -e native`)

Fichier : `firmware/test/test_protection/test_protection.cpp`

| ID | Test | Résultat |
|----|------|----------|
| U01 | `test_undervoltage_disconnects` | ✅ |
| U02 | `test_nominal_voltage_connects` | ✅ |
| U03 | `test_overvoltage_disconnects` | ✅ |
| U04 | `test_overcurrent_positive_disconnects` | ✅ |
| U05 | `test_overcurrent_negative_disconnects` | ✅ |
| U06 | `test_nominal_current_connects` | ✅ |
| U07 | `test_voltage_imbalance_disconnects` | ✅ |
| U08 | `test_voltage_imbalance_within_threshold_connects` | ✅ |
| U09 | `test_permanent_lock_above_max` | ✅ |
| U10 | `test_no_permanent_lock_at_max` | ✅ |

---

## L2 — Simulation étendue (`firmware/test/test_s3_sim/`)

Fichiers : `sim_s3.cpp` (scénarios S3) + `sim_validation.cpp` (validation étendue)

### Scénarios S3 de base

| ID | Scénario | Description | Résultat |
|----|----------|-------------|----------|
| S3-A | Déséquilibre tension | BAT1=27V BAT2=25V → coupure BAT2, remonte → reconnexion | ✅ |
| S3-B | Reconnexion auto | BAT2=22V → coupure → remonte → reconnexion immédiate (sw<5) | ✅ |
| S3-C | Verrouillage permanent | 5 cycles fault/normal → tempo 10s → 6e faute → lock | ✅ |

### Scénarios de validation étendue (`sim_validation.cpp`)

| ID | Scénario | Description | Résultat |
|----|----------|-------------|----------|
| TV01 | Seuil sous-tension exact | 24 000 mV = connexion autorisée ; 23 999 mV = coupure | ✅ |
| TV02 | Seuil sur-tension exact | 30 000 mV = connexion autorisée ; 30 001 mV = coupure | ✅ |
| TV03 | Sur-courant positif | +1.1 A → coupure ; +0.9 A → connexion | ✅ |
| TV04 | Sur-courant négatif | −1.1 A → coupure ; −0.9 A → connexion | ✅ |
| TV05 | Seuil déséquilibre exact | diff = 1.0 V → connexion ; diff = 1.001 V → coupure | ✅ |
| TV06 | 4 batteries, 1 en faute | BAT1–3 ok, BAT4=22V → seule BAT4 coupée, les autres ok | ✅ |
| TV07 | Temporisation 10 s précise | Au tick nb_switch=5 : tempo, pas de reconnexion avant 10 s | ✅ |
| TV08 | Faute courant + déséquilibre | Double faute sur même batterie → 1 seule incrémentation sw | ✅ |

---

## L3 — Banc hardware

### Matériel requis

| Quantité | Composant | Spec | Usage |
|----------|-----------|------|-------|
| 2 | Alimentation programmable | 0–35 V / 5 A min | Simule tensions batterie |
| 1 | Charge résistive ou électronique | 5–50 Ω / 5 W | Injection de courant test |
| 2 | Multimètre de précision | ±0.5% DC | Vérification mesures INA237 |
| 1 | BMU v1 ou v2 (PCB) | — | Carte sous test |
| 1 | ESP32 Wrover Kit (ou K32) | 16 MB flash | Microcontrôleur |
| 1 | Câble USB série | — | Moniteur 115200 baud |
| 1 | PC + PlatformIO | — | Flash + monitoring |
| 4 | Fils de mesure (banana) | ≥ 4 mm², 5 A | Connexion puissance |

### Montage du banc

```
  [Alim 1]──────┐          [ESP32 / K32]
  0–35V/5A      │              │ I²C (GPIO32/33)
                ├──[INA237_0]──┤──[TCA9535_0]──[MOSFET 0]──[LED 🟢/🔴]
                │              │
  [Alim 2]──────┤
  0–35V/5A      ├──[INA237_1]──┤──[TCA9535_0]──[MOSFET 1]──[LED 🟢/🔴]
                │              │
  [Charge R]────┘         [USB série] → PC (115200 baud)
  (optionnel)
```

> Commencer avec 2 batteries (Alim 1 + Alim 2) sur TCA9535_0.
> BMU v1 fabriqué = cible de test principale.

### Procédures de test hardware

#### TB01 — I²C scan (F01 F02 prérequis)
```
1. Flasher le firmware : pio run -e kxkm-v3-16MB --target upload
2. Ouvrir moniteur série : pio device monitor --baud 115200
3. Observer le scan I²C au démarrage
```
**Pass** : Toutes les adresses INA237 (0x40–) et TCA9535 (0x20–) attendues apparaissent.
**Fail** : Adresse manquante → vérifier câblage I²C, solder jumpers JP.

---

#### TB02 — Calibration mesure tension (F01)
```
1. Alim 1 → 27.00 V (réglé sur source)
2. Mesurer la tension en parallèle avec multimètre
3. Lire valeur INA237 sur moniteur série
```
**Pass** : Écart INA237 vs multimètre < ±0.5% (±135 mV sur 27 V).

---

#### TB03 — Calibration mesure courant (F02)
```
1. Alim 1 → 27.00 V, charge R = 27 Ω → courant théorique = 1.0 A
2. Mesurer courant avec pince ampèremétrique ou shunt externe
3. Lire valeur INA237 sur moniteur série
```
**Pass** : Écart INA237 vs référence < ±5% (±50 mA sur 1 A).

> Rappel : shunt = 2 mΩ (BMU v1/v2), plage INA237 = ±163.84 mV → max ~82 A.

---

#### TB04 — Test relais MOSFET (F03 F04 base)
```
1. Alim 1 → 27 V, vérifier LED verte allumée (batterie connectée)
2. Observer signal de commande TCA9535 (GPIO 0–3) avec oscilloscope ou LED de test
3. Baisser Alim 1 à 22 V
4. Attendre 500 ms (1 tick)
```
**Pass** : LED passe rouge, relais MOSFET ouvert (mesurer continuité ou Vout=0).

---

#### TB05 — Protection sous-tension (F04)
```
1. Alim 1 → 27 V (nominal)
2. Baisser progressivement jusqu'à 24.0 V → vérifier connexion maintenue
3. Baisser à 23.9 V → vérifier coupure
4. Remonter à 27 V → vérifier reconnexion (nb_switch=1 < 5)
```
**Pass** :
- 24.0 V → connecté
- 23.9 V → coupure dans les 500 ms suivant la lecture
- Retour 27 V → reconnexion dans les 500 ms

---

#### TB06 — Protection sur-tension (F03)
```
1. Alim 1 → 27 V
2. Monter à 30.0 V → connexion maintenue
3. Monter à 30.1 V → coupure attendue
4. Redescendre à 27 V → reconnexion
```
**Pass** : même logique que TB05.

---

#### TB07 — Protection sur-courant (F05)
```
1. Alim 1 → 27 V, charge variable
2. Charge légère (R=100Ω → ~270 mA) → connexion maintenue
3. Charge lourde (R=24Ω → ~1.1 A) → coupure attendue dans 500 ms
4. Couper charge → reconnexion
```
**Pass** : coupure à I > 1 A, connexion à I < 1 A.
**Note** : tester aussi courant négatif (retour de charge régénérative si dispo).

---

#### TB08 — Déséquilibre tension 2 batteries (F06)
```
1. Alim 1 → 27 V, Alim 2 → 25 V
2. Les deux connectées au démarrage
3. Attendre 1 tick (500 ms)
```
**Pass** :
- BAT2 déconnectée (LED rouge), BAT1 maintenue (LED verte)
- nb_switch(BAT2) inchangé (déséquilibre ne compte pas)
- Remonter Alim 2 à 27 V → BAT2 reconnectée dans 500 ms

---

#### TB09 — Reconnexion automatique & tempo (F07)
```
1. Provoquer 4 coupures manuelles sur BAT1 (sous-tension répétée)
2. À la 5e : observer message "too many cut off battery, try to reconnect in X s"
3. Mesurer durée avant reconnexion
```
**Pass** : reconnexion dans 10 s ± 1 s après la 5e coupure.

---

#### TB10 — Verrouillage permanent (F08)
```
1. Provoquer 6 coupures sur BAT1
2. Observer message "too many cut off battery, constant cut off"
3. Remonter tension à nominal → vérifier que BAT1 reste déconnectée
```
**Pass** : LED rouge fixe, pas de reconnexion quelle que soit la tension.
**Reset** : power cycle ESP32 (Nb_switch remis à 0 en RAM).

---

#### TB11 — Test 4 batteries simultanées (F11)
```
1. Connecter 4 alimentations sur TCA9535_0 / INA237 0x40–0x43
2. Vérifier scan I²C : 4 adresses INA + 1 TCA
3. Mettre BAT3 en sous-tension (22 V)
4. Vérifier isolation : BAT1, BAT2, BAT4 restent connectées
```
**Pass** : seule BAT3 est coupée.

---

#### TB12 — Log série (F09)
```
1. Lancer pio device monitor --baud 115200
2. Observer les messages pendant les tests TB05–TB11
```
**Pass** : messages présents et cohérents avec l'état observé (tension, courant, action).

---

#### TB13 — Interface web (F10)
```
1. Connecter ESP32 au WiFi (ou mode AP)
2. Ouvrir navigateur → IP de l'ESP32
3. Vérifier affichage tension/courant en temps réel
```
**Pass** : données affichées, rafraîchissement automatique.
**Priorité** : COULD (optionnel en V1).

---

## Critères d'acceptance globaux

| Critère | Seuil |
|---------|-------|
| Couverture MUST (F01–F08, F11) | 100% pass |
| Couverture SHOULD (F09) | pass ou justification |
| Couverture COULD (F10) | best effort |
| Précision mesure tension | ±0.5% pleine échelle |
| Précision mesure courant | ±5% |
| Timing coupure | ≤ 1 tick (500 ms) après dépassement seuil |
| Timing reconnexion temporisée | 10 s ± 1 s |
| Isolation des coupures (multi-bat) | 0 faux-positif sur batteries saines |

---

## Ordre d'exécution recommandé

```
L1 ✅ → L2 ✅ → TB01 → TB02 → TB03 → TB04
                                    ↓
         TB13 ← TB12 ← TB11 ← TB10 ← TB09 ← TB08 ← TB07 ← TB06 ← TB05
```

Arrêt sur premier FAIL bloquant (TB01–TB04 sont des prérequis).
