//! Mesure résistance interne par pulse-off. Cf spec §6.1.
//!
//! Micro state machine : l'appelant (`protection_task` côté C) invoque
//! `tick()` à chaque cycle 5 Hz, avec le `Snapshot` courant et le numéro de
//! tick. La state machine produit une `RintAction` indiquant d'ouvrir/fermer
//! un contacteur, jusqu'à ce que la mesure soit collectée.
//!
//! L'engine est un singleton stocké dans `bmu-core`. Les résultats sont
//! cachés par batterie dans `results[MAX_BATTERIES]` et exposés via
//! `RintEngine::result(idx)`.

#![no_std]

use bmu_types::{BatteryState, Milliamps, Milliohms, Millivolts, Snapshot, MAX_BATTERIES};

/// Durée de la phase pulse en ticks 5 Hz (3 ticks = 600 ms).
pub const PULSE_DURATION_TICKS: u32 = 3;

/// Durée du cooldown après mesure en ticks 5 Hz (50 ticks = 10 s).
pub const COOLDOWN_TICKS: u32 = 50;

/// Courant minimum absolu pour considérer une mesure valide (50 `mA`).
pub const MIN_CURRENT_FOR_MEASUREMENT_MA: u32 = 50;

/// Résultat d'une mesure `R_int` pour une batterie.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RintResult {
    pub idx: u8,
    pub r_ohmic: Milliohms,
    pub v_before: Millivolts,
    pub v_during: Millivolts,
    pub i_before: Milliamps,
    pub measured_at_tick: u32,
}

/// État courant de la state machine `R_int` (singleton pour toute la fleet).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum RintState {
    /// Pas de mesure en cours.
    #[default]
    Idle,
    /// Une requête a été acceptée, pulse non encore démarré.
    ArmRequested { idx: u8 },
    /// Pulse en cours, on attend `PULSE_DURATION_TICKS` avant de mesurer.
    PulseStarted {
        idx: u8,
        v_before: Millivolts,
        i_before: Milliamps,
        start_tick: u32,
    },
    /// Mesure terminée, on attend `COOLDOWN_TICKS` avant d'accepter une nouvelle requête.
    Cooldown { idx: u8, deadline_tick: u32 },
}

/// Action produite par `RintEngine::tick`, à merger dans `Actions` par `bmu-core`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RintAction {
    None,
    OpenContact(u8),
    CloseContact(u8),
}

/// Erreurs renvoyées par `RintEngine::request`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RintError {
    /// Une mesure est déjà en cours.
    Busy,
    /// Index de batterie hors borne.
    InvalidIndex,
    /// La batterie cible n'est pas `Online`.
    BatteryNotOnline,
    /// Une autre batterie est `Offline` ou `Latched` — mesure interdite pour safety.
    FleetUnsettled,
    /// Courant absolu en dessous de `MIN_CURRENT_FOR_MEASUREMENT_MA`.
    CurrentTooLow,
}

/// Moteur singleton. Stocke l'état courant + résultats cachés par batterie.
///
/// NOTE : pas `Copy` — state machine stateful. Une copie accidentelle
/// créerait une divergence silencieuse (une instance avance, l'autre reste
/// figée en `PulseStarted` avec le contacteur "ouvert" côté caller).
/// Passer par `&mut RintEngine` uniquement.
#[derive(Debug, Clone)]
pub struct RintEngine {
    state: RintState,
    results: [Option<RintResult>; MAX_BATTERIES],
    /// Timestamp de la dernière mesure par batterie. Réservé pour Phase 8+ :
    /// rejeter une requête si la dernière mesure date de moins de N ticks
    /// (throttling per-battery). Écrit à chaque `tick()` réussi, non encore lu.
    #[allow(dead_code)]
    last_measure_tick: [Option<u32>; MAX_BATTERIES],
}

