//! Duty cycling balancer, non-autoritaire. Cf spec §6.2.
//!
//! Règle absolue : le balancer propose, la protection dispose.
//! `BalancerEngine::tick()` produit un bitmask `u16` de préférences (1 bit
//! par batterie) ; `bmu-core` applique un `AND-mask` avec la sortie
//! protection pour la décision finale.
//!
//! Approximation `SoC` V1 : la tension est utilisée comme proxy du niveau
//! de charge (les batteries "pleines" ont une tension plus haute). Une
//! deadband de ±2 % autour du max fleet évite le jitter.

#![no_std]

use bmu_types::{BatteryState, Snapshot, MAX_BATTERIES};

/// Période complète du cycle `PWM` software en ticks.
/// Le compteur interne `cycle_pos` parcourt `0..=255` (256 valeurs)
/// avant de wrapper, donc un cycle complet dure exactement 256 ticks
/// soit ~51 s à 5 Hz.
pub const DUTY_CYCLE_PERIOD: u16 = 256;

/// Tolérance `SoC` en pourcentage : les batteries dont l'écart absolu au max
/// est <= à cette valeur ne sont pas balancées (pas de jitter).
pub const SOC_DEADBAND_PCT: u8 = 2;

/// Pénalité appliquée au `target` des batteries hors-deadband.
/// Creuse l'écart entre batteries hautes et basses pour accentuer le
/// balancing. Valeur empirique V1, à calibrer en Phase 10 si besoin.
const DEADBAND_PENALTY: u8 = 10;

/// Agent de balancing. Stateful : `cycle_pos` avance à chaque tick,
/// `targets` est recalculé à chaque tick depuis le `Snapshot`.
///
/// NOTE : pas `Copy` — state machine stateful. Une copie accidentelle
/// créerait une divergence de phase `PWM` silencieuse. Passer par
/// `&mut BalancerEngine` uniquement.
#[derive(Debug, Clone, Default)]
pub struct BalancerEngine {
    cycle_pos: u8,
    targets: [u8; MAX_BATTERIES],
}

impl BalancerEngine {
    /// Crée un balancer en position 0 avec tous les targets à 0.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            cycle_pos: 0,
            targets: [0; MAX_BATTERIES],
        }
    }

    /// Recalcule les duties targets à partir du `Snapshot`, avance d'un step
    /// dans le cycle `PWM`, et retourne le bitmask des batteries que le
    /// balancer voudrait voir ON ce tick (préférence, pas autorité).
    #[must_use]
    #[allow(clippy::indexing_slicing)]
    pub fn tick(&mut self, snapshot: &Snapshot) -> u16 {
        self.compute_targets(snapshot);
        self.cycle_pos = self.cycle_pos.wrapping_add(1);
        let mut mask: u16 = 0;
        let n = (snapshot.n_bat as usize).min(MAX_BATTERIES);
        for i in 0..n {
            if self.targets[i] > self.cycle_pos {
                mask |= 1 << i;
            }
        }
        mask
    }

    /// Retourne le duty `%` actuel pour `idx` (0..100), ou 0 si hors-range.
    /// Conversion : `target / 255 × 100`.
    #[must_use]
    #[allow(clippy::indexing_slicing, clippy::cast_possible_truncation)]
    pub fn duty_pct(&self, idx: u8) -> u8 {
        if (idx as usize) < MAX_BATTERIES {
            ((u16::from(self.targets[idx as usize]) * 100) / 255) as u8
        } else {
            0
        }
    }

    /// Calcule les duties targets.
    /// Règle : les plus hautes tensions (proxy `SoC`) = targets plus hauts.
    /// Deadband `SOC_DEADBAND_PCT` pour éviter le jitter autour du max.
    /// Les batteries non-`Online` ont `target = 0`.
    #[allow(
        clippy::indexing_slicing,
        clippy::arithmetic_side_effects,
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn compute_targets(&mut self, snapshot: &Snapshot) {
        let n = (snapshot.n_bat as usize).min(MAX_BATTERIES);

        // Trouver le max voltage parmi les Online
        let mut max_v: i32 = 0;
        for i in 0..n {
            let bat = &snapshot.batteries[i];
            if bat.state == BatteryState::Online && bat.voltage.as_raw() > max_v {
                max_v = bat.voltage.as_raw();
            }
        }

        // Pour chaque batterie : target proportionnel à (v / max_v)
        for i in 0..n {
            let bat = &snapshot.batteries[i];
            if bat.state != BatteryState::Online || max_v == 0 {
                self.targets[i] = 0;
                continue;
            }
            // proximity = (v × 255) / max_v, clampé dans [0, 255]
            let proximity_raw = (i64::from(bat.voltage.as_raw()) * 255) / i64::from(max_v);
            let proximity = proximity_raw.clamp(0, 255) as u8;

            // Deadband : si la batterie est très proche du max, garder le
            // target haut (pas de décrément) pour éviter le jitter.
            let deadband_thresh: u8 = 255 - (((255_u16 * u16::from(SOC_DEADBAND_PCT)) / 100) as u8);
            self.targets[i] = if proximity > deadband_thresh {
                proximity
            } else {
                // Les batteries plus basses : légèrement réduites (décale
                // le ratio pour accentuer le balancing)
                proximity.saturating_sub(DEADBAND_PENALTY)
            };
        }

        // Reset les indices hors fleet à 0 (safety)
        for i in n..MAX_BATTERIES {
            self.targets[i] = 0;
        }
    }
}

