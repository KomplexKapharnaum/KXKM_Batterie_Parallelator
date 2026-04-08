# Plan d'Optimisation Firmware BMU — ESP-IDF

## État actuel

| Ressource | Utilisé | Disponible | Utilisation |
|-----------|---------|------------|-------------|
| Flash (OTA) | 1.71 MB | 2.0 MB | **82%** |
| DRAM | 249 KB | 342 KB | **73%** |
| IRAM | 16.4 KB | 16.4 KB | **100%** |
| PSRAM (Octal) | ~150 KB | 8 MB | 2% |
| Tâches FreeRTOS | 7 tâches | 28.5 KB stacks | |

### Top consommateurs

| Composant | Flash (code) | DRAM (data) |
|-----------|-------------|-------------|
| LVGL | 271 KB (16%) | 66 KB (27%) |
| bmu_display | 31 KB | 8 KB |
| bmu_ble (all) | 15 KB | 3.4 KB |
| bmu_influx | 7 KB | 4.6 KB |
| bmu_web | 7 KB | 0.4 KB |
| bmu_protection | 5 KB | 0 |
| chart_hist (PSRAM) | — | 150 KB |

---

## Phase 1 : Optimisation RAM (priorité haute)

### 1.1 Déplacer les buffers lourds en PSRAM

**Cible : -40 KB DRAM**

| Buffer | Taille | Actuellement | Déplacer vers |
|--------|--------|-------------|---------------|
| LVGL draw buffers | 2 × 320×240×2 = 30 KB | DRAM | PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) |
| BLE GATT chr defs `s_bat_chr_defs` | 3 KB | BSS (DRAM) | PSRAM ou allocation dynamique au runtime |
| InfluxDB buffer `s_buffer[4096]` | 4 KB | BSS | PSRAM |
| R_int cache `s_cache[32]` | 2.5 KB | BSS | PSRAM |

**Action :** Dans `bmu_display.cpp`, vérifier que les draw buffers LVGL sont alloués en PSRAM (le BSP BOX-3 devrait le faire). Si non, forcer avec `LV_MEM_CUSTOM=1` + `heap_caps_malloc`.

### 1.2 Réduire les tableaux statiques à `nb_ina` réel

**Cible : -8 KB DRAM**

Actuellement TOUS les tableaux sont dimensionnés à `BMU_MAX_BATTERIES=32`, même si seulement 8 batteries sont présentes.

| Tableau | Taille actuelle | Taille réelle (8 bat) | Économie |
|---------|----------------|----------------------|----------|
| `battery_voltages[32]` | 128 B | 32 B | 96 B |
| `nb_switch[32]` | 128 B | 32 B | 96 B |
| `reconnect_time_ms[32]` | 256 B | 64 B | 192 B |
| `battery_state[32]` | 128 B | 32 B | 96 B |
| `imbalance_count[32]` | 32 B | 8 B | 24 B |
| `s_rint_cache[32]` | 2560 B | 640 B | 1920 B |
| 10 × UI pointers `[32]` | 2560 B | 640 B | 1920 B |
| BLE handles `[32]` | 576 B | 144 B | 432 B |

**Approche :** Garder les tableaux statiques à 32 (évite les réallocs pour hotplug), mais déplacer les plus gros en PSRAM via `EXT_RAM_BSS_ATTR` ou allocation dynamique.

### 1.3 Optimiser les stacks de tâches

**Cible : -6 KB DRAM**

| Tâche | Stack actuel | Stack optimisé | Justification |
|-------|-------------|---------------|---------------|
| cloud | 4096 | 6144 (PSRAM) | snprintf + MQTT — trop petit, déplacer en PSRAM |
| Ah_all | 4096 | 2048 | Juste des lectures I2C + calculs float |
| vrm | 4096 | 2048 | Similaire à cloud mais plus léger |
| ws_push | 4096 | 2048 | Juste un push WebSocket |
| rint_periodic | 4096 | 3072 | Mesures I2C + calculs |
| hotplug | 3072 | 2048 | Probes I2C légers |
| vedirect | 4096 | 2048 | Parse UART texte |

