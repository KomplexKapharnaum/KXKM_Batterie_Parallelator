//! Config runtime validée. Cf spec §5.4.

use crate::{Milliamps, Millivolts};

/// Erreurs de validation de config. Jamais de texte runtime (`no_std` no alloc).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum ConfigError {
    /// `umin` < 18000 mV (< 3V/cell × 6 cells Li-ion).
    UminTooLow = 1,
    /// `umax` > 36000 mV (> 3.6V/cell × 10 cells).
    UmaxTooHigh = 2,
    /// `umin` >= `umax` (intervalle vide).
    UminGteUmax = 3,
    /// `imax` > 10 A (hors spec hardware).
    ImaxTooHigh = 4,
    /// `imax` <= 0 (protection désactivée).
    ImaxNonPositive = 5,
    /// `vdiff_imbalance` > 5000 mV (seuil imbalance aberrant).
    VdiffTooHigh = 6,
    /// `nb_switch_max` 0 ou > 20.
    NbSwitchOutOfRange = 7,
    /// `reconnect_delay_ms` < 1 s ou > 10 min.
    ReconnectDelayOutOfRange = 8,
    /// `tick_period_ms` 0 ou > 1 s.
    TickPeriodOutOfRange = 9,
}

/// Config runtime du core BMU. Valeurs défaut = spec `Kill_LIFE`
/// `01_spec.md` F01-F08.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Config {
    /// Seuil de sous-tension (default 24000 mV).
    pub umin: Millivolts,
    /// Seuil de sur-tension (default 30000 mV).
    pub umax: Millivolts,
    /// Seuil de sur-courant absolu (default 1000 mA).
    pub imax: Milliamps,
    /// Seuil d'imbalance de tension (default 1000 mV).
    pub vdiff_imbalance: Millivolts,
    /// Nombre max de switchs avant latch permanent (default 5).
    pub nb_switch_max: u8,
    /// Délai de reconnect après fault (default 10000 ms).
    pub reconnect_delay_ms: u32,
    /// Période de tick de la boucle protection (default 200 ms = 5 Hz).
    pub tick_period_ms: u32,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            umin: Millivolts::from_raw(24_000),
            umax: Millivolts::from_raw(30_000),
            imax: Milliamps::from_raw(1_000),
            vdiff_imbalance: Millivolts::from_raw(1_000),
            nb_switch_max: 5,
            reconnect_delay_ms: 10_000,
            tick_period_ms: 200,
        }
    }
}

impl Config {
    /// Valide les bornes de sécurité. Appelée à chaque `bmu_core_set_config`
    /// et au boot. Rejette toute config dangereuse.
    ///
    /// # Errors
    ///
    /// Retourne un `ConfigError` discriminant la borne violée.
    pub const fn validate(&self) -> Result<(), ConfigError> {
        if self.umin.as_raw() < 18_000 {
            return Err(ConfigError::UminTooLow);
        }
        if self.umax.as_raw() > 36_000 {
            return Err(ConfigError::UmaxTooHigh);
        }
        if self.umin.as_raw() >= self.umax.as_raw() {
            return Err(ConfigError::UminGteUmax);
        }
        if self.imax.as_raw() <= 0 {
            return Err(ConfigError::ImaxNonPositive);
        }
        if self.imax.as_raw() > 10_000 {
            return Err(ConfigError::ImaxTooHigh);
        }
        if self.vdiff_imbalance.as_raw() > 5_000 {
            return Err(ConfigError::VdiffTooHigh);
        }
        if self.nb_switch_max == 0 || self.nb_switch_max > 20 {
            return Err(ConfigError::NbSwitchOutOfRange);
        }
        if self.reconnect_delay_ms < 1_000 || self.reconnect_delay_ms > 600_000 {
            return Err(ConfigError::ReconnectDelayOutOfRange);
        }
        if self.tick_period_ms == 0 || self.tick_period_ms > 1_000 {
            return Err(ConfigError::TickPeriodOutOfRange);
        }
        Ok(())
    }
}

