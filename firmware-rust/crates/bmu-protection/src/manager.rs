//! Battery manager : Ah counting, topology check, per-battery counters.

use bmu_types::{BatteryState, MilliampHours, Milliamps, MAX_BATTERIES};

use crate::latch::SwitchCounter;
use crate::state::Transition;

/// F09 : vérifie la contrainte `Nb_TCA` × 4 == `Nb_INA`.
/// Cas dégradé boot : 0/0 considéré KO pour forcer fail-safe all OFF.
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub const fn topology_ok(n_ina: u8, n_tca: u8) -> bool {
    if n_ina == 0 && n_tca == 0 {
        return false;
    }
    (n_tca as u16) * 4 == n_ina as u16
}

/// F02 : intègre le courant dans le compteur `Ah` pendant `dt_ms`.
/// Formule : `delta_mah = (i_ma × dt_ms + residue) / 3_600_000`, le reste
/// est stocké dans `residue_ma_ms` pour préserver la précision sub-mAh
/// entre appels successifs.
/// Signé : discharge (`i_ma` négatif) décrémente, charge incrémente.
/// Saturating sur `i32` overflow.
#[inline]
#[must_use]
#[allow(
    clippy::arithmetic_side_effects,
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap
)]
pub fn integrate_charge(
    current_ah: MilliampHours,
    i: Milliamps,
    dt_ms: u32,
    residue_ma_ms: &mut i64,
) -> MilliampHours {
    // i_ma × dt_ms en i64 évite overflow pour dt jusqu'à ~10 min
    let charge_ma_ms = i64::from(i.as_raw()) * i64::from(dt_ms);
    let total = charge_ma_ms + *residue_ma_ms;
    let delta_mah = (total / 3_600_000) as i32;
    *residue_ma_ms = total - i64::from(delta_mah) * 3_600_000;
    current_ah.saturating_add(MilliampHours::from_raw(delta_mah))
}

/// `BatteryManager` : stocke les counters par batterie + topology.
/// État persistant (counters + Ah accumulators) — l'état courant de chaque
/// batterie est dans le `Snapshot` côté runtime.
///
/// NOTE : pas `Copy` — ce container porte de l'état mutable. Une copie
/// accidentelle créerait une divergence silencieuse entre le manager
/// original et la copie. Passer par `&mut BatteryManager` uniquement.
#[derive(Debug, Clone)]
pub struct BatteryManager {
    n_bat: u8,
    counters: [SwitchCounter; MAX_BATTERIES],
    ah_accumulators: [MilliampHours; MAX_BATTERIES],
    ah_residues: [i64; MAX_BATTERIES],
}

impl BatteryManager {
    /// Crée un manager vide (aucune batterie, counters à zéro).
    #[must_use]
    pub const fn new() -> Self {
        Self {
            n_bat: 0,
            counters: [SwitchCounter::new(); MAX_BATTERIES],
            ah_accumulators: [MilliampHours::ZERO; MAX_BATTERIES],
            ah_residues: [0_i64; MAX_BATTERIES],
        }
    }

    /// Nombre de batteries actuellement gérées (0 si topology invalide).
    #[inline]
    #[must_use]
    pub const fn n_bat(&self) -> u8 {
        self.n_bat
    }

    /// Met à jour la topologie. Si invalide → `n_bat = 0` (fail-safe).
    #[allow(clippy::cast_possible_truncation)]
    pub fn set_topology(&mut self, n_ina: u8, n_tca: u8) {
        if topology_ok(n_ina, n_tca) {
            // MAX_BATTERIES = 16, fits u8 trivially
            let max_u8 = MAX_BATTERIES as u8;
            self.n_bat = n_ina.min(max_u8);
        } else {
            self.n_bat = 0;
        }
    }

    /// Retourne une copie du counter pour `idx`, ou counter vide si hors-range.
    #[inline]
    #[must_use]
    #[allow(clippy::indexing_slicing)]
    pub fn counter(&self, idx: u8) -> SwitchCounter {
        let i = idx as usize;
        if i < MAX_BATTERIES {
            self.counters[i]
        } else {
            SwitchCounter::new()
        }
    }

    /// Retourne l'accumulateur `Ah` pour `idx`, ou `ZERO` si hors-range.
    #[inline]
    #[must_use]
    #[allow(clippy::indexing_slicing)]
    pub fn ah(&self, idx: u8) -> MilliampHours {
        let i = idx as usize;
        if i < MAX_BATTERIES {
            self.ah_accumulators[i]
        } else {
            MilliampHours::ZERO
        }
    }

    /// Applique une `Transition` : incrémente le compteur si fault (`Offline` ou `Latch`).
    /// Index hors-range : no-op.
    #[allow(clippy::indexing_slicing)]
    pub fn apply_transition(&mut self, idx: u8, transition: Transition, now_ms: u64) {
        let i = idx as usize;
        if i >= MAX_BATTERIES {
            return;
        }
        match transition {
            Transition::Move(BatteryState::Offline, _) | Transition::Latch(_) => {
                self.counters[i].record_fault(now_ms);
            }
            Transition::Stay(_) | Transition::Move(_, _) => {}
        }
    }

