//! Transitions pures (state, measurement, context) → (new state, action).
//! Cf spec §5.1 diagramme d'états F01-F11.

#![allow(clippy::module_name_repetitions)]

use crate::checks::{check_imbalance, check_oc, check_ov, check_uv};
use crate::latch::{OfflineReasonExt, SwitchCounter};
use bmu_types::{BatteryState, Config, LatchReason, Milliamps, Millivolts, OfflineReason};

/// Mesure instantanée tension/courant d'une batterie.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Measurement {
    pub voltage: Millivolts,
    pub current: Milliamps,
}

/// Contexte complet fourni à [`transition`] pour évaluer une batterie.
#[derive(Debug, Clone, Copy)]
pub struct TransitionContext<'a> {
    pub measurement: Measurement,
    pub fleet_max: Millivolts,
    pub counter: &'a SwitchCounter,
    pub now_ms: u64,
    pub cfg: &'a Config,
    pub topology_ok: bool,
}

/// Résultat d'une transition d'état (pure, sans side effect).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Transition {
    /// Rester dans le même état.
    Stay(BatteryState),
    /// Transitionner vers un nouvel état avec une raison.
    ///
    /// Convention sur le 2ème paramètre :
    /// - `OfflineReason::Ok` — transition vers un état sain (ex. `Move(Online, Ok)`
    ///   après recovery ou `Unknown → Online` au boot). Marqueur "pas de fault".
    /// - Toute autre valeur — transition causée par un fault détecté
    ///   (ex. `Move(Offline, UnderVoltage)`).
    ///
    /// Les callers Task 6.4+ doivent traiter `Ok` comme "transition saine"
    /// et les autres variants comme "transition fault-driven à logger".
    Move(BatteryState, OfflineReason),
    /// Latch permanent avec raison.
    Latch(LatchReason),
}

/// Fonction de transition pure. Cf spec §5.1 diagramme d'états F01-F11.
/// Retourne la [`Transition`] à appliquer sans modifier d'état mutable.
///
/// Ordre de priorité :
/// 1. Topology mismatch → `Latch(TopologyFailSafe)` (sauf `Absent`/`Latched` qui restent)
/// 2. États terminaux `Absent`/`Latched` → `Stay`
/// 3. Fault detection dans l'ordre : `UV` > `OV` > `OC` > `Imbalance`
/// 4. `Unknown` → `Online(Ok)` ou `Offline(fault)`
/// 5. `Online` + fault → `Latch` si `count+1 >= nb_switch_max`, sinon `Offline(fault)`
/// 6. `Offline` + delay elapsed + no fault → `Online(Ok)`
#[must_use]
pub fn transition(state: BatteryState, ctx: &TransitionContext<'_>) -> Transition {
    // Priority 0 : topology mismatch → latch tous états sauf terminaux.
    if !ctx.topology_ok {
        return match state {
            BatteryState::Absent | BatteryState::Latched => Transition::Stay(state),
            BatteryState::Unknown | BatteryState::Online | BatteryState::Offline => {
                Transition::Latch(LatchReason::TopologyFailSafe)
            }
        };
    }

    // États terminaux.
    match state {
        BatteryState::Absent => return Transition::Stay(BatteryState::Absent),
        BatteryState::Latched => return Transition::Stay(BatteryState::Latched),
        BatteryState::Unknown | BatteryState::Online | BatteryState::Offline => {}
    }

    let fault = detect_fault(ctx);

    match state {
        BatteryState::Unknown => match fault {
            None => Transition::Move(BatteryState::Online, OfflineReason::Ok),
            Some(reason) => Transition::Move(BatteryState::Offline, reason),
        },
        BatteryState::Online => match fault {
            None => Transition::Stay(BatteryState::Online),
            Some(reason) => {
                // F08 : si ce fault va porter le compteur à `nb_switch_max`,
                // latch permanent directement.
                if ctx.counter.count().saturating_add(1) >= ctx.cfg.nb_switch_max {
                    Transition::Latch(reason.as_latch_reason())
                } else {
                    Transition::Move(BatteryState::Offline, reason)
                }
            }
        },
        BatteryState::Offline => {
            if !ctx.counter.reconnect_delay_elapsed(ctx.now_ms, ctx.cfg) {
                return Transition::Stay(BatteryState::Offline);
            }
            match fault {
                None => Transition::Move(BatteryState::Online, OfflineReason::Ok),
                Some(_) => Transition::Stay(BatteryState::Offline),
            }
        }
        // Absent / Latched déjà traités plus haut ; exhaustivité du match.
        BatteryState::Absent | BatteryState::Latched => Transition::Stay(state),
    }
}

