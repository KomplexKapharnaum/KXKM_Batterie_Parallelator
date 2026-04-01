# Refonte LVGL UI/UX — ESP32-S3-BOX-3

**Date:** 2026-04-01
**Écran:** 320×240 px, 2.4" capacitive touch, ILI9342C
**LVGL:** v9.1.0, font montserrat_14 uniquement
**Thème:** High Contrast — noir pur (#000), couleurs saturées, lisible en plein soleil

## Objectif

Refonte complète de l'interface LVGL du BMU : nouveau layout, palette high contrast, fonctionnalités manquantes (climat AHT30, courant réel INA237, status BLE/WiFi), config runtime NVS, et correction des bugs critiques.

## Navigation — 5 onglets

Tab bar en bas, icônes + labels, 64px par tab (320/5).

| # | Icône | Label | Contenu |
|---|-------|-------|---------|
| 1 | ⚡ | BATT | Top bar stats + liste batteries |
| 2 | 🧠 | SOH | Prédiction santé IA (TFLite) |
| 3 | 📊 | SYS | Connexion, climat, solar, firmware, I2C |
| 4 | ⚠ | ALERT | Liste chronologique avec badge compteur |
| 5 | ⚙ | CONFIG | Device name, WiFi, seuils, MQTT, BLE, luminosité |

## Palette High Contrast

| Usage | Couleur | Hex |
|-------|---------|-----|
| Fond écran | Noir pur | #000000 |
| Fond carte | Gris très foncé | #0A0A0A / #111111 |
| Texte principal | Blanc pur | #FFFFFF |
| Texte secondaire | Gris clair | #CCCCCC |
| Texte tertiaire | Gris moyen | #666666 |
| OK / Connecté | Vert saturé | #00FF66 |
| Warning / Usure | Orange | #FFAA00 |
| Erreur / OFF | Rouge saturé | #FF0044 |
| Info / Titre BMU | Bleu ciel | #00AAFF |
| SOH / Santé | Cyan | #00FFCC |
| Solar | Jaune | #FFD600 |
| Fond erreur | Rouge sombre | #0A0000 / #1A0000 |
| Fond warning | Orange sombre | #0A0800 |

## Écran 1 — ⚡ BATT (Dashboard)

### Top Bar (fixe, ~55px)

**Ligne 1 :** Nom device (bleu, ex: "⚡ kryole") + indicateurs connexion (●BLE ●WiFi ○MQTT)

**Ligne 2 :** 5 tuiles stats horizontales :

| V MOY | I TOTAL | Ah IN | Ah OUT | CLIMAT |
|-------|---------|-------|--------|--------|
| 26.4V | 4.1A | 12.3 | 8.2 | 23°C 45% |

Chaque tuile : fond #111, valeur grande (14px bold), label petit (7px gris).
V MOY en vert si dans la plage, orange/rouge sinon.

### Liste batteries (scrollable, reste de l'écran)

Chaque ligne = une batterie :

```
[border-left 3px couleur] B1 [===barre===] 26.4V  1.2A
```

- Border left : vert (OK), rouge (OFF/erreur), orange (warning)
- Barre de progression : voltage mappé sur la plage 24-30V, gradient vert ou orange
- Batterie OFF : fond rouge sombre, texte rouge, barre vide
- Tap sur une batterie → overlay détail (écran modal)

### Overlay Détail Batterie

Déclenché par tap sur une ligne batterie. Plein écran avec bouton retour.

**Layout :**
- Header : "← Ret" + "BAT N" (bleu)
- Chart : 150×90px, 60 points (30s), voltage (bleu) + courant (vert), axes auto-scale
- Valeurs : V, I, P (calculé), T (AHT30 si dispo), Ah+, Ah-, nb switches, état
- Boutons : Switch ON (vert) / Switch OFF (rouge) / Reset (orange)
- **Dialog de confirmation** obligatoire avant Switch ON/OFF

**Fixes critiques :**
- Courant réel depuis INA237 (plus hardcodé 0A)
- `s_nav_ref` correctement initialisé
- Plage chart auto-scale (pas hardcodé 20-32V)

## Écran 2 — 🧠 SOH (State of Health IA)

**Header :** "🧠 State of Health" (cyan) + timestamp dernière prédiction

**SOH Moyen :** Grande carte centrée avec pourcentage (24px bold), barre de progression.

**Liste par batterie :** Barres horizontales avec pourcentage et couleur :
- ≥70% : vert (#00FF66) — OK
- 40-70% : orange (#FFAA00) — USURE
- <40% : rouge (#FF3333) — REMPLACER (label d'alerte)

**Légende :** En bas, 3 points couleur avec labels.

**Source données :** `bmu_soh_get_cached(i)` — retourne 0.0-1.0 ou <0 si invalide.

## Écran 3 — 📊 SYS (Système)

5 sections compactes, pas de scroll, tout visible d'un coup.

### Section CONNEXION
Ligne horizontale : ●BLE (vert/gris) | ●WiFi IP (vert/gris) | ●MQTT (vert/orange)

### Section CLIMAT
🌡 **23.5°C** | 💧 **45%** — données AHT30 (bmu_climate_get_*)

### Section SOLAIRE
- État MPPT (Float/Bulk/Absorption) avec couleur
- PV tension, puissance, bat tension, yield jour
- Masqué si VE.Direct non connecté

### Section FIRMWARE
v0.5.0 | Heap 16.1MB | Up 2h34m | INA:4 TCA:1

### Section I2C BUS
Device count + error count. 3 dernières lignes de log (compactes).

## Écran 4 — ⚠ ALERT

**Badge compteur** sur l'icône tab (rouge, nombre d'alertes actives).

**Liste scrollable** chronologique (plus récente en haut) :
- Timestamp HH:MM:SS
- Message avec icône couleur (rouge=erreur, orange=warning, vert=info)
- Ring buffer 30 alertes max

**Bouton CLEAR** en haut à droite pour effacer.

## Écran 5 — ⚙ CONFIG

Sections avec contrôles natifs LVGL :

### Device Name
Champ texte + clavier virtuel LVGL. Valeur depuis `bmu_config_get_device_name()`.

### WiFi
- SSID : champ texte + clavier
- Password : champ texte + clavier (masqué)
- Status : ●Connecté IP / ○Déconnecté

### Seuils Protection
Steppers (±) sans clavier :
- V min : 20000-30000 mV (pas 500)
- V max : 25000-35000 mV (pas 500)
- I max : 1000-50000 mA (pas 1000)
- V diff : 100-5000 mV (pas 100)

### MQTT
Champ texte URI (mqtt://...)

### Options
- BLE : toggle ON/OFF
- Luminosité : slider 0-100%
- Dim timeout : stepper 0-300s (pas 10)

### Bouton Sauvegarder
Gros bouton vert en bas. Écrit dans NVS via `bmu_config_set_*()`.

## Contraintes techniques

- **Font :** montserrat_14 uniquement (sauf si on active montserrat_10 pour les labels très petits — nécessite menuconfig)
- **LVGL pool :** 64KB — surveiller avec `lv_mem_monitor()`
- **Display buffer :** 12.8KB DMA en RAM interne (pas PSRAM)
- **Thread safety :** tout accès LVGL sous `bsp_display_lock()/unlock()`
- **Refresh :** timer 100ms, chart push 500ms
- **Touch :** GT911 capacitif, callback pour wake backlight

## Bugs à corriger

1. Courant batterie toujours 0.0A → lire `bmu_ina237_read_current()` dans le chart push
2. `s_nav_ref` jamais initialisé dans detail → passer via `bmu_ui_main_set_nav_state()`
3. Plage chart hardcodée 20-32V → auto-scale basé sur min/max des données
4. Pas de confirmation Switch ON/OFF → dialog `lv_msgbox` avant action
5. Liste alertes reconstruite entièrement à chaque ajout → update incrémental

## Hors scope

- Multi-font (rester sur montserrat_14 sauf nécessité prouvée)
- Animations/transitions complexes (budget CPU serré)
- Écran de splash / boot logo
- Internationalisation (tout en français)
