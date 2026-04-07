# Soft-Balancing + R_int opportuniste — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Équilibrer les batteries par duty-cycling lent (ON/OFF à ~1 Hz) et mesurer la résistance interne gratuitement pendant les fenêtres OFF.

**Architecture:** Nouveau composant `bmu_balancer` qui s'insère entre la protection et le main loop. Pour chaque batterie connectée dont la tension dépasse la moyenne flotte + seuil, le balancer réduit le duty-cycle (OFF pendant N cycles sur M). Pendant chaque transition ON→OFF, on déclenche une mesure R_int opportuniste via `bmu_rint_on_disconnect()` existant. Le balancer ne touche pas aux batteries en ERROR/LOCKED/DISCONNECTED.

**Tech Stack:** ESP-IDF 5.4, FreeRTOS, bmu_tca9535 (switch), bmu_rint (R_int), bmu_protection (état)

---

## File Map

| File | Action | Responsabilité |
|------|--------|----------------|
| `firmware-idf/components/bmu_balancer/include/bmu_balancer.h` | Create | API publique |
| `firmware-idf/components/bmu_balancer/bmu_balancer.cpp` | Create | Logique duty-cycling + R_int trigger |
| `firmware-idf/components/bmu_balancer/Kconfig` | Create | Seuils configurables |
| `firmware-idf/components/bmu_balancer/CMakeLists.txt` | Create | Build config |
| `firmware-idf/main/main.cpp` | Modify | Init + appel dans loop |
| `firmware-idf/test/test_balancer/main/test_balancer.cpp` | Create | Tests unitaires |

## Contraintes hardware rappel

- TCA9535 I2C write : ~200 µs à 50 kHz
- Dead-time MOSFET : 100ms ON / 50ms OFF (dans switch_battery)
- Main loop : 500 ms
- R_int mesure : V2 à +100ms, V3 à +1000ms après OFF
- Minimum 2 batteries connectées pour toute opération OFF

## Algorithme

```
Pour chaque batterie i (toutes les 500ms, dans le main loop) :
  Si état != CONNECTED → skip (la protection gère)
  
  Calculer V_moy = moyenne des CONNECTED
  delta = V_bat[i] - V_moy
  
  Si delta > BALANCE_HIGH_MV (ex: +200 mV) :
    → incrémenter compteur_off[i]
    → Si compteur_off[i] >= duty_on_cycles (ex: 3) :
        switch OFF bat[i]
        déclencher R_int opportuniste (V1=V_bat, I1=I_bat déjà lus)
        compteur_off[i] = 0
        compteur_on_wait[i] = duty_off_cycles (ex: 2)
    
  Si compteur_on_wait[i] > 0 :
    → décrémenter compteur_on_wait[i]
    → Si compteur_on_wait[i] == 0 :
        switch ON bat[i]
    → Sinon : rester OFF
        Si == duty_off_cycles - 1 (100ms après OFF) : lire V2 pour R_ohmic
        Si == 0 (1000ms après OFF) : lire V3 pour R_total
  
  Sinon :
    → batterie ON, duty normal
```

Duty-cycle effectif pour une batterie trop chargée : 3 cycles ON, 2 cycles OFF → 60% ON → réduit sa contribution au courant flotte.

---

### Task 1: Kconfig + CMakeLists + Header

**Files:**
- Create: `firmware-idf/components/bmu_balancer/Kconfig`
- Create: `firmware-idf/components/bmu_balancer/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_balancer/include/bmu_balancer.h`

- [ ] **Step 1: Créer le Kconfig**