#[cfg(test)]
#[allow(
    clippy::unwrap_used,
    clippy::panic,
    clippy::field_reassign_with_default,
    clippy::cast_possible_truncation,
    clippy::indexing_slicing
)]
mod tests {
    use super::*;
    use bmu_types::{Milliamps, Millivolts};

    fn fleet(n: u8, voltages_mv: &[i32]) -> Snapshot {
        let mut snap = Snapshot::default();
        snap.n_bat = n;
        for (i, v) in voltages_mv.iter().enumerate().take(n as usize) {
            snap.batteries[i].idx = i as u8;
            snap.batteries[i].state = BatteryState::Online;
            snap.batteries[i].voltage = Millivolts::from_raw(*v);
            snap.batteries[i].current = Milliamps::from_raw(-500);
        }
        snap
    }

    #[test]
    fn new_balancer_all_targets_zero() {
        let b = BalancerEngine::new();
        for i in 0..16 {
            assert_eq!(b.duty_pct(i), 0);
        }
    }

    #[test]
    fn tick_empty_snapshot_returns_zero_mask() {
        let mut b = BalancerEngine::new();
        let snap = Snapshot::default();
        assert_eq!(b.tick(&snap), 0);
    }

    #[test]
    fn tick_all_offline_returns_zero() {
        let mut b = BalancerEngine::new();
        let mut snap = fleet(4, &[25_000, 26_000, 27_000, 28_000]);
        for i in 0..4 {
            snap.batteries[i].state = BatteryState::Offline;
        }
        assert_eq!(b.tick(&snap), 0);
    }

    #[test]
    fn tick_uniform_fleet_balanced() {
        // Toutes à la même tension → targets ≈ 255 → duty ≈ 100 %
        let mut b = BalancerEngine::new();
        let snap = fleet(4, &[27_000, 27_000, 27_000, 27_000]);
        let mut ons = [0_u32; 4];
        for _ in 0..512 {
            let mask = b.tick(&snap);
            for (i, counter) in ons.iter_mut().enumerate() {
                if (mask >> i) & 1 == 1 {
                    *counter += 1;
                }
            }
        }
        for (i, &count) in ons.iter().enumerate() {
            assert!(count > 400, "bat {i} too often off: {count}");
        }
    }

    #[test]
    fn tick_offline_battery_never_selected() {
        let mut b = BalancerEngine::new();
        let mut snap = fleet(4, &[27_000, 27_000, 27_000, 27_000]);
        snap.batteries[1].state = BatteryState::Offline;
        for _ in 0..512 {
            let mask = b.tick(&snap);
            assert_eq!((mask >> 1) & 1, 0, "offline bat in mask");
        }
    }

    #[test]
    fn duty_pct_range() {
        let mut b = BalancerEngine::new();
        let snap = fleet(4, &[27_000, 26_500, 26_000, 25_500]);
        let _ = b.tick(&snap);
        for i in 0..4 {
            assert!(b.duty_pct(i) <= 100);
        }
    }

    #[test]
    fn cycle_pos_wraps() {
        // Run 300 ticks → cycle_pos wraps u8 à 256 via wrapping_add
        let mut b = BalancerEngine::new();
        let snap = fleet(4, &[27_000, 27_000, 27_000, 27_000]);
        for _ in 0..300 {
            let _ = b.tick(&snap);
        }
        // No panic → wrap OK
    }

    /// Cœur algorithmique : batterie plus haute tension → duty plus élevé.
    /// Filet de sécurité contre une inversion future de la logique deadband.
    #[test]
    fn higher_voltage_has_higher_duty() {
        let mut b = BalancerEngine::new();
        // 28 V (dans deadband) vs 26 V (hors deadband, penalty)
        let snap = fleet(2, &[28_000, 26_000]);
        let _ = b.tick(&snap);
        assert!(
            b.duty_pct(0) > b.duty_pct(1),
            "haute tension doit avoir duty plus élevé: bat0={} bat1={}",
            b.duty_pct(0),
            b.duty_pct(1),
        );
    }
}
