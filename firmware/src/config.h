#pragma once

/**
 * config.h — Configuration centrale KXKM Batterie Parallelator
 *
 * Tous les seuils de protection et paramètres firmware sont définis ici.
 * Inclure avant tout autre header dans main.cpp.
 *
 * Unités : tensions en mV, courants en A, temps en ms.
 */

// ── Bus I²C ──────────────────────────────────────────────────────────────────
#define I2C_Speed             50      // Vitesse I²C en kHz (50 = fiable sur câblage long)

// ── Protection tension ───────────────────────────────────────────────────────
#define alert_bat_min_voltage 24000   // Seuil sous-tension en mV  (24 V)
#define alert_bat_max_voltage 30000   // Seuil sur-tension en mV   (30 V)

// ── Protection courant ───────────────────────────────────────────────────────
#define alert_bat_max_current 10       // Seuil sur-courant en A (valeur absolue)
#define current_diff          10      // Réservé (non utilisé en V1)

// ── Déséquilibre tension ─────────────────────────────────────────────────────
#define voltage_diff          1       // Tolérance déséquilibre entre batteries (V)

// ── Reconnexion / verrouillage ───────────────────────────────────────────────
#define reconnect_delay       10000   // Délai de reconnexion en ms (nb_switch == max)
#define Nb_switch_max         5       // Coupures max avant verrouillage permanent