```
menu "BMU Soft Balancer"

    config BMU_BALANCER_ENABLED
        bool "Enable soft-balancing"
        default y

    config BMU_BALANCE_HIGH_MV
        int "Seuil haut au-dessus de V_moy pour reduire duty (mV)"
        default 200
        range 50 1000
        depends on BMU_BALANCER_ENABLED

    config BMU_BALANCE_DUTY_ON
        int "Nombre de cycles ON avant un cycle OFF"
        default 3
        range 1 10
        depends on BMU_BALANCER_ENABLED

    config BMU_BALANCE_DUTY_OFF
        int "Nombre de cycles OFF (500ms chacun)"
        default 2
        range 1 6
        depends on BMU_BALANCER_ENABLED
        help
            2 cycles = 1000ms OFF, compatible avec mesure R_int
            (V2 à +100ms, V3 à +1000ms)

    config BMU_BALANCE_MIN_CONNECTED
        int "Minimum batteries connectées pour equilibrer"
        default 3
        range 2 32
        depends on BMU_BALANCER_ENABLED

endmenu
```

- [ ] **Step 2: Créer le CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "bmu_balancer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_protection bmu_rint bmu_tca9535 bmu_ina237
)
```

- [ ] **Step 3: Créer le header**

```cpp
#pragma once

#include "esp_err.h"
#include "bmu_protection.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialiser le balancer. Appeler après bmu_protection_init(). */
esp_err_t bmu_balancer_init(bmu_protection_ctx_t *prot);

/**
 * Tick du balancer — appeler une fois par cycle main loop (500ms).
 * Gère le duty-cycling et déclenche les mesures R_int opportunistes.
 * @return nombre de batteries en cours d'équilibrage (OFF par duty)
 */
int bmu_balancer_tick(void);

/** Retourne true si la batterie idx est actuellement en phase OFF (duty) */
bool bmu_balancer_is_off(uint8_t idx);

/** Retourne le duty-cycle effectif (0-100%) pour une batterie */
int bmu_balancer_get_duty_pct(uint8_t idx);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_balancer/
git commit -m "feat(balancer): component scaffold — Kconfig, header, CMakeLists"
```

---

### Task 2: Implémentation du balancer

**Files:**
- Create: `firmware-idf/components/bmu_balancer/bmu_balancer.cpp`

- [ ] **Step 1: Écrire l'implémentation**

```cpp
/**
 * bmu_balancer — Soft-balancing par duty-cycling + mesure R_int opportuniste.
 *
 * Principe : les batteries dont la tension dépasse V_moy + seuil sont
 * déconnectées périodiquement (duty réduit) pour réduire leur contribution
 * au courant flotte. Pendant les fenêtres OFF, on mesure R_int gratis.
 */

#include "bmu_balancer.h"
#include "bmu_protection.h"
#include "bmu_rint.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "esp_log.h"
#include <cmath>

static const char *TAG = "BALANCER";

#if !CONFIG_BMU_BALANCER_ENABLED

esp_err_t bmu_balancer_init(bmu_protection_ctx_t *) { return ESP_OK; }
int bmu_balancer_tick(void) { return 0; }
bool bmu_balancer_is_off(uint8_t) { return false; }
int bmu_balancer_get_duty_pct(uint8_t) { return 100; }

#else

static bmu_protection_ctx_t *s_prot = NULL;

/* État par batterie */
static struct {
    int  on_counter;      /* cycles ON restants avant OFF */
    int  off_counter;     /* cycles OFF restants avant ON */
    bool balancing;       /* true = cette batterie est duty-cycled */
    float v_before_mv;    /* V sous charge avant le OFF (pour R_int) */
    float i_before_a;     /* I sous charge avant le OFF (pour R_int) */
} s_bat[BMU_MAX_BATTERIES];

esp_err_t bmu_balancer_init(bmu_protection_ctx_t *prot)
{
    if (prot == NULL) return ESP_ERR_INVALID_ARG;
    s_prot = prot;
    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        s_bat[i].on_counter  = CONFIG_BMU_BALANCE_DUTY_ON;
        s_bat[i].off_counter = 0;
        s_bat[i].balancing   = false;
    }
    ESP_LOGI(TAG, "Init OK — seuil=%d mV, duty ON=%d OFF=%d, min_conn=%d",
             CONFIG_BMU_BALANCE_HIGH_MV, CONFIG_BMU_BALANCE_DUTY_ON,
             CONFIG_BMU_BALANCE_DUTY_OFF, CONFIG_BMU_BALANCE_MIN_CONNECTED);
    return ESP_OK;
}

