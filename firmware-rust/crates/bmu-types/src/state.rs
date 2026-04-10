//! Enum d'états batterie, raisons offline/latch. Cf spec §5.1.

/// État courant d'une batterie dans la state machine protection.
/// Discriminants `u8` figés pour l'ABI C (cf `bmu-core::ffi_types`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
#[repr(u8)]
pub enum BatteryState {
    /// État initial avant toute lecture.
    #[default]
    Unknown = 0,
    /// Batterie détectée absente (topology mismatch ou slot vide).
    Absent = 1,
    /// Batterie en ligne, relais fermé.
    Online = 2,
    /// Batterie temporairement offline (fault non-latché).
    Offline = 3,
    /// Latch permanent (F08 `Kill_LIFE`).
    Latched = 4,
}

impl BatteryState {
    /// Un état terminal ne transitionne plus automatiquement.
    #[inline]
    #[must_use]
    pub const fn is_terminal(self) -> bool {
        matches!(self, Self::Latched | Self::Absent)
    }

    /// True si la batterie peut être physiquement commutée ON par le core.
    /// `Unknown` inclus : permet la transition initiale vers `Online`
    /// au premier check OK.
    #[inline]
    #[must_use]
    pub const fn allows_switch_on(self) -> bool {
        matches!(self, Self::Online | Self::Unknown)
    }
}

/// Raison d'une transition en `Offline` ou `Latched`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum OfflineReason {
    /// Aucune faute.
    Ok = 0,
    /// Sous-tension (F01).
    UnderVoltage = 1,
    /// Sur-tension (F02).
    OverVoltage = 2,
    /// Sur-courant (F03).
    OverCurrent = 3,
    /// Déséquilibre de tension (F04).
    Imbalance = 4,
    /// Topologie invalide (F05).
    Topology = 5,
    /// Commande manuelle (web / BLE).
    Manual = 6,
}

/// Raison d'un latch permanent (F08 `Kill_LIFE`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum LatchReason {
    /// Nombre max de switchs atteint (F07).
    MaxSwitchReached = 1,
    /// Topology fail-safe (F05 permanent).
    TopologyFailSafe = 2,
    /// Forçage manuel permanent.
    ManualForceOff = 3,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn battery_state_unknown_default() {
        assert_eq!(BatteryState::default(), BatteryState::Unknown);
    }

    #[test]
    fn battery_state_discriminants_stable() {
        assert_eq!(BatteryState::Unknown as u8, 0);
        assert_eq!(BatteryState::Absent as u8, 1);
        assert_eq!(BatteryState::Online as u8, 2);
        assert_eq!(BatteryState::Offline as u8, 3);
        assert_eq!(BatteryState::Latched as u8, 4);
    }

    #[test]
    fn offline_reason_discriminants_stable() {
        assert_eq!(OfflineReason::Ok as u8, 0);
        assert_eq!(OfflineReason::UnderVoltage as u8, 1);
        assert_eq!(OfflineReason::OverVoltage as u8, 2);
        assert_eq!(OfflineReason::OverCurrent as u8, 3);
        assert_eq!(OfflineReason::Imbalance as u8, 4);
        assert_eq!(OfflineReason::Topology as u8, 5);
        assert_eq!(OfflineReason::Manual as u8, 6);
    }

    #[test]
    fn battery_state_is_terminal() {
        assert!(!BatteryState::Unknown.is_terminal());
        assert!(!BatteryState::Online.is_terminal());
        assert!(!BatteryState::Offline.is_terminal());
        assert!(BatteryState::Latched.is_terminal());
        assert!(BatteryState::Absent.is_terminal());
    }

    #[test]
    fn battery_state_allows_switch_on() {
        assert!(BatteryState::Online.allows_switch_on());
        assert!(BatteryState::Unknown.allows_switch_on());
        assert!(!BatteryState::Offline.allows_switch_on());
        assert!(!BatteryState::Latched.allows_switch_on());
        assert!(!BatteryState::Absent.allows_switch_on());
    }
}
