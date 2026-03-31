# Phase 8: Display Dashboard Ameliore — Design Spec

**Projet:** KXKM Batterie Parallelator (BMU)
**Date:** 2026-03-31
**Status:** Approved

---

## 1. Ecran grille batteries (amelioration existant)

- Tap simple sur une cellule batterie → navigation vers ecran detail de cette batterie
- L'ecran detail remplace le contenu de l'onglet Batt (pas un nouvel onglet)
- Bouton "← Retour" pour revenir a la grille
- Les cellules sont clickables via `lv_obj_add_event_cb(cell, cb, LV_EVENT_CLICKED, (void*)idx)`

## 2. Ecran detail batterie (nouveau)

### Graphique temps reel V/I
- LVGL `lv_chart` widget, 2 series (voltage mV en bleu, courant A en vert)
- Historique configurable via Kconfig : `CONFIG_BMU_CHART_HISTORY_POINTS` (defaut 600 = 5 min @ 500ms)
- Ring buffer statique par batterie (pas de malloc dynamique)
- Mise a jour toutes les 500ms (synchro avec la boucle protection)

### Valeurs numeriques
- Tension (V), Courant (A), Puissance (W), Temperature (°C)
- Ah decharge, Ah charge
- Nb coupures / max
- Etat : CONNECTED/DISCONNECTED/RECONNECTING/ERROR/LOCKED (couleur)

### Actions
- Bouton "Switch ON" / "Switch OFF" → appelle `bmu_protection_web_switch()`
- Bouton "Reset compteur" → appelle `bmu_protection_reset_switch_count()`
- Dialog de confirmation avant chaque action (lv_msgbox)

## 3. 5e onglet "Solar" (nouveau)

- Affiche les donnees VE.Direct du chargeur Victron MPPT
- PV tension (V), PV puissance (W), courant charge (A)
- Etat MPPT : Off/Bulk/Absorption/Float/Equalize (texte + couleur)
- Yield aujourd'hui (Wh), yield total (kWh), puissance max jour (W)
- Infos chargeur : Product ID, serial, firmware
- Si VE.Direct non connecte : message "Pas de chargeur detecte"
- Donnees lues via `bmu_vedirect_get_data()`

## 4. Modifications bmu_display.cpp

- Tabview passe de 4 a 5 onglets : Batt / Sys / Alert / I2C / Solar
- Ecran detail est un `lv_obj_t*` cree dynamiquement au tap, detruit au retour
- Periodic callback met a jour : grille OU detail (selon lequel est visible), system, debug, solar
- Nouvelle Kconfig : `CONFIG_BMU_CHART_HISTORY_POINTS` (defaut 600)

## 5. Fichiers

| Fichier | Action |
|---------|--------|
| `bmu_display.cpp` | Modifier — ajouter 5e tab, gestion navigation detail |
| `bmu_ui_main.cpp` | Modifier — ajouter callback tap sur cellules |
| `bmu_ui_detail.cpp` | Recrire — graphique chart + valeurs + boutons actions |
| `bmu_ui_solar.cpp` | Creer — ecran VE.Direct |
| `bmu_display/Kconfig` | Modifier — ajouter CONFIG_BMU_CHART_HISTORY_POINTS |
| `bmu_display/include/bmu_ui.h` | Modifier — ajouter declarations solar + detail ameliore |