int bmu_balancer_tick(void)
{
    if (s_prot == NULL) return 0;

    int nb = s_prot->nb_ina;
    if (nb < CONFIG_BMU_BALANCE_MIN_CONNECTED) return 0;

    /* ── Calcul V_moy des batteries connectées ────────────────────── */
    float sum_v = 0;
    int n_conn = 0;
    for (int i = 0; i < nb; i++) {
        if (bmu_protection_get_state(s_prot, i) != BMU_STATE_CONNECTED) continue;
        if (s_bat[i].off_counter > 0) continue; /* exclure les OFF du calcul */
        float v = bmu_protection_get_voltage(s_prot, i);
        if (v > 1000.0f) { /* > 1V = capteur valide */
            sum_v += v;
            n_conn++;
        }
    }

    if (n_conn < CONFIG_BMU_BALANCE_MIN_CONNECTED) return 0;
    float v_moy = sum_v / (float)n_conn;

    int balancing_count = 0;

    for (int i = 0; i < nb; i++) {
        bmu_battery_state_t state = bmu_protection_get_state(s_prot, i);

        /* Ne jamais toucher aux batteries non connectées */
        if (state != BMU_STATE_CONNECTED && s_bat[i].off_counter == 0) {
            s_bat[i].balancing = false;
            continue;
        }

        /* ── Batterie en phase OFF (duty réduit) ─────────────────── */
        if (s_bat[i].off_counter > 0) {
            s_bat[i].off_counter--;
            balancing_count++;

            /* Cycle 1 après OFF (500ms ≈ temps pour R_int V2+V3) :
             * bmu_rint_on_disconnect() a déjà été appelé au moment du OFF,
             * il gère les lectures V2/V3 en interne avec ses propres delays.
             * Rien à faire ici. */

            if (s_bat[i].off_counter == 0) {
                /* Fin de la fenêtre OFF → reconnecter */
                int tca_idx = i / 4;
                int ch = i % 4;
                if (tca_idx < s_prot->nb_tca) {
                    bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], ch, true);
                    bmu_tca9535_set_led(&s_prot->tca_devices[tca_idx], ch, false, true);
                    ESP_LOGD(TAG, "BAT[%d] balance ON (fin duty OFF)", i + 1);
                }
                s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
            }
            continue;
        }

        /* ── Batterie en phase ON — vérifier si elle doit être duty-cycled ── */
        float v = bmu_protection_get_voltage(s_prot, i);
        float delta = v - v_moy;

        if (delta > (float)CONFIG_BMU_BALANCE_HIGH_MV) {
            /* Tension trop haute → décompter les cycles ON */
            s_bat[i].on_counter--;
            s_bat[i].balancing = true;

            if (s_bat[i].on_counter <= 0) {
                /* Temps de déconnecter pour réduire le duty */
                int tca_idx = i / 4;
                int ch = i % 4;
                if (tca_idx < s_prot->nb_tca) {
                    /* Lire V/I avant le OFF pour R_int */
                    float v1 = 0, i1 = 0;
                    bmu_ina237_read_voltage_current(
                        &s_prot->ina_devices[i], &v1, &i1);
                    s_bat[i].v_before_mv = v1;
                    s_bat[i].i_before_a = i1;

                    /* Switch OFF */
                    bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], ch, false);
                    bmu_tca9535_set_led(&s_prot->tca_devices[tca_idx], ch, true, false);

                    /* Déclencher mesure R_int opportuniste */
                    bmu_rint_on_disconnect((uint8_t)i, v1, i1);

                    ESP_LOGI(TAG, "BAT[%d] balance OFF (V=%.0f > moy=%.0f +%d) R_int triggered",
                             i + 1, v, v_moy, CONFIG_BMU_BALANCE_HIGH_MV);
                }
                s_bat[i].off_counter = CONFIG_BMU_BALANCE_DUTY_OFF;
                balancing_count++;
            }
        } else {
            /* Tension OK — reset duty, pas de balancing */
            s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
            s_bat[i].balancing = false;
        }
    }

    return balancing_count;
}

