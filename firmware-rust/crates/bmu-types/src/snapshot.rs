//! Immutable snapshots émis par le core à chaque tick.
//! Cf spec §3.4 `BmuSnapshotC` et §7.2 layout battery characteristic.

use crate::{BatteryState, MilliampHours, Milliamps, Milliohms, Millivolts, OfflineReason};

/// Nombre maximum de batteries supportées par le core (slots `INA237`).
pub const MAX_BATTERIES: usize = 16;

/// Une batterie à l'instant T.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Battery {
    /// Index du slot batterie (0..`MAX_BATTERIES`).
    pub idx: u8,
    /// État courant dans la state machine protection.
    pub state: BatteryState,
    /// Raison de la dernière transition `Offline`/`Latched`.
    pub reason: OfflineReason,
    /// Compteur de switchs (incrémenté à chaque transition physique ON).
    pub switch_count: u8,
    /// Tension mesurée aux bornes du shunt `INA237`.
    pub voltage: Millivolts,
    /// Courant signé (positif = discharge).
    pub current: Milliamps,
    /// Charge restante estimée (Ah counting).
    pub ah_remaining: MilliampHours,
    /// Température en dixièmes de degré Celsius.
    pub temp_c10: i16,
    /// SOH 0..100, `0xFF` = unknown (SOH model pas encore appliqué).
    pub soh_pct: u8,
    /// Duty cycle balancer 0..100.
    pub balancer_duty_pct: u8,
    /// Résistance ohmique (`Milliohms::UNKNOWN` = non mesurée).
    pub r_ohmic: Milliohms,
}

impl Default for Battery {
    fn default() -> Self {
        Self {
            idx: 0,
            state: BatteryState::Unknown,
            reason: OfflineReason::Ok,
            switch_count: 0,
            voltage: Millivolts::ZERO,
            current: Milliamps::ZERO,
            ah_remaining: MilliampHours::ZERO,
            temp_c10: 0,
            soh_pct: 0xFF,
            balancer_duty_pct: 0,
            r_ohmic: Milliohms::UNKNOWN,
        }
    }
}

/// Métadonnées système (agrégats non-battery).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct System {
    /// Topologie valide (`Nb_TCA × 4 == Nb_INA`).
    pub topology_ok: bool,
    /// Nombre de batteries détectées (≤ `MAX_BATTERIES`).
    pub n_bat: u8,
    /// Latence tick p50 en µs.
    pub tick_us_p50: u32,
    /// Latence tick p99 en µs.
    pub tick_us_p99: u32,
    /// Nombre de watchdog feeds depuis le boot.
    pub wdt_feeds: u32,
    /// Horloge monotonique en µs depuis le boot.
    pub monotonic_us: u64,
}

/// Snapshot immuable émis à chaque tick par le core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Snapshot {
    /// Nombre de batteries valides dans `batteries`.
    pub n_bat: u8,
    /// Tableau inline de `MAX_BATTERIES` slots.
    pub batteries: [Battery; MAX_BATTERIES],
    /// Métadonnées système.
    pub system: System,
}

impl Default for Snapshot {
    fn default() -> Self {
        let mut batteries = [Battery::default(); MAX_BATTERIES];
        for (i, b) in batteries.iter_mut().enumerate() {
            // i est borné par MAX_BATTERIES = 16, donc fit toujours dans u8.
            #[allow(clippy::cast_possible_truncation)]
            {
                b.idx = i as u8;
            }
        }
        Self {
            n_bat: 0,
            batteries,
            system: System::default(),
        }
    }
}

impl Snapshot {
    /// Max tension parmi les batteries `Online`. Retourne `ZERO` si aucune
    /// n'est `Online`.
    ///
    /// **CRIT-B fix**: tous les checks imbalance utilisent cette fonction,
    /// JAMAIS un max local glissant. Cf spec §5.3.
    #[must_use]
    pub fn fleet_max_voltage(&self) -> Millivolts {
        let mut max = Millivolts::ZERO;
        let n = (self.n_bat as usize).min(MAX_BATTERIES);
        for b in self.batteries.iter().take(n) {
            if b.state == BatteryState::Online && b.voltage > max {
                max = b.voltage;
            }
        }
        max
    }

    /// Somme des courants des batteries `Online` (agrégat fleet).
    #[must_use]
    pub fn fleet_sum_current(&self) -> Milliamps {
        let mut sum: i32 = 0;
        let n = (self.n_bat as usize).min(MAX_BATTERIES);
        for b in self.batteries.iter().take(n) {
            if b.state == BatteryState::Online {
                sum = sum.saturating_add(b.current.as_raw());
            }
        }
        Milliamps::from_raw(sum)
    }
}

#[cfg(test)]
#[allow(clippy::field_reassign_with_default)]
mod tests {
    use super::*;

    #[test]
    fn snapshot_default_is_empty() {
        let snap = Snapshot::default();
        assert_eq!(snap.n_bat, 0);
        assert!(!snap.system.topology_ok);
        for bat in &snap.batteries {
            assert_eq!(bat.state, BatteryState::Unknown);
            assert_eq!(bat.voltage, Millivolts::ZERO);
        }
    }

    #[test]
    fn snapshot_max_size() {
        assert_eq!(MAX_BATTERIES, 16);
        let snap = Snapshot::default();
        assert_eq!(snap.batteries.len(), MAX_BATTERIES);
    }

    #[test]
    fn battery_default_unknown() {
        let b = Battery::default();
        assert_eq!(b.idx, 0);
        assert_eq!(b.state, BatteryState::Unknown);
        assert_eq!(b.reason, OfflineReason::Ok);
        assert_eq!(b.switch_count, 0);
        assert_eq!(b.soh_pct, 0xFF);
        assert_eq!(b.r_ohmic, Milliohms::UNKNOWN);
    }

    #[test]
    fn snapshot_fleet_max_empty_returns_zero() {
        let snap = Snapshot::default();
        assert_eq!(snap.fleet_max_voltage(), Millivolts::ZERO);
    }

    #[test]
    fn snapshot_fleet_max_online_only() {
        let mut snap = Snapshot::default();
        snap.n_bat = 3;
        snap.batteries[0].state = BatteryState::Online;
        snap.batteries[0].voltage = Millivolts::from_raw(25000);
        snap.batteries[1].state = BatteryState::Online;
        snap.batteries[1].voltage = Millivolts::from_raw(27500);
        snap.batteries[2].state = BatteryState::Offline;
        snap.batteries[2].voltage = Millivolts::from_raw(30000);
        assert_eq!(snap.fleet_max_voltage(), Millivolts::from_raw(27500));
    }

    #[test]
    fn snapshot_fleet_sum_current_all_states() {
        let mut snap = Snapshot::default();
        snap.n_bat = 3;
        snap.batteries[0].state = BatteryState::Online;
        snap.batteries[0].current = Milliamps::from_raw(-500);
        snap.batteries[1].state = BatteryState::Online;
        snap.batteries[1].current = Milliamps::from_raw(-750);
        snap.batteries[2].state = BatteryState::Offline;
        snap.batteries[2].current = Milliamps::from_raw(-100);
        assert_eq!(snap.fleet_sum_current(), Milliamps::from_raw(-1250));
    }
}
