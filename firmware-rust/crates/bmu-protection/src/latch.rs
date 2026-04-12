//! F07 reconnect delay + F08 permanent latch après 5e fault.

use bmu_types::{Config, LatchReason, OfflineReason};

/// Compteur de switchs per-battery. Persistant tant que la batterie n'est pas
/// reset via `BLE` `ResetLatch`. **Ne décroît jamais** (propriété du type :
/// seuls `record_fault` et `reset` exposés).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct SwitchCounter {
    count: u8,
    last_fault_ms: u64,
}

impl SwitchCounter {
    /// Crée un compteur vide.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            count: 0,
            last_fault_ms: 0,
        }
    }

    /// Nombre de switchs enregistrés (saturé à `u8::MAX`).
    #[inline]
    #[must_use]
    pub const fn count(&self) -> u8 {
        self.count
    }

    /// Timestamp (ms) du dernier fault enregistré. 0 si aucun.
    #[inline]
    #[must_use]
    pub const fn last_fault_ms(&self) -> u64 {
        self.last_fault_ms
    }

    /// Enregistre un nouveau fault. Incrémente le compteur (saturating à 255)
    /// et note le timestamp. Le compteur ne décroît JAMAIS.
    pub fn record_fault(&mut self, now_ms: u64) {
        self.count = self.count.saturating_add(1);
        self.last_fault_ms = now_ms;
    }

    /// Réinitialise le compteur. Appelé uniquement par `ResetLatch` `BLE` command
    /// avec audit log `SD`.
    pub fn reset(&mut self) {
        self.count = 0;
        self.last_fault_ms = 0;
    }

    /// True si le compteur a atteint ou dépassé `nb_switch_max` → latch permanent `F08`.
    #[inline]
    #[must_use]
    pub fn should_latch_permanent(&self, cfg: &Config) -> bool {
        self.count >= cfg.nb_switch_max
    }

    /// True si le délai de reconnect est écoulé depuis le dernier fault.
    /// Comparaison stricte `>` : un elapsed exactement égal au threshold
    /// n'est PAS encore considéré écoulé.
    #[inline]
    #[must_use]
    pub fn reconnect_delay_elapsed(&self, now_ms: u64, cfg: &Config) -> bool {
        let elapsed = now_ms.saturating_sub(self.last_fault_ms);
        elapsed > u64::from(cfg.reconnect_delay_ms)
    }
}

/// Trait helper pour mapper `OfflineReason` → `LatchReason` lors du latch `F08`.
pub trait OfflineReasonExt {
    /// Retourne le `LatchReason` correspondant à cet `OfflineReason`.
    #[must_use]
    fn as_latch_reason(&self) -> LatchReason;
}

impl OfflineReasonExt for OfflineReason {
    fn as_latch_reason(&self) -> LatchReason {
        // Match exhaustif (pas de catch-all `_`) : tout nouveau variant de
        // `OfflineReason` provoquera une erreur de compilation, forçant un
        // mapping explicite — indispensable en safety-critical.
        //
        // `Ok` et les fault variants produisent la même valeur mais sont
        // listés séparément pour documenter l'intention ; `clippy::match_same_arms`
        // est explicitement toléré ici.
        #[allow(clippy::match_same_arms)]
        match self {
            Self::Topology => LatchReason::TopologyFailSafe,
            Self::Manual => LatchReason::ManualForceOff,
            Self::UnderVoltage | Self::OverVoltage | Self::OverCurrent | Self::Imbalance => {
                LatchReason::MaxSwitchReached
            }
            // `Ok` ne devrait jamais passer par `as_latch_reason` (pas de
            // fault à latcher). Mappé à `MaxSwitchReached` par défaut pour
            // éviter un panic, mais c'est un usage incorrect à flagger en
            // Task 6.3 si détecté dynamiquement.
            Self::Ok => LatchReason::MaxSwitchReached,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn switch_counter_new_zero() {
        let c = SwitchCounter::new();
        assert_eq!(c.count(), 0);
        assert_eq!(c.last_fault_ms(), 0);
    }

    #[test]
    fn switch_counter_record_fault_increments() {
        let mut c = SwitchCounter::new();
        c.record_fault(1_000);
        assert_eq!(c.count(), 1);
        assert_eq!(c.last_fault_ms(), 1_000);

        c.record_fault(2_000);
        assert_eq!(c.count(), 2);
        assert_eq!(c.last_fault_ms(), 2_000);
    }

    #[test]
    fn switch_counter_monotonic() {
        // F07: le compteur ne décroît jamais (propriété du type)
        let mut c = SwitchCounter::new();
        c.record_fault(1_000);
        c.record_fault(2_000);
        c.record_fault(3_000);
        assert_eq!(c.count(), 3);
        // Pas de méthode decrement exposée — seulement reset()
    }

    #[test]
    fn switch_counter_reset_to_zero() {
        let mut c = SwitchCounter::new();
        c.record_fault(1_000);
        c.record_fault(2_000);
        c.reset();
        assert_eq!(c.count(), 0);
        assert_eq!(c.last_fault_ms(), 0);
    }

    #[test]
    fn should_latch_permanent_below_max_no() {
        let cfg = Config::default(); // nb_switch_max = 5
        let mut c = SwitchCounter::new();
        for i in 1..=4 {
            c.record_fault(i * 1_000);
        }
        assert!(!c.should_latch_permanent(&cfg));
    }

    #[test]
    fn should_latch_permanent_at_max_yes() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        for i in 1..=5 {
            c.record_fault(i * 1_000);
        }
        assert!(c.should_latch_permanent(&cfg));
    }

    #[test]
    fn should_latch_permanent_above_max_yes() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        for i in 1..=10 {
            c.record_fault(i * 1_000);
        }
        assert!(c.should_latch_permanent(&cfg));
    }

    #[test]
    fn reconnect_delay_elapsed_before_delay() {
        let cfg = Config::default(); // reconnect_delay_ms = 10000
        let mut c = SwitchCounter::new();
        c.record_fault(5_000);
        // now = 10_000, elapsed = 5_000 < 10_000 → NO
        assert!(!c.reconnect_delay_elapsed(10_000, &cfg));
    }

    #[test]
    fn reconnect_delay_elapsed_after_delay() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        c.record_fault(5_000);
        // now = 15_001, elapsed = 10_001 > 10_000 → YES
        assert!(c.reconnect_delay_elapsed(15_001, &cfg));
    }

    #[test]
    fn reconnect_delay_elapsed_exact_no() {
        let cfg = Config::default();
        let mut c = SwitchCounter::new();
        c.record_fault(5_000);
        // now = 15_000, elapsed = 10_000 = threshold → PAS elapsed (strict >)
        assert!(!c.reconnect_delay_elapsed(15_000, &cfg));
    }

    #[test]
    fn offline_reason_to_latch_reason_mapping() {
        assert_eq!(
            OfflineReason::UnderVoltage.as_latch_reason(),
            LatchReason::MaxSwitchReached
        );
        assert_eq!(
            OfflineReason::Topology.as_latch_reason(),
            LatchReason::TopologyFailSafe
        );
        assert_eq!(
            OfflineReason::Manual.as_latch_reason(),
            LatchReason::ManualForceOff
        );
    }
}
