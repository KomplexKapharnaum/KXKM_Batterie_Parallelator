/**
 * sim_validation.cpp — Simulation étendue L2 — KXKM Batterie Parallelator
 *
 * Couvre TV01–TV08 du plan specs/04_validation.md
 * Même logique firmware que sim_s3.cpp (main.cpp).
 *
 * Compilation : g++ -std=c++14 -o sim_validation sim_validation.cpp && ./sim_validation
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

// ── Paramètres firmware ───────────────────────────────────────────────────────
static const float ALERT_MIN_MV      = 24000.0f;
static const float ALERT_MAX_MV      = 30000.0f;
static const float ALERT_MAX_CURRENT = 1.0f;
static const float VOLTAGE_DIFF_V    = 1.0f;
static const int   NB_SWITCH_MAX     = 5;
static const long  RECONNECT_DELAY   = 10000;
static const long  LOOP_INTERVAL     = 500;

// ── Batterie ──────────────────────────────────────────────────────────────────
struct Battery {
    int   id;
    float voltage_mv;
    float current_a;
    bool  connected;
    int   nb_switch;
    int   check_switch;
    long  reconnect_time;
};

#define MAX_BAT 16
static Battery bat[MAX_BAT];
static int nb_bat = 0;
static long vtime_ms = 0;
long millis() { return vtime_ms; }

// ── Résultats ────────────────────────────────────────────────────────────────
static int pass_count = 0;
static int fail_count = 0;

static void check(const char* id, const char* desc, bool condition) {
    if (condition) {
        printf("  %-6s ✅  %s\n", id, desc);
        pass_count++;
    } else {
        printf("  %-6s ❌  %s\n", id, desc);
        fail_count++;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static float find_max_mv(Battery* b, int n) {
    float mx = 0;
    for (int i = 0; i < n; i++) mx = (b[i].voltage_mv > mx) ? b[i].voltage_mv : mx;
    return mx;
}

static void init_park(int n, float voltage_mv = 27000, float current_a = 0.0f) {
    nb_bat = n;
    for (int i = 0; i < n; i++)
        bat[i] = {i, voltage_mv, current_a, true, 0, 0, 0};
    vtime_ms = 0;
}

// Boucle firmware (identique à sim_s3.cpp, sans printf — retourne les actions)
struct LoopResult { bool disconnected[MAX_BAT]; };

static LoopResult bmu_loop_silent() {
    LoopResult r;
    memset(&r, 0, sizeof(r));
    float vmax_v = find_max_mv(bat, nb_bat) / 1000.0f;

    for (int i = 0; i < nb_bat; i++) {
        float vmv = bat[i].voltage_mv;
        float ia  = bat[i].current_a;
        float vv  = vmv / 1000.0f;

        if (vmv < ALERT_MIN_MV) {
            if (bat[i].check_switch == 0) { bat[i].nb_switch++; bat[i].check_switch = 1; }
            bat[i].connected = false;
            r.disconnected[i] = true;
        } else if (vmv > ALERT_MAX_MV) {
            if (bat[i].check_switch == 0) { bat[i].nb_switch++; bat[i].check_switch = 1; }
            bat[i].connected = false;
            r.disconnected[i] = true;
        } else if (ia > ALERT_MAX_CURRENT) {
            if (bat[i].check_switch == 0) { bat[i].nb_switch++; bat[i].check_switch = 1; }
            bat[i].connected = false;
            r.disconnected[i] = true;
        } else if (ia < -ALERT_MAX_CURRENT) {
            if (bat[i].check_switch == 0) { bat[i].nb_switch++; bat[i].check_switch = 1; }
            bat[i].connected = false;
            r.disconnected[i] = true;
        } else {
            bat[i].check_switch = 0;
            if (bat[i].nb_switch < NB_SWITCH_MAX) {
                bat[i].connected = true;
            } else if (bat[i].nb_switch == NB_SWITCH_MAX) {
                // Firmware : check_switch=1 avant switch_off → nb_switch PAS incrémenté ici
                if (bat[i].reconnect_time == 0) {
                    bat[i].reconnect_time = millis();  // armer timer seulement
                }
                if (millis() - bat[i].reconnect_time > RECONNECT_DELAY) {
                    bat[i].connected = true;           // reconnexion après délai
                } else {
                    bat[i].connected = false;
                    r.disconnected[i] = true;
                }
            } else {
                bat[i].connected = false;
                r.disconnected[i] = true;
            }

            // Déséquilibre — ne pas incrémenter nb_switch (firmware : check_switch=1 avant)
            if ((vmax_v - vv) > VOLTAGE_DIFF_V) {
                bat[i].connected = false;
                r.disconnected[i] = true;
            }
        }
    }
    return r;
}

static void tick(int n = 1) {
    for (int k = 0; k < n; k++) {
        vtime_ms += LOOP_INTERVAL;
        bmu_loop_silent();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TV01 — Seuil sous-tension exact
// ─────────────────────────────────────────────────────────────────────────────
static void tv01() {
    printf("\n── TV01 — Seuil sous-tension exact ─────────────────────────────\n");

    // 24 000 mV = limite basse → connexion autorisée
    init_park(1, 24000);
    tick();
    check("TV01a", "24 000 mV → connecté (seuil inclus)", bat[0].connected == true);

    // 23 999 mV → coupure
    init_park(1, 23999);
    tick();
    check("TV01b", "23 999 mV → coupure", bat[0].connected == false);

    // 24 001 mV → connexion
    init_park(1, 24001);
    tick();
    check("TV01c", "24 001 mV → connecté", bat[0].connected == true);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV02 — Seuil sur-tension exact
// ─────────────────────────────────────────────────────────────────────────────
static void tv02() {
    printf("\n── TV02 — Seuil sur-tension exact ──────────────────────────────\n");

    init_park(1, 30000);
    tick();
    check("TV02a", "30 000 mV → connecté (seuil inclus)", bat[0].connected == true);

    init_park(1, 30001);
    tick();
    check("TV02b", "30 001 mV → coupure", bat[0].connected == false);

    init_park(1, 29999);
    tick();
    check("TV02c", "29 999 mV → connecté", bat[0].connected == true);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV03 — Sur-courant positif
// ─────────────────────────────────────────────────────────────────────────────
static void tv03() {
    printf("\n── TV03 — Sur-courant positif ───────────────────────────────────\n");

    // +0.9 A → connexion
    init_park(1, 27000, 0.9f);
    tick();
    check("TV03a", "+0.90 A → connecté", bat[0].connected == true);

    // +1.0 A → connexion (seuil non dépassé)
    init_park(1, 27000, 1.0f);
    tick();
    check("TV03b", "+1.00 A → connecté (seuil exact)", bat[0].connected == true);

    // +1.1 A → coupure
    init_park(1, 27000, 1.1f);
    tick();
    check("TV03c", "+1.10 A → coupure", bat[0].connected == false);
    check("TV03d", "nb_switch incrémenté", bat[0].nb_switch == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV04 — Sur-courant négatif
// ─────────────────────────────────────────────────────────────────────────────
static void tv04() {
    printf("\n── TV04 — Sur-courant négatif ───────────────────────────────────\n");

    init_park(1, 27000, -0.9f);
    tick();
    check("TV04a", "−0.90 A → connecté", bat[0].connected == true);

    init_park(1, 27000, -1.1f);
    tick();
    check("TV04b", "−1.10 A → coupure", bat[0].connected == false);
    check("TV04c", "nb_switch incrémenté", bat[0].nb_switch == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV05 — Seuil déséquilibre exact
// ─────────────────────────────────────────────────────────────────────────────
static void tv05() {
    printf("\n── TV05 — Seuil déséquilibre exact ─────────────────────────────\n");

    // diff = 1.0 V → connexion (seuil non dépassé : condition > 1V)
    init_park(2, 27000);
    bat[0].voltage_mv = 27000;
    bat[1].voltage_mv = 26000;  // diff = 1.0 V exactement
    tick();
    check("TV05a", "diff = 1.000 V → connecté (seuil non dépassé)", bat[1].connected == true);
    check("TV05b", "nb_switch BAT2 inchangé (déséquilibre ne compte pas)", bat[1].nb_switch == 0);

    // diff = 1.001 V → coupure
    init_park(2, 27000);
    bat[0].voltage_mv = 27001;
    bat[1].voltage_mv = 26000;  // diff = 1.001 V
    tick();
    check("TV05c", "diff = 1.001 V → coupure", bat[1].connected == false);
    check("TV05d", "nb_switch BAT2 inchangé (déséquilibre ne compte pas)", bat[1].nb_switch == 0);

    // diff = 0.9 V → connexion
    init_park(2, 27000);
    bat[0].voltage_mv = 27000;
    bat[1].voltage_mv = 26100;  // diff = 0.9 V
    tick();
    check("TV05e", "diff = 0.900 V → connecté", bat[1].connected == true);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV06 — 4 batteries, 1 en faute
// ─────────────────────────────────────────────────────────────────────────────
static void tv06() {
    printf("\n── TV06 — 4 batteries, isolation d'une faute ───────────────────\n");

    init_park(4, 27000);
    bat[2].voltage_mv = 22000;  // BAT3 en sous-tension
    tick();

    check("TV06a", "BAT1 reste connectée", bat[0].connected == true);
    check("TV06b", "BAT2 reste connectée", bat[1].connected == true);
    check("TV06c", "BAT3 coupée (22V < 24V)", bat[2].connected == false);
    check("TV06d", "BAT4 reste connectée", bat[3].connected == true);
    check("TV06e", "nb_switch BAT3 = 1", bat[2].nb_switch == 1);
    check("TV06f", "nb_switch BAT1/2/4 = 0", bat[0].nb_switch == 0 && bat[1].nb_switch == 0 && bat[3].nb_switch == 0);

    // Rétablissement BAT3
    bat[2].voltage_mv = 27000;
    bat[2].check_switch = 0;
    tick();
    check("TV06g", "BAT3 reconnectée après retour nominal", bat[2].connected == true);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV07 — Temporisation 10 s précise
// ─────────────────────────────────────────────────────────────────────────────
static void tv07() {
    printf("\n── TV07 — Temporisation 10 s (nb_switch = %d) ──────────────────\n", NB_SWITCH_MAX);

    init_park(1, 27000);

    // Provoquer NB_SWITCH_MAX coupures
    for (int k = 0; k < NB_SWITCH_MAX; k++) {
        bat[0].voltage_mv = 22000;
        bat[0].check_switch = 0;
        tick();           // coupure
        bat[0].voltage_mv = 27000;
        bat[0].check_switch = 0;
        tick();           // retour
    }
    // À ce stade nb_switch == NB_SWITCH_MAX, reconnect_time vient d'être armé
    bool locked_before = !bat[0].connected;
    check("TV07a", "Pas de reconnexion immédiate au tick nb_switch=max", locked_before);

    // Avancer de 9 s (< 10 s) → toujours en attente
    long t_before = vtime_ms;
    vtime_ms += 9000;
    bmu_loop_silent();
    check("TV07b", "Toujours déconnecté à t+9 s", bat[0].connected == false);

    // Avancer de 1.5 s → total > 10 s → reconnexion
    vtime_ms += 1500;
    bmu_loop_silent();
    check("TV07c", "Reconnecté à t+10.5 s (après délai 10 s)", bat[0].connected == true);

    printf("         Durée totale simulée : %ld ms\n", vtime_ms - t_before + 1000);
}

// ─────────────────────────────────────────────────────────────────────────────
// TV08 — Double faute : courant + déséquilibre simultanés
// ─────────────────────────────────────────────────────────────────────────────
static void tv08() {
    printf("\n── TV08 — Double faute (courant + déséquilibre simultanés) ─────\n");

    init_park(2, 27000);
    bat[0].voltage_mv = 27000;
    bat[0].current_a  = 1.5f;   // sur-courant
    bat[1].voltage_mv = 25000;  // déséquilibre (27-25=2V > 1V)

    tick();

    check("TV08a", "BAT1 coupée (sur-courant)", bat[0].connected == false);
    check("TV08b", "BAT1 nb_switch = 1 (pas de double-comptage)", bat[0].nb_switch == 1);
    check("TV08c", "BAT2 coupée (déséquilibre)", bat[1].connected == false);
    check("TV08d", "BAT2 nb_switch = 0 (déséquilibre ne compte pas)", bat[1].nb_switch == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║   Simulation L2 étendue — Gate S3 Validation                    ║\n");
    printf("║   TV01–TV08 · specs/04_validation.md                            ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");

    tv01();
    tv02();
    tv03();
    tv04();
    tv05();
    tv06();
    tv07();
    tv08();

    printf("\n══════════════════════════════════════════════════════════════════\n");
    printf("  Résultat L2 : %d/%d pass\n", pass_count, pass_count + fail_count);
    if (fail_count == 0)
        printf("  → Logique firmware conforme aux specs pour tous les cas limites.\n");
    else
        printf("  → %d cas en échec — voir détail ci-dessus.\n", fail_count);
    printf("══════════════════════════════════════════════════════════════════\n\n");

    return fail_count > 0 ? 1 : 0;
}
