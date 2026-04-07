# Display Fix: écran blanc + swipe batterie + no-hscroll

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger l'écran blanc causé par les modifs display, ajouter le swipe gauche/droite dans la vue détail, garantir zéro scroll horizontal.

**Architecture:** L'écran blanc est probablement causé par le `swipe_cb` qui détruit le panel LVGL pendant son propre event callback (use-after-free → crash silencieux). La solution est de différer le changement de batterie via `lv_async_call`. Le lambda C++ dans `make_stat` pourrait aussi poser problème si le compilateur ne le supporte pas bien — le remplacer par une fonction statique.

**Tech Stack:** ESP-IDF 5.4, LVGL v9, ESP32-S3-BOX-3 (320×240)

---

### Task 1: Fix swipe use-after-free dans bmu_ui_detail.cpp

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_detail.cpp:141-162`

Le `swipe_cb` appelle `bmu_ui_detail_destroy()` puis `bmu_ui_detail_create()` pendant le callback LVGL du panel. Le panel est supprimé alors que LVGL est encore en train de traiter son event → use-after-free → crash/écran blanc.

**Fix:** utiliser `lv_async_call()` pour différer le destroy+create au prochain tick LVGL.

- [ ] **Step 1: Remplacer le swipe_cb par une version async-safe**

Remplacer dans `bmu_ui_detail.cpp` le block `swipe_cb` (lignes 141-162) par :

```cpp
/* ── Swipe gauche/droite — differe via lv_async_call ──────────── */

static int s_swipe_next = -1;

static void swipe_async_cb(void *data)
{
    (void)data;
    if (s_ctx_ref == NULL || s_nav_ref == NULL || s_swipe_next < 0) return;
    int next = s_swipe_next;
    s_swipe_next = -1;

    lv_obj_t *parent = (s_panel != NULL) ? lv_obj_get_parent(s_panel) : NULL;
    bmu_ui_detail_destroy();
    if (parent != NULL) {
        s_nav_ref->detail_visible = true;
        s_nav_ref->detail_battery = next;
        bmu_ui_detail_create(parent, s_ctx_ref, next);
    }
}

static void swipe_cb(lv_event_t *e)
{
    (void)e;
    if (s_ctx_ref == NULL || s_nav_ref == NULL) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    int nb = s_ctx_ref->nb_ina;
    if (nb <= 1) return;

    if (dir == LV_DIR_LEFT) {
        s_swipe_next = (s_battery_idx + 1) % nb;
    } else if (dir == LV_DIR_RIGHT) {
        s_swipe_next = (s_battery_idx - 1 + nb) % nb;
    } else {
        return;
    }
    ESP_LOGI(TAG, "Swipe BAT %d -> BAT %d (async)", s_battery_idx + 1, s_swipe_next + 1);
    lv_async_call(swipe_async_cb, NULL);
}
```

- [ ] **Step 2: Build et vérifier que ça compile**

```bash
cd firmware-idf && source ~/esp/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Expected: `Project build complete`

### Task 2: Fix lambda C++ dans bmu_ui_main.cpp

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_main.cpp:131-147`

Le lambda `auto make_stat = [&](...)` capture `stats_row` par référence. Certaines versions d'ESP-IDF xtensa gcc ont des bugs avec les lambdas C++ dans du code C-style. Remplacer par une fonction statique classique.

- [ ] **Step 1: Remplacer le lambda par une fonction statique**

Remplacer le block lambda + appels (lignes 131-147) par :

```cpp
    /* 4 labels compacts dans la flex row */
    auto add_stat_pair = [](lv_obj_t *row, const char *key, lv_obj_t **val_out) {
        lv_obj_t *klbl = lv_label_create(row);
        lv_label_set_text(klbl, key);
        lv_obj_set_style_text_color(klbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(klbl, &lv_font_montserrat_14, 0);

        *val_out = lv_label_create(row);
        lv_label_set_text(*val_out, "---");
        lv_obj_set_style_text_color(*val_out, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(*val_out, &lv_font_montserrat_14, 0);
    };

    add_stat_pair(stats_row, "V",  &s_vmoy_label);
    add_stat_pair(stats_row, "I",  &s_itot_label);
    add_stat_pair(stats_row, "+",  &s_ahin_label);
    add_stat_pair(stats_row, "-",  &s_ahout_label);
```

Le changement : `[&]` → `[]` (pas de capture, le lambda ne dépend que de ses paramètres).

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

### Task 3: Flash et vérifier

- [ ] **Step 1: Flash**

```bash
idf.py -p /dev/cu.usbmodem3101 flash 2>&1 | tail -5
```

- [ ] **Step 2: Vérifier visuellement**

L'écran doit afficher :
- Ligne 1: nom device + dots
- Ligne 2: V xxx I xxx +xxx -xxx
- Ligne 3: vmin vmoy vmax T°C
- Grille batteries scrollable verticalement

- [ ] **Step 3: Tester le swipe dans la vue détail**

1. Tap sur une batterie → overlay détail
2. Swipe gauche → batterie suivante
3. Swipe droite → batterie précédente
4. Bouton retour → grille

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_detail.cpp firmware-idf/components/bmu_display/bmu_ui_main.cpp
git commit -m "fix(display): async swipe + no-hscroll + stats bar layout"
```