**Méthode :** Utiliser `uxTaskGetStackHighWaterMark()` en debug pour mesurer l'usage réel, puis ajuster. Les tâches en PSRAM utilisent `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` (déjà activé).

---

## Phase 2 : Optimisation CPU/Cycles (priorité haute)

### 2.1 Réduire la fréquence de la boucle protection

**Actuellement :** 500ms × 8 batteries = 16 lectures I2C / seconde + 16 TCA switches potentiels.

**Proposition :**
- **Protection V/I :** garder 500ms (sécurité)
- **Imbalance check :** calculer `fleet_max` **une seule fois par cycle** au lieu de N fois
- **Balancer tick :** passer de 500ms à 2000ms (le balancing n'a pas besoin de réagir vite)
- **Display update :** passer de chaque cycle à toutes les 2s

### 2.2 Pré-calculer fleet_max une fois par cycle

**Gain : 7 appels à `find_fleet_max_mv()` économisés par cycle**

```cpp
// AVANT (dans check_battery, appelé 8 fois) :
is_imbalance_ok(v_mv, find_fleet_max_mv(ctx))  // itère les 8 batteries à chaque appel

// APRÈS (dans main loop, calculé une fois) :
float fleet_max = compute_fleet_max(&prot);
for (int i = 0; i < nb_ina; i++) {
    bmu_protection_check_battery_with_fleet(&prot, i, fleet_max);
}
```

### 2.3 Éviter les switch_battery(OFF) redondants

**Déjà fait** dans le fix précédent — ne switch OFF que sur transition d'état. Économise ~50ms I2C par batterie déjà OFF.

### 2.4 Réduire le spam I2C du hotplug

**Actuellement :** Le hotplug probe toutes les adresses inconnues (16 INA + 8 TCA = 24 probes potentiels) toutes les 10s, avec double-probe (50ms × 24 = 1.2s de bus occupé).

**Proposition :**
- Ne scanner que les adresses qui **étaient présentes au boot** + les adresses adjacentes
- Augmenter l'intervalle à 30s si la topologie est stable depuis 5 minutes

---

## Phase 3 : Optimisation Flash (priorité moyenne)

### 3.1 Optimiser LVGL (271 KB = 16% du flash)

| Option | Économie estimée | Impact |
|--------|-----------------|--------|
| Désactiver les widgets LVGL non utilisés | -30 à -50 KB | Aucun si bien ciblé |
| `LV_USE_CHART=0` et implémenter un chart minimal custom | -15 KB | Perte de la lib chart mais gain perf |
| `LV_USE_FLEX=0` si pas utilisé | -5 KB | |
| `LV_USE_GRID=0` si pas utilisé | -5 KB | |
| `LV_FONT_MONTSERRAT_14` seul (désactiver les autres) | -10 KB | |
| `LV_USE_THEME_DEFAULT=0` + theme minimal | -8 KB | |

**Méthode :** Auditer `lv_conf.h` ou `sdkconfig` pour les `LV_USE_*` activés mais non utilisés.

### 3.2 Compiler avec -Os au lieu de -Og

**Gain :** ~10-15% de flash en passant de `-Og` (debug) à `-Os` (size). Configurable via `menuconfig` → `Compiler options → Optimization Level`.

### 3.3 Activer le link-time optimization (LTO)

**Gain :** ~5-10% flash supplémentaire. `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y` + `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`.

---

## Phase 4 : Optimisation C++ / Architecture (priorité moyenne)

### 4.1 Éliminer les `vTaskDelay` dans la state machine

Les `switch_battery()` fait un `vTaskDelay(100ms)` pour le dead-time MOSFET et `50ms` pour le switch-off. Pendant ce temps, la tâche main est **bloquée** et ne traite pas les autres batteries.

**Proposition :** Architecture non-bloquante avec timer :
```cpp
// Au lieu de :
switch_battery(ON);  // bloque 100ms
// Utiliser :
set_switch_gpio(ON);
schedule_callback(100ms, verify_switch_and_set_led);
```

**Alternative plus simple :** Réduire les dead-times :
- MOSFET IRF4905 : temps de commutation ~50ns. Le 100ms est excessif → **10ms** suffit
- Switch OFF : 50ms → **5ms**

### 4.2 Snapshot atomique de l'état au début de chaque cycle

**Résout C1, H4 de l'audit :**

```cpp
// Au début de chaque cycle main loop :
typedef struct {
    float fleet_max;
    uint8_t nb_ina;
    uint8_t nb_tca;
} cycle_snapshot_t;

cycle_snapshot_t snap;
xSemaphoreTake(prot.state_mutex, ...);
snap.nb_ina = prot.nb_ina;
snap.nb_tca = prot.nb_tca;
snap.fleet_max = compute_fleet_max_locked(&prot);
xSemaphoreGive(prot.state_mutex);

for (int i = 0; i < snap.nb_ina; i++) {
    bmu_protection_check_battery(&prot, i, &snap);
}
```

### 4.3 Event-driven au lieu de polling pour le display

**Actuellement :** Le display met à jour toutes les batteries toutes les 500ms via la tâche LVGL.

**Proposition :** Notification par event group quand une tension change significativement (>100mV) :
```cpp
xEventGroupSetBits(display_events, BAT_UPDATED_BIT);
// La tâche display attend l'event au lieu de poller
```

---

## Phase 5 : Robustesse (priorité haute, en parallèle)

### 5.1 Corriger les data races de l'audit (C1, C3, H2, H3, H4)

| ID | Fix | Effort |
|----|-----|--------|
| C1 | fleet_max pré-calculé sous mutex (Phase 4.2) | 1h |
| C3 | `__atomic_load_n` pour nb_ina dans cloud task | 15min |
| H2 | `_Atomic int` pour `s_consecutive_failures` | 15min |
| H3 | Suspendre protection pendant compaction hotplug | 1h |
| H4 | Snapshot nb_ina en local au début du cycle | 15min |

### 5.2 Corriger le web switch (C2 de l'audit)

```cpp
esp_err_t bmu_protection_web_switch(ctx, idx, on) {
    if (state == LOCKED || state == ERROR) return ESP_ERR_NOT_ALLOWED;
    if (on) {
        // validate voltage + current + imbalance
        switch_battery(ON);
        ctx->nb_switch[idx]++;
        ctx->battery_state[idx] = BMU_STATE_RECONNECTING;
    }
}
```

### 5.3 Supprimer le bit-bang recovery (H1 de l'audit)

Le `gpio_config()` dans `bmu_i2c_bus_recover()` détache les pins I2C du périphérique. Comme `i2c_master_bus_reset()` fait déjà le recovery SCL, le fallback bit-bang est dangereux → le supprimer.

---

## Ordre d'exécution recommandé

| Sprint | Phase | Gain | Risque |
|--------|-------|------|--------|
| **1** | 5.1-5.3 | Robustesse | Faible |
| **2** | 2.1-2.3 | CPU -30% | Faible |
| **3** | 1.1-1.3 | DRAM -40 KB | Moyen (PSRAM latence) |
| **4** | 4.1-4.2 | CPU -20%, propreté | Moyen |
| **5** | 3.1-3.3 | Flash -50 KB | Faible |
| **6** | 4.3 | CPU display -50% | Moyen |

---

## Métriques de succès

| Métrique | Avant | Cible |
|----------|-------|-------|
| DRAM utilisé | 73% (249 KB) | <60% (205 KB) |
| Flash OTA | 82% (1.71 MB) | <75% (1.5 MB) |
| Boucle protection | ~800ms (8 bat × 100ms) | <200ms |
| I2C transactions/s | ~32 | <20 |
| Uptime stable | ~2 min → LOCKED | >24h sans disconnect |
| Watchdog LVGL | Occasionnel | Jamais |