impl Default for RintEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl RintEngine {
    /// Crée un moteur vide en état `Idle`.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            state: RintState::Idle,
            results: [None; MAX_BATTERIES],
            last_measure_tick: [None; MAX_BATTERIES],
        }
    }

    /// État courant.
    #[must_use]
    pub const fn state(&self) -> RintState {
        self.state
    }

    /// Retourne le dernier `RintResult` mesuré pour `idx`, ou `None`.
    #[must_use]
    #[allow(clippy::indexing_slicing)]
    pub fn result(&self, idx: u8) -> Option<RintResult> {
        if (idx as usize) < MAX_BATTERIES {
            self.results[idx as usize]
        } else {
            None
        }
    }

    /// Reçoit une requête externe (commande `BLE` `TriggerRint`) pour armer
    /// une mesure. Refuse si :
    /// - state != `Idle` (une mesure est déjà en cours)
    /// - `idx` hors range
    /// - la batterie cible n'est pas `Online` (sécurité §6.1)
    /// - un autre fault est en cours dans la fleet (safety)
    /// - `|i_before| < MIN_CURRENT_FOR_MEASUREMENT_MA`
    ///
    /// # Errors
    /// Voir `RintError`.
    #[allow(clippy::indexing_slicing)]
    pub fn request(&mut self, idx: u8, snapshot: &Snapshot) -> Result<(), RintError> {
        if !matches!(self.state, RintState::Idle) {
            return Err(RintError::Busy);
        }
        if (idx as usize) >= MAX_BATTERIES || idx >= snapshot.n_bat {
            return Err(RintError::InvalidIndex);
        }
        let bat = &snapshot.batteries[idx as usize];
        if bat.state != BatteryState::Online {
            return Err(RintError::BatteryNotOnline);
        }
        // Refuse si une autre batterie est Offline ou Latched (Absent/Unknown OK).
        for (i, b) in snapshot.batteries.iter().enumerate() {
            if i == idx as usize {
                continue;
            }
            if matches!(b.state, BatteryState::Offline | BatteryState::Latched) {
                return Err(RintError::FleetUnsettled);
            }
        }
        if bat.current.abs() < MIN_CURRENT_FOR_MEASUREMENT_MA {
            return Err(RintError::CurrentTooLow);
        }
        self.state = RintState::ArmRequested { idx };
        Ok(())
    }

    /// Tick 5 Hz. Le caller fournit le `Snapshot` courant + `tick_no`.
    /// Retourne une `RintAction` à merger dans `Actions`.
    #[allow(
        clippy::indexing_slicing,
        clippy::arithmetic_side_effects,
        clippy::cast_possible_truncation
    )]
    pub fn tick(&mut self, snapshot: &Snapshot, tick_no: u32) -> RintAction {
        match self.state {
            RintState::Idle => RintAction::None,
            RintState::ArmRequested { idx } => {
                let bat = &snapshot.batteries[idx as usize];
                self.state = RintState::PulseStarted {
                    idx,
                    v_before: bat.voltage,
                    i_before: bat.current,
                    start_tick: tick_no,
                };
                RintAction::OpenContact(idx)
            }
            RintState::PulseStarted {
                idx,
                v_before,
                i_before,
                start_tick,
            } => {
                let elapsed = tick_no.wrapping_sub(start_tick);
                if elapsed < PULSE_DURATION_TICKS {
                    return RintAction::None;
                }
                let bat = &snapshot.batteries[idx as usize];
                let v_during = bat.voltage;
                let delta_mv = v_before.abs_diff(v_during);
                let r_milliohms = if i_before.abs() >= MIN_CURRENT_FOR_MEASUREMENT_MA {
                    // R (mΩ) = delta_mv × 1000 / |i_before|
                    let r = (u64::from(delta_mv) * 1_000) / u64::from(i_before.abs());
                    Milliohms::from_raw(r as u32)
                } else {
                    Milliohms::UNKNOWN
                };
                self.results[idx as usize] = Some(RintResult {
                    idx,
                    r_ohmic: r_milliohms,
                    v_before,
                    v_during,
                    i_before,
                    measured_at_tick: tick_no,
                });
                self.last_measure_tick[idx as usize] = Some(tick_no);
                self.state = RintState::Cooldown {
                    idx,
                    deadline_tick: tick_no.wrapping_add(COOLDOWN_TICKS),
                };
                RintAction::CloseContact(idx)
            }
            RintState::Cooldown { deadline_tick, .. } => {
                if tick_no >= deadline_tick {
                    self.state = RintState::Idle;
                }
                RintAction::None
            }
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

    fn nominal_snapshot(n_bat: u8) -> Snapshot {
        let mut snap = Snapshot::default();
        snap.n_bat = n_bat;
        for i in 0..(n_bat as usize) {
            snap.batteries[i].idx = i as u8;
            snap.batteries[i].state = BatteryState::Online;
            snap.batteries[i].voltage = Millivolts::from_raw(27_000);
            snap.batteries[i].current = Milliamps::from_raw(-500);
        }
        snap.system.topology_ok = true;
        snap
    }

    #[test]
    fn new_engine_idle() {
        let e = RintEngine::new();
        assert_eq!(e.state(), RintState::Idle);
        for i in 0..16 {
            assert!(e.result(i).is_none());
        }
    }

    #[test]
    fn request_idle_ok() {
        let mut e = RintEngine::new();
        let snap = nominal_snapshot(4);
        assert!(e.request(0, &snap).is_ok());
        assert_eq!(e.state(), RintState::ArmRequested { idx: 0 });
    }

    #[test]
    fn request_twice_fails() {
        let mut e = RintEngine::new();
        let snap = nominal_snapshot(4);
        e.request(0, &snap).unwrap();
        assert_eq!(e.request(1, &snap).err(), Some(RintError::Busy));
    }

    #[test]
    fn request_out_of_range() {
        let mut e = RintEngine::new();
        let snap = nominal_snapshot(4);
        assert_eq!(e.request(10, &snap).err(), Some(RintError::InvalidIndex));
    }

    #[test]
    fn request_offline_battery_fails() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        snap.batteries[0].state = BatteryState::Offline;
        assert_eq!(e.request(0, &snap).err(), Some(RintError::BatteryNotOnline));
    }

    #[test]
    fn request_fleet_unsettled_fails() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        snap.batteries[2].state = BatteryState::Latched;
        assert_eq!(e.request(0, &snap).err(), Some(RintError::FleetUnsettled));
    }

    #[test]
    fn request_current_too_low_fails() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        snap.batteries[0].current = Milliamps::from_raw(10);
        assert_eq!(e.request(0, &snap).err(), Some(RintError::CurrentTooLow));
    }

    #[test]
    fn full_pulse_sequence() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        e.request(0, &snap).unwrap();

        // Tick 100: arm → pulse start (open contact)
        let action = e.tick(&snap, 100);
        assert_eq!(action, RintAction::OpenContact(0));
        assert!(matches!(e.state(), RintState::PulseStarted { .. }));

        // Tick 101, 102: waiting
        assert_eq!(e.tick(&snap, 101), RintAction::None);
        assert_eq!(e.tick(&snap, 102), RintAction::None);

        // Tick 103: elapsed = 3 = PULSE_DURATION_TICKS → measure + close
        // Drop tension 700 mV pendant pulse
        snap.batteries[0].voltage = Millivolts::from_raw(26_300);
        let action = e.tick(&snap, 103);
        assert_eq!(action, RintAction::CloseContact(0));
        assert!(matches!(e.state(), RintState::Cooldown { .. }));

        // R = 700 mV × 1000 / 500 mA = 1400 mΩ
        let r = e.result(0).unwrap();
        assert_eq!(r.r_ohmic, Milliohms::from_raw(1400));
        assert_eq!(r.v_before, Millivolts::from_raw(27_000));
        assert_eq!(r.v_during, Millivolts::from_raw(26_300));
    }

    #[test]
    fn cooldown_returns_to_idle_after_deadline() {
        let mut e = RintEngine::new();
        let mut snap = nominal_snapshot(4);
        e.request(0, &snap).unwrap();
        e.tick(&snap, 100);
        e.tick(&snap, 101);
        e.tick(&snap, 102);
        snap.batteries[0].voltage = Millivolts::from_raw(26_800);
        e.tick(&snap, 103); // deadline = 153

        // Tick 152: still cooldown
        assert_eq!(e.tick(&snap, 152), RintAction::None);
        assert!(matches!(e.state(), RintState::Cooldown { .. }));

        // Tick 153: deadline → Idle
        assert_eq!(e.tick(&snap, 153), RintAction::None);
        assert_eq!(e.state(), RintState::Idle);
    }
}
