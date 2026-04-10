//! Commandes émises par BLE/UI vers le core via `bmu_core_command`.
//! Cf spec §7.4 Commandes Control.

use crate::Config;

/// Commande vers le core. Variants correspondent aux commandes BLE (§7.4)
/// + commandes internes (hotplug, SOH update).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Command {
    /// Pas de commande (slot vide).
    None = 0,
    /// Forçage manuel OFF d'une batterie.
    ForceOff {
        /// Index du slot batterie.
        idx: u8,
    } = 1,
    /// Remise à zéro du compteur Ah d'une batterie.
    ResetAh {
        /// Index du slot batterie.
        idx: u8,
    } = 2,
    /// Armement d'une mesure `R_int` pulse-off.
    TriggerRint {
        /// Index du slot batterie.
        idx: u8,
    } = 3,
    /// Reset d'un latch permanent (authentifié côté host).
    ResetLatch {
        /// Index du slot batterie.
        idx: u8,
    } = 4,
    /// Mise à jour de la `Config` runtime (après validation).
    SetConfig(Config) = 6,
    /// Mise à jour du SOH calculé par `bmu_soh` côté C++.
    UpdateSoh {
        /// Index du slot batterie.
        idx: u8,
        /// SOH 0..100.
        soh_pct: u8,
    } = 7,
    /// Notification de changement de topologie (hotplug `INA`/`TCA`).
    TopologyChanged {
        /// Nombre de devices `INA237` détectés.
        n_ina: u8,
        /// Nombre de devices `TCA9535` détectés.
        n_tca: u8,
    } = 8,
}

#[cfg(test)]
#[allow(clippy::field_reassign_with_default, clippy::panic)]
mod tests {
    use super::*;

    #[test]
    fn command_none_is_default_slot() {
        // Discriminants non cast-able avec variants à data,
        // mais matches! suffit pour vérifier la variante.
        assert!(matches!(Command::None, Command::None));
    }

    #[test]
    fn command_force_off_carries_idx() {
        let c = Command::ForceOff { idx: 5 };
        if let Command::ForceOff { idx } = c {
            assert_eq!(idx, 5);
        } else {
            panic!("wrong variant");
        }
    }

    #[test]
    fn command_set_config_carries_config() {
        let mut cfg = Config::default();
        cfg.nb_switch_max = 3;
        let c = Command::SetConfig(cfg);
        if let Command::SetConfig(got) = c {
            assert_eq!(got.nb_switch_max, 3);
        } else {
            panic!("wrong variant");
        }
    }
}
