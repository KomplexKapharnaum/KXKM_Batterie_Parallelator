//! Property tests sur les invariants critiques de la state machine.
//! Cf spec §10.1 "Property tests via proptest".

#![allow(clippy::unwrap_used, clippy::panic)]

use bmu_protection::latch::SwitchCounter;
use bmu_protection::{integrate_charge, transition, Measurement, Transition, TransitionContext};
use bmu_types::{BatteryState, Config, LatchReason, MilliampHours, Milliamps, Millivolts};
use proptest::prelude::*;

/// Stratégie proptest : tension batterie plausible (0..40 V).
fn arb_millivolts() -> impl Strategy<Value = Millivolts> {
    (0i32..40_000).prop_map(Millivolts::from_raw)
}

/// Stratégie proptest : courant batterie plausible (-5..5 A).
fn arb_milliamps() -> impl Strategy<Value = Milliamps> {
    (-5_000i32..5_000).prop_map(Milliamps::from_raw)
}

proptest! {
    /// Invariant 1 : Latched ne transitionne jamais sans ResetLatch command.
    /// transition() n'a pas accès à ResetLatch → ne doit JAMAIS quitter Latched.
    #[test]
    fn latched_stays_latched(
        v in arb_millivolts(),
        i in arb_milliamps(),
        fleet_max in arb_millivolts(),
        now in 0u64..1_000_000,
    ) {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let ctx = TransitionContext {
            measurement: Measurement { voltage: v, current: i },
            fleet_max,
            counter: &counter,
            now_ms: now,
            cfg: &cfg,
            topology_ok: true,
        };
        prop_assert_eq!(
            transition(BatteryState::Latched, &ctx),
            Transition::Stay(BatteryState::Latched),
        );
    }

    /// Invariant 2 : Absent ne transitionne jamais.
    #[test]
    fn absent_stays_absent(
        v in arb_millivolts(),
        i in arb_milliamps(),
        fleet_max in arb_millivolts(),
        topology_ok in any::<bool>(),
    ) {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let ctx = TransitionContext {
            measurement: Measurement { voltage: v, current: i },
            fleet_max,
            counter: &counter,
            now_ms: 0,
            cfg: &cfg,
            topology_ok,
        };
        prop_assert_eq!(
            transition(BatteryState::Absent, &ctx),
            Transition::Stay(BatteryState::Absent),
        );
    }

    /// Invariant 3 : topology KO → latch (sauf Absent/Latched qui sont terminaux).
    #[test]
    fn topology_fail_latches_non_terminal(
        state in prop_oneof![
            Just(BatteryState::Unknown),
            Just(BatteryState::Online),
            Just(BatteryState::Offline),
        ],
        v in arb_millivolts(),
        i in arb_milliamps(),
        fleet_max in arb_millivolts(),
    ) {
        let cfg = Config::default();
        let counter = SwitchCounter::new();
        let ctx = TransitionContext {
            measurement: Measurement { voltage: v, current: i },
            fleet_max,
            counter: &counter,
            now_ms: 0,
            cfg: &cfg,
            topology_ok: false,
        };
        prop_assert_eq!(
            transition(state, &ctx),
            Transition::Latch(LatchReason::TopologyFailSafe),
        );
    }

    /// Invariant 4 : SwitchCounter::count ne décroît jamais via record_fault.
    #[test]
    fn counter_monotonic_under_record(
        faults in prop::collection::vec(0u64..10_000, 0..20),
    ) {
        let mut counter = SwitchCounter::new();
        let mut prev_count = 0u8;
        for t in faults {
            counter.record_fault(t);
            prop_assert!(counter.count() >= prev_count);
            prev_count = counter.count();
        }
    }

    /// Invariant 5 : integrate_charge préserve la direction du courant.
    /// Positive → ah augmente ou stable ; négative → ah diminue ou stable.
    #[test]
    fn integrate_charge_sign_preserved(
        initial in -10_000i32..10_000,
        current_ma in -5_000i32..5_000,
        dt_ms in 0u32..10_000,
    ) {
        let before = MilliampHours::from_raw(initial);
        let mut residue: i64 = 0;
        let after = integrate_charge(
            before,
            Milliamps::from_raw(current_ma),
            dt_ms,
            &mut residue,
        );
        match current_ma.cmp(&0) {
            core::cmp::Ordering::Greater => {
                prop_assert!(after.as_raw() >= before.as_raw());
            }
            core::cmp::Ordering::Less => {
                prop_assert!(after.as_raw() <= before.as_raw());
            }
            core::cmp::Ordering::Equal => {
                prop_assert_eq!(after, before);
            }
        }
    }
}