/// Évalue les checks F03-F06 et retourne la première raison de fault.
/// Priorité : `UV` > `OV` > `OC` > `Imbalance`.
/// `#[inline]` : hot path (appelée par tick par batterie en Phase 7).
#[inline]
fn detect_fault(ctx: &TransitionContext<'_>) -> Option<OfflineReason> {
    let v = ctx.measurement.voltage;
    let i = ctx.measurement.current;

    if check_uv(v, ctx.cfg) {
        return Some(OfflineReason::UnderVoltage);
    }
    if check_ov(v, ctx.cfg) {
        return Some(OfflineReason::OverVoltage);
    }
    if check_oc(i, ctx.cfg) {
        return Some(OfflineReason::OverCurrent);
    }
    if check_imbalance(v, ctx.fleet_max, ctx.cfg) {
        return Some(OfflineReason::Imbalance);
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctx<'a>(
        v_mv: i32,
        i_ma: i32,
        fleet_max_mv: i32,
        counter: &'a SwitchCounter,
        cfg: &'a Config,
        now_ms: u64,
    ) -> TransitionContext<'a> {
        TransitionContext {
            measurement: Measurement {
                voltage: Millivolts::from_raw(v_mv),
                current: Milliamps::from_raw(i_ma),
            },
            fleet_max: Millivolts::from_raw(fleet_max_mv),
            counter,
            now_ms,
            cfg,
            topology_ok: true,
        }
    }

    #[test]
    fn unknown_to_online_when_ok() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Unknown, &c),
            Transition::Move(BatteryState::Online, OfflineReason::Ok),
        );
    }

    #[test]
    fn unknown_to_offline_uv() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(23_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Unknown, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::UnderVoltage),
        );
    }

    #[test]
    fn online_to_offline_ov() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(30_001, 0, 30_001, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::OverVoltage),
        );
    }

    #[test]
    fn online_to_offline_oc() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 1_200, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::OverCurrent),
        );
    }

    #[test]
    fn online_to_offline_imbalance() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        // v = 25000, fleet_max = 27000 → diff 2000 > 1000
        let c = ctx(25_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Move(BatteryState::Offline, OfflineReason::Imbalance),
        );
    }

    #[test]
    fn offline_retries_online_after_delay() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        counter.record_fault(5_000);
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 15_001);
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Move(BatteryState::Online, OfflineReason::Ok),
        );
    }

    #[test]
    fn offline_stays_before_delay() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        counter.record_fault(5_000);
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 10_000);
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Stay(BatteryState::Offline),
        );
    }

    #[test]
    fn offline_stays_if_fault_persists() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        counter.record_fault(5_000);
        let c = ctx(23_000, 0, 27_000, &counter, &cfg, 16_000);
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Stay(BatteryState::Offline),
        );
    }

    /// F08 : après `nb_switch_max` faults, latch permanent.
    #[test]
    fn online_fault_at_max_triggers_latch() {
        let cfg = Config::default();
        let mut counter = SwitchCounter::new();
        for i in 1..=4 {
            counter.record_fault(i * 1_000);
        }
        // Counter = 4. Next fault → 5 = `nb_switch_max` → latch F08.
        let c = ctx(23_000, 0, 27_000, &counter, &cfg, 5_000);
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Latch(LatchReason::MaxSwitchReached),
        );
    }

    /// **Régression CRIT-C** (deadlock absent — core sans mutex).
    /// Bug historique : `validateBatteryVoltageForSwitch` v1 utilisait un mutex
    /// qui pouvait deadlock en réentrance. Le core Rust n'a PAS de mutex.
    /// [`transition`] est une fonction pure stateless : deadlock impossible par construction.
    #[test]
    fn test_crit_c_transition_is_pure_no_lock() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        // Appels "nested" simulés : transition dans transition, sans lock.
        let _ = transition(BatteryState::Unknown, &c);
        let _ = transition(BatteryState::Online, &c);
        let _ = transition(BatteryState::Offline, &c);
    }

    #[test]
    fn topology_mismatch_latches_all() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let mut c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        c.topology_ok = false;
        assert_eq!(
            transition(BatteryState::Online, &c),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
        assert_eq!(
            transition(BatteryState::Unknown, &c),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
        assert_eq!(
            transition(BatteryState::Offline, &c),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
    }

    #[test]
    fn latched_stays_latched() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(27_000, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Latched, &c),
            Transition::Stay(BatteryState::Latched),
        );
    }

    #[test]
    fn absent_stays_absent() {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let c = ctx(0, 0, 27_000, &counter, &cfg, 0);
        assert_eq!(
            transition(BatteryState::Absent, &c),
            Transition::Stay(BatteryState::Absent),
        );
    }
}
