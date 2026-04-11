//! Checks purs F03-F06. Chaque fonction est sans état, testable isolément.
//! Fonctions `check_uv`, `check_ov`, `check_oc`, `check_imbalance` — toutes
//! pures sur `bmu_types::Millivolts` / `Milliamps` / `Config`.

use bmu_types::{Config, Milliamps, Millivolts};

/// F03 : under-voltage. True si la batterie est en fault.
/// Compare strict : `v < cfg.umin`.
#[inline]
#[must_use]
pub fn check_uv(v: Millivolts, cfg: &Config) -> bool {
    v < cfg.umin
}

/// F04 : over-voltage. True si la batterie est en fault.
/// Compare strict : `v > cfg.umax`.
#[inline]
#[must_use]
pub fn check_ov(v: Millivolts, cfg: &Config) -> bool {
    v > cfg.umax
}

/// F05 : over-current. Compare `|i|` à `cfg.imax`. True si en fault.
/// `imax` est toujours positif (validé par `Config::validate`).
#[inline]
#[must_use]
pub fn check_oc(i: Milliamps, cfg: &Config) -> bool {
    // Note : `Milliamps::abs()` retourne déjà un u32 (unsigned_abs)
    i.abs() > cfg.imax.as_raw().unsigned_abs()
}

/// F06 : imbalance vs fleet max. True si en fault.
///
/// **Fix CRIT-B** : `fleet_max` est fourni par l'appelant, qui DOIT
/// utiliser `Snapshot::fleet_max_voltage()` calculé sur les batteries
/// `Online` uniquement (jamais un max local stale).
#[inline]
#[must_use]
pub fn check_imbalance(v: Millivolts, fleet_max: Millivolts, cfg: &Config) -> bool {
    if fleet_max <= v {
        // Cette batterie est le max fleet ou au-dessus : pas d'imbalance
        return false;
    }
    let diff_mv = fleet_max.abs_diff(v);
    let threshold_mv = cfg.vdiff_imbalance.as_raw().unsigned_abs();
    diff_mv > threshold_mv
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_uv_below_threshold_fails() {
        let cfg = Config::default();
        assert!(check_uv(Millivolts::from_raw(23_999), &cfg));
    }

    #[test]
    fn check_uv_at_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_uv(Millivolts::from_raw(24_000), &cfg));
    }

    #[test]
    fn check_uv_above_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_uv(Millivolts::from_raw(25_000), &cfg));
    }

    /// **Régression CRIT-A** (mV vs V confusion).
    /// Bug historique : spec v1 confondait `24` (volts) avec `24000`
    /// (millivolts). Le type `Millivolts` empêche la confusion sémantique ;
    /// ce test documente que `from_raw(24)` = 24 mV → undervoltage,
    /// et `from_raw(24_000)` = 24 V → nominal.
    #[test]
    fn test_crit_a_mv_volt_confusion_impossible() {
        let cfg = Config::default();
        assert!(check_uv(Millivolts::from_raw(24), &cfg));
        assert!(!check_uv(Millivolts::from_raw(24_000), &cfg));
    }

    #[test]
    fn check_ov_above_threshold_fails() {
        let cfg = Config::default();
        assert!(check_ov(Millivolts::from_raw(30_001), &cfg));
    }

    #[test]
    fn check_ov_at_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_ov(Millivolts::from_raw(30_000), &cfg));
    }

    #[test]
    fn check_oc_positive_above_threshold_fails() {
        let cfg = Config::default();
        assert!(check_oc(Milliamps::from_raw(1_001), &cfg));
    }

    #[test]
    fn check_oc_negative_above_threshold_fails() {
        let cfg = Config::default();
        assert!(check_oc(Milliamps::from_raw(-1_001), &cfg));
    }

    #[test]
    fn check_oc_zero_passes() {
        let cfg = Config::default();
        assert!(!check_oc(Milliamps::from_raw(0), &cfg));
    }

    #[test]
    fn check_oc_at_threshold_passes() {
        let cfg = Config::default();
        assert!(!check_oc(Milliamps::from_raw(1_000), &cfg));
        assert!(!check_oc(Milliamps::from_raw(-1_000), &cfg));
    }

    #[test]
    fn check_imbalance_within_threshold() {
        let cfg = Config::default();
        // fleet_max = 27000, v = 26200 → diff = 800 < 1000 → OK
        assert!(!check_imbalance(
            Millivolts::from_raw(26_200),
            Millivolts::from_raw(27_000),
            &cfg,
        ));
    }

    #[test]
    fn check_imbalance_above_threshold_fails() {
        let cfg = Config::default();
        // fleet_max = 27000, v = 25500 → diff = 1500 > 1000 → FAIL
        assert!(check_imbalance(
            Millivolts::from_raw(25_500),
            Millivolts::from_raw(27_000),
            &cfg,
        ));
    }

    #[test]
    fn check_imbalance_at_threshold_passes() {
        let cfg = Config::default();
        // diff = 1000 exact → pass (borne inclusive)
        assert!(!check_imbalance(
            Millivolts::from_raw(26_000),
            Millivolts::from_raw(27_000),
            &cfg,
        ));
    }

    /// **Régression CRIT-B** (imbalance basé sur fleet max vivant, pas stale).
    /// Bug historique : `bmu_protection` v1 calculait un "max local glissant"
    /// qui pouvait rester bloqué sur la valeur d'une batterie déjà offline,
    /// masquant un imbalance réel. Ici on garantit que `fleet_max` est fourni
    /// par l'appelant, typiquement `Snapshot::fleet_max_voltage()` qui EXCLUT
    /// les batteries offline.
    #[test]
    fn test_crit_b_imbalance_uses_fleet_max_not_local() {
        let cfg = Config::default();
        let fleet_max = Millivolts::from_raw(27_000);
        assert!(
            check_imbalance(Millivolts::from_raw(25_500), fleet_max, &cfg),
            "fleet_max=27000 et bat=25500 doit déclencher imbalance"
        );
        // Scénario stale fleet_max : si v1 gardait 25500 comme fleet_max,
        // diff serait 0 → PASS (imbalance masquée — bug documenté).
        let stale_fleet_max = Millivolts::from_raw(25_500);
        assert!(
            !check_imbalance(Millivolts::from_raw(25_500), stale_fleet_max, &cfg),
            "Stale fleet_max 25500 masque l'imbalance — bug v1 documenté"
        );
    }
}