#[cfg(test)]
#[allow(clippy::field_reassign_with_default)]
mod tests {
    use super::*;

    #[test]
    fn config_default_matches_kill_life_spec() {
        let c = Config::default();
        assert_eq!(c.umin, Millivolts::from_raw(24000));
        assert_eq!(c.umax, Millivolts::from_raw(30000));
        assert_eq!(c.imax, Milliamps::from_raw(1000));
        assert_eq!(c.vdiff_imbalance, Millivolts::from_raw(1000));
        assert_eq!(c.nb_switch_max, 5);
        assert_eq!(c.reconnect_delay_ms, 10_000);
        assert_eq!(c.tick_period_ms, 200);
    }

    #[test]
    fn config_default_validates() {
        assert!(Config::default().validate().is_ok());
    }

    #[test]
    fn config_rejects_umin_below_18v() {
        let mut c = Config::default();
        c.umin = Millivolts::from_raw(17999);
        assert_eq!(c.validate(), Err(ConfigError::UminTooLow));
    }

    #[test]
    fn config_rejects_umax_above_36v() {
        let mut c = Config::default();
        c.umax = Millivolts::from_raw(36001);
        assert_eq!(c.validate(), Err(ConfigError::UmaxTooHigh));
    }

    #[test]
    fn config_rejects_umin_gte_umax() {
        let mut c = Config::default();
        c.umin = Millivolts::from_raw(30000);
        c.umax = Millivolts::from_raw(30000);
        assert_eq!(c.validate(), Err(ConfigError::UminGteUmax));
    }

    #[test]
    fn config_rejects_imax_over_10a() {
        let mut c = Config::default();
        c.imax = Milliamps::from_raw(10_001);
        assert_eq!(c.validate(), Err(ConfigError::ImaxTooHigh));
    }

    #[test]
    fn config_rejects_imax_zero_or_negative() {
        let mut c = Config::default();
        c.imax = Milliamps::from_raw(0);
        assert_eq!(c.validate(), Err(ConfigError::ImaxNonPositive));
    }

    #[test]
    fn config_rejects_vdiff_over_5v() {
        let mut c = Config::default();
        c.vdiff_imbalance = Millivolts::from_raw(5001);
        assert_eq!(c.validate(), Err(ConfigError::VdiffTooHigh));
    }

    #[test]
    fn config_rejects_nb_switch_zero() {
        let mut c = Config::default();
        c.nb_switch_max = 0;
        assert_eq!(c.validate(), Err(ConfigError::NbSwitchOutOfRange));
    }

    #[test]
    fn config_rejects_nb_switch_over_20() {
        let mut c = Config::default();
        c.nb_switch_max = 21;
        assert_eq!(c.validate(), Err(ConfigError::NbSwitchOutOfRange));
    }

    #[test]
    fn config_rejects_reconnect_delay_below_1s() {
        let mut c = Config::default();
        c.reconnect_delay_ms = 999;
        assert_eq!(c.validate(), Err(ConfigError::ReconnectDelayOutOfRange));
    }

    #[test]
    fn config_rejects_reconnect_delay_over_10min() {
        let mut c = Config::default();
        c.reconnect_delay_ms = 600_001;
        assert_eq!(c.validate(), Err(ConfigError::ReconnectDelayOutOfRange));
    }

    #[test]
    fn config_rejects_tick_period_zero() {
        let mut c = Config::default();
        c.tick_period_ms = 0;
        assert_eq!(c.validate(), Err(ConfigError::TickPeriodOutOfRange));
    }

    #[test]
    fn config_rejects_tick_period_over_1s() {
        let mut c = Config::default();
        c.tick_period_ms = 1001;
        assert_eq!(c.validate(), Err(ConfigError::TickPeriodOutOfRange));
    }
}