bool bmu_balancer_is_off(uint8_t idx)
{
    if (idx >= BMU_MAX_BATTERIES) return false;
    return s_bat[idx].off_counter > 0;
}

int bmu_balancer_get_duty_pct(uint8_t idx)
{
    if (idx >= BMU_MAX_BATTERIES || !s_bat[idx].balancing) return 100;
    int total = CONFIG_BMU_BALANCE_DUTY_ON + CONFIG_BMU_BALANCE_DUTY_OFF;
    return (CONFIG_BMU_BALANCE_DUTY_ON * 100) / total;
}

#endif /* CONFIG_BMU_BALANCER_ENABLED */
```

- [ ] **Step 2: Commit**

```bash
git add firmware-idf/components/bmu_balancer/bmu_balancer.cpp
git commit -m "feat(balancer): soft-balancing duty-cycling + R_int opportuniste"
```

---

### Task 3: Intégration dans main.cpp

**Files:**
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Ajouter l'include et l'init**

Après `#include "bmu_influx_store.h"` ajouter :
```cpp
#include "bmu_balancer.h"
```

Après `bmu_rint_start_periodic()` dans app_main, ajouter :
```cpp
    bmu_balancer_init(&prot);
```

- [ ] **Step 2: Ajouter le tick dans le main loop**

Dans la boucle principale (après les appels `bmu_protection_check_battery` et avant le `vTaskDelay`), ajouter :

```cpp
        /* Soft-balancing : duty-cycling des batteries trop chargées */
        int balancing = bmu_balancer_tick();
        if (balancing > 0) {
            ESP_LOGD("MAIN", "%d batterie(s) en equilibrage", balancing);
        }
```

- [ ] **Step 3: Protection interaction — skip les batteries OFF par le balancer**

Dans la boucle `for (int i = 0; i < nb_ina; i++)` du main loop, avant `bmu_protection_check_battery`, ajouter :

```cpp
            /* Skip la protection si le balancer a mis cette batterie en OFF volontaire */
            if (bmu_balancer_is_off((uint8_t)i)) continue;
```

Ceci évite que la protection ne détecte un "disconnect" et incrémente le compteur nb_switch pour une déconnexion volontaire.

- [ ] **Step 4: Build et vérifier**

```bash
cd firmware-idf && source ~/esp/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Expected: `Project build complete`

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/main/main.cpp
git commit -m "feat(main): integrate soft-balancer in main loop"
```

---

### Task 4: Tests unitaires

**Files:**
- Create: `firmware-idf/test/test_balancer/main/test_balancer.cpp`
- Create: `firmware-idf/test/test_balancer/CMakeLists.txt`

- [ ] **Step 1: Écrire les tests**

Tests de la logique de décision (pas de hardware, stubs pour les V/I) :