    /// Reset du compteur pour une batterie (via `BLE` `ResetLatch`).
    /// Index hors-range : no-op.
    #[allow(clippy::indexing_slicing)]
    pub fn reset_counter(&mut self, idx: u8) {
        let i = idx as usize;
        if i < MAX_BATTERIES {
            self.counters[i].reset();
        }
    }

    /// Reset `Ah` accumulator (via `BLE` `ResetAh`).
    #[allow(clippy::indexing_slicing)]
    pub fn reset_ah(&mut self, idx: u8) {
        let i = idx as usize;
        if i < MAX_BATTERIES {
            self.ah_accumulators[i] = MilliampHours::ZERO;
            self.ah_residues[i] = 0;
        }
    }

    /// Intègre le courant dans le compteur `Ah` pour une batterie.
    #[allow(clippy::indexing_slicing)]
    pub fn integrate(&mut self, idx: u8, i: Milliamps, dt_ms: u32) {
        let i_idx = idx as usize;
        if i_idx < MAX_BATTERIES {
            self.ah_accumulators[i_idx] = integrate_charge(
                self.ah_accumulators[i_idx],
                i,
                dt_ms,
                &mut self.ah_residues[i_idx],
            );
        }
    }
}

impl Default for BatteryManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::panic, clippy::cast_possible_truncation)]
mod tests {
    use super::*;
    use bmu_types::{LatchReason, OfflineReason};

    #[test]
    fn topology_ok_4x4() {
        assert!(topology_ok(16, 4));
    }

    #[test]
    fn topology_ok_boot_empty() {
        // Cas spécial : aucun device au boot → topology_ok = false
        assert!(!topology_ok(0, 0));
    }

    #[test]
    fn topology_fail_ina_without_tca() {
        assert!(!topology_ok(16, 3));
        assert!(!topology_ok(16, 5));
    }

    #[test]
    fn topology_fail_tca_without_ina() {
        assert!(!topology_ok(15, 4));
        assert!(!topology_ok(17, 4));
    }

    #[test]
    fn topology_partial_3x4() {
        // 3 TCA détectés, 12 INA → topology OK pour 12 batteries
        assert!(topology_ok(12, 3));
    }

    #[test]
    fn coulomb_counter_discharge_accumulates() {
        // 1000 mA discharge pendant 3600 × 1 s = 1000 mAh accumulés côté decharge
        let mut ah = MilliampHours::ZERO;
        let mut residue: i64 = 0;
        for _ in 0..3600 {
            ah = integrate_charge(ah, Milliamps::from_raw(-1000), 1000, &mut residue);
        }
        assert!((ah.as_raw() - (-1000)).abs() <= 5, "got {}", ah.as_raw());
    }

    #[test]
    fn coulomb_counter_charge_accumulates() {
        let mut ah = MilliampHours::ZERO;
        let mut residue: i64 = 0;
        for _ in 0..3600 {
            ah = integrate_charge(ah, Milliamps::from_raw(1000), 1000, &mut residue);
        }
        assert!((ah.as_raw() - 1000).abs() <= 5, "got {}", ah.as_raw());
    }

    #[test]
    fn coulomb_counter_zero_current_no_change() {
        let ah = MilliampHours::from_raw(500);
        let mut residue: i64 = 0;
        let new_ah = integrate_charge(ah, Milliamps::ZERO, 200, &mut residue);
        assert_eq!(new_ah, ah);
    }

    #[test]
    fn coulomb_counter_saturating_positive() {
        let ah = MilliampHours::from_raw(i32::MAX - 5);
        let mut residue: i64 = 0;
        let new_ah = integrate_charge(ah, Milliamps::from_raw(10_000), 3_600_000, &mut residue);
        assert_eq!(new_ah, MilliampHours::from_raw(i32::MAX));
    }

    #[test]
    fn battery_manager_new_empty() {
        let mgr = BatteryManager::new();
        assert_eq!(mgr.n_bat(), 0);
        for i in 0..MAX_BATTERIES {
            assert_eq!(mgr.counter(i as u8).count(), 0);
        }
    }

    #[test]
    fn battery_manager_set_topology_updates_n_bat() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(16, 4);
        assert_eq!(mgr.n_bat(), 16);
    }

    #[test]
    fn battery_manager_set_topology_invalid_zeroes_n_bat() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(15, 4); // mismatch
        assert_eq!(mgr.n_bat(), 0);
    }

    #[test]
    fn battery_manager_apply_transition_move() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        mgr.apply_transition(
            0,
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
            1_000,
        );
        assert_eq!(mgr.counter(0).count(), 1);
        assert_eq!(mgr.counter(0).last_fault_ms(), 1_000);
    }

    #[test]
    fn battery_manager_apply_transition_latch() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        mgr.apply_transition(0, Transition::Latch(LatchReason::MaxSwitchReached), 5_000);
        // Latch also records the fault in the counter
        assert_eq!(mgr.counter(0).count(), 1);
    }

    #[test]
    fn battery_manager_reset_counter() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        mgr.apply_transition(
            0,
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
            1_000,
        );
        mgr.reset_counter(0);
        assert_eq!(mgr.counter(0).count(), 0);
    }

    #[test]
    fn battery_manager_out_of_range_noop() {
        let mut mgr = BatteryManager::new();
        mgr.set_topology(4, 1);
        // Index 20 out of range, no panic
        mgr.apply_transition(
            20,
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
            1_000,
        );
        mgr.reset_counter(20);
    }
}