```cpp
#include <cassert>
#include <cstring>

/* ── Stubs minimaux pour tester la logique ─────────────────────────── */

#define BMU_MAX_BATTERIES 32
#define CONFIG_BMU_BALANCER_ENABLED 1
#define CONFIG_BMU_BALANCE_HIGH_MV 200
#define CONFIG_BMU_BALANCE_DUTY_ON 3
#define CONFIG_BMU_BALANCE_DUTY_OFF 2
#define CONFIG_BMU_BALANCE_MIN_CONNECTED 3

/* Stub protection state */
static float stub_voltages[BMU_MAX_BATTERIES];
static int   stub_states[BMU_MAX_BATTERIES]; /* 0=CONNECTED */
static int   stub_nb_ina = 4;

/* Helpers — ces fonctions seraient normalement dans les vrais composants */
float bmu_protection_get_voltage(void *, int idx) { return stub_voltages[idx]; }
int   bmu_protection_get_state(void *, int idx)   { return stub_states[idx]; }

/* ── Tests ─────────────────────────────────────────────────────────── */

static void test_no_balancing_when_voltages_equal()
{
    for (int i = 0; i < 4; i++) {
        stub_voltages[i] = 27000.0f; /* Toutes à 27.000V */
        stub_states[i] = 0;          /* CONNECTED */
    }
    /* V_moy = 27000, delta = 0 pour toutes → aucune ne dépasse +200mV */
    /* Pas de balancing attendu */
}

static void test_high_voltage_battery_gets_duty_cycled()
{
    stub_voltages[0] = 27500.0f; /* +500 mV au-dessus */
    stub_voltages[1] = 27000.0f;
    stub_voltages[2] = 27000.0f;
    stub_voltages[3] = 27000.0f;
    /* V_moy ≈ 27125, delta[0] = +375 > 200 → bat 0 duty-cycled */
    /* Après 3 ticks ON → OFF pendant 2 ticks → ON */
}

static void test_disconnected_battery_not_balanced()
{
    stub_voltages[0] = 27500.0f;
    stub_states[0] = 1; /* DISCONNECTED */
    /* Ne doit jamais être touchée par le balancer */
}

int main()
{
    test_no_balancing_when_voltages_equal();
    test_high_voltage_battery_gets_duty_cycled();
    test_disconnected_battery_not_balanced();
    return 0;
}
```

Note : ces tests sont des smoke tests de la logique. Le vrai test sera le terrain avec batteries réelles.

- [ ] **Step 2: CMakeLists.txt pour le test**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_balancer)
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/test/test_balancer/
git commit -m "test(balancer): smoke tests logique duty-cycling"
```

---

### Task 5: Affichage du balancing sur l'écran

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_main.cpp`

- [ ] **Step 1: Indicateur visuel sur la barre batterie**

Dans `bmu_ui_main_update()`, dans la boucle per-battery, après le calcul de couleur par état, ajouter :

```cpp
        /* Indicateur balancing : barre jaune si duty-cycled */
        if (bmu_balancer_is_off((uint8_t)i)) {
            col = UI_COLOR_WARN; /* Orange pendant le OFF duty */
        }
```

Et ajouter `#include "bmu_balancer.h"` en haut du fichier.

- [ ] **Step 2: Build + flash**

```bash
idf.py build && idf.py -p /dev/cu.usbmodem3101 flash
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_main.cpp
git commit -m "feat(display): indicateur orange pour batteries en equilibrage"
```

---

## Timing détaillé

```
Cycle main loop (500 ms)
│
├── Protection check (toutes batteries)
│   └── Skip batteries où balancer_is_off == true
│
├── Balancer tick
│   ├── Calcul V_moy (CONNECTED only, excluant les OFF)
│   │
│   ├── Pour BAT trop haute (delta > 200 mV) :
│   │   ├── Cycle 1-3 : ON (compteur décompte)
│   │   ├── Cycle 3→4 : OFF + lecture V1/I1 + rint_on_disconnect()
│   │   │                 └── rint fait en interne :
│   │   │                     +100ms → V2 (ohmique)
│   │   │                     +1000ms → V3 (total)
│   │   ├── Cycle 4 : OFF (rint en cours)
│   │   └── Cycle 5 : ON (duty reprend)
│   │
│   └── Pour BAT normale : ON continu, reset compteurs
│
└── vTaskDelay(500 ms)
```

Duty effectif : 3 ON / 2 OFF = **60% ON** pour les batteries trop chargées.
R_int mesurée **automatiquement** à chaque fenêtre OFF, sans task périodique séparée.

## Sécurité

- Le balancer ne touche **jamais** aux batteries ERROR/LOCKED/DISCONNECTED
- Minimum 3 batteries connectées pour activer (configurable)
- La protection est bypassée (skip) pendant les OFF → pas d'incrémentation nb_switch
- Si une erreur survient pendant le OFF, la protection reprend au cycle suivant
- Le duty minimum (1 ON / 6 OFF = 14%) laisse toujours la batterie connectée régulièrement
