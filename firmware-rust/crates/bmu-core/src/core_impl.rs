//! Struct `BmuCore` qui agrège protection + `RintEngine` + `BalancerEngine`
//! et expose l'API unique appelée par la façade FFI (`bmu_core_tick`,
//! `bmu_core_command`).
//!
//! Cf plan Phase 9 Task 9.2 et spec §3.4.

#![allow(clippy::module_name_repetitions)]

use bmu_balancer::BalancerEngine;
use bmu_protection::{transition, BatteryManager, Measurement, Transition, TransitionContext};
use bmu_rint::{RintAction, RintEngine};
use bmu_types::{
    Actions, BatteryState, Command, Config, LatchReason, Milliamps, Millivolts, OfflineReason,
    Snapshot, MAX_BATTERIES,
};

/// Instance core — possédée par l'appelant C via pointeur opaque `BmuCore*`.
///
/// Agrège la configuration, le `BatteryManager` (counters + `Ah`), le
/// `RintEngine` (mesure `R_int`), le `BalancerEngine` (`PWM` duty), le
/// dernier `Snapshot` produit et les `pending_latch` issus de commandes
/// `ForceOff`.
///
/// NOTE : `Clone` uniquement (pas `Copy`) — les sous-composants sont
/// stateful, une copie silencieuse créerait une divergence.
#[derive(Debug, Clone)]
pub struct BmuCore {
    config: Config,
    manager: BatteryManager,
    rint: RintEngine,
    balancer: BalancerEngine,
    cached_snapshot: Snapshot,
    pending_latch: [Option<LatchReason>; MAX_BATTERIES],
    tick_count: u32,
}

impl BmuCore {
    /// Crée un `BmuCore` avec la `Config` fournie et des sous-composants
    /// vierges.
    #[must_use]
    pub fn new(config: Config) -> Self {
        Self {
            config,
            manager: BatteryManager::new(),
            rint: RintEngine::new(),
            balancer: BalancerEngine::new(),
            cached_snapshot: Snapshot::default(),
            pending_latch: [None; MAX_BATTERIES],
            tick_count: 0,
        }
    }

    /// Retourne la `Config` active.
    #[must_use]
    pub const fn config(&self) -> &Config {
        &self.config
    }

    /// Remplace la `Config` après validation.
    ///
    /// # Errors
    /// `ConfigError` si la nouvelle config échoue `validate()`.
    pub fn set_config(&mut self, new_cfg: Config) -> Result<(), bmu_types::ConfigError> {
        new_cfg.validate()?;
        self.config = new_cfg;
        Ok(())
    }

    /// Retourne le dernier `Snapshot` produit par `step()`.
    #[must_use]
    pub const fn cached_snapshot(&self) -> &Snapshot {
        &self.cached_snapshot
    }

    /// Cœur de la boucle : consomme un `ParsedInputs` et produit un nouveau
    /// `Snapshot` + `Actions`.
    ///
    /// Pipeline (§2.1) :
    /// 1. Met à jour topologie / `n_bat`.
    /// 2. Construit le snapshot de travail (mesures + états précédents).
    /// 3. Calcule `fleet_max` UNE seule fois sur le snapshot frais (`CRIT-B`).
    /// 4. Lance les transitions par batterie, applique les `pending_latch`.
    /// 5. Intègre `Ah`, calcule le `protection_allowed_mask`.
    /// 6. `RintEngine::tick` + `BalancerEngine::tick`, merge avec protection.
    /// 7. Compose `Actions` (masks `tca_set_mask` / `tca_clr_mask`).
    #[allow(
        clippy::too_many_lines,
        clippy::indexing_slicing,
        clippy::cast_possible_truncation,
        clippy::arithmetic_side_effects
    )]
    pub fn step(&mut self, parsed: &ParsedInputs) -> (Snapshot, Actions) {
        self.tick_count = self.tick_count.wrapping_add(1);

        // 1. Update topology + n_bat.
        self.manager.set_topology(parsed.n_ina, parsed.n_tca);
        let n_bat = self.manager.n_bat();
        let topology_ok = n_bat > 0;

        // 2. Snapshot de travail depuis mesures + états précédents.
        let mut snap = self.cached_snapshot;
        snap.n_bat = n_bat;
        snap.system.topology_ok = topology_ok;
        snap.system.n_bat = n_bat;
        snap.system.monotonic_us = parsed.monotonic_us;

        for i in 0..(n_bat as usize) {
            let m = &parsed.measurements[i];
            snap.batteries[i].idx = i as u8;
            snap.batteries[i].voltage = m.voltage;
            snap.batteries[i].current = m.current;
        }
        // Slots au-delà de n_bat → Absent.
        for i in (n_bat as usize)..MAX_BATTERIES {
            snap.batteries[i].state = BatteryState::Absent;
            snap.batteries[i].voltage = Millivolts::ZERO;
            snap.batteries[i].current = Milliamps::ZERO;
        }

        // 3. Calcul `fleet_max` UNE fois (`CRIT-B`).
        //
        // Note tick 1 : les batteries sont encore en état `Unknown` (copiées
        // du `cached_snapshot` default), et `fleet_max_voltage()` ne prend en
        // compte que les `Online`. Donc `fleet_max == ZERO` au premier tick.
        // C'est acceptable : `check_imbalance` court-circuite quand
        // `fleet_max <= v` → aucun imbalance ne peut déclencher au tick 1.
        // Le vrai check commence au tick 2 quand les états sont stabilisés.
        let fleet_max = snap.fleet_max_voltage();

        // 4. Transitions par batterie.
        let now_ms = parsed.monotonic_us / 1_000;
        let dt_ms = self.config.tick_period_ms;
        let mut protection_allowed_mask: u16 = 0;
        for i in 0..(n_bat as usize) {
            let current_state = snap.batteries[i].state;
            let counter = self.manager.counter(i as u8);
            let ctx = TransitionContext {
                measurement: Measurement {
                    voltage: snap.batteries[i].voltage,
                    current: snap.batteries[i].current,
                },
                fleet_max,
                counter: &counter,
                now_ms,
                cfg: &self.config,
                topology_ok,
            };
            let trans = if let Some(latch_reason) = self.pending_latch[i] {
                self.pending_latch[i] = None;
                Transition::Latch(latch_reason)
            } else {
                transition(current_state, &ctx)
            };
            match trans {
                Transition::Stay(s) => {
                    snap.batteries[i].state = s;
                }
                Transition::Move(new_state, reason) => {
                    snap.batteries[i].state = new_state;
                    snap.batteries[i].reason = reason;
                    self.manager.apply_transition(i as u8, trans, now_ms);
                }
                Transition::Latch(reason) => {
                    snap.batteries[i].state = BatteryState::Latched;
                    snap.batteries[i].reason = match reason {
                        // Préserve la dernière raison Offline : c'est elle
                        // qui a causé l'épuisement du compteur de switchs.
                        LatchReason::MaxSwitchReached => snap.batteries[i].reason,
                        LatchReason::TopologyFailSafe => OfflineReason::Topology,
                        LatchReason::ManualForceOff => OfflineReason::Manual,
                    };
                    self.manager.apply_transition(i as u8, trans, now_ms);
                }
            }
            snap.batteries[i].switch_count = self.manager.counter(i as u8).count();
            // Intégration Ah.
            self.manager
                .integrate(i as u8, snap.batteries[i].current, dt_ms);
            snap.batteries[i].ah_remaining = self.manager.ah(i as u8);
            // `protection_allowed_mask`.
            if snap.batteries[i].state.allows_switch_on() {
                protection_allowed_mask |= 1u16 << i;
            }
        }
        snap.system.wdt_feeds = snap.system.wdt_feeds.wrapping_add(1);

        // 5. `RintEngine`.
        let rint_action = self.rint.tick(&snap, self.tick_count);
        // `rint_trigger_idx` expose au caller C quelle batterie est ciblée
        // par une mesure R_int en cours. Sentinelle `0xFF` = pas de trigger
        // (aucune mesure active). La valeur réelle est implicite dans le
        // `RintAction` overlay ci-dessous ; ce champ est un indicateur
        // synthétique à implémenter en Task 9.3 (lecture depuis
        // `rint.state()` pour extraire l'`idx` actif).
        let rint_trigger_idx: u8 = 0xFF;

        // 6. `BalancerEngine`.
        let balancer_mask = self.balancer.tick(&snap);
        let final_on_mask = Actions::merge_balancer(balancer_mask, protection_allowed_mask);

        // Expose balancer duty + `R_int` par batterie.
        for i in 0..(n_bat as usize) {
            snap.batteries[i].balancer_duty_pct = self.balancer.duty_pct(i as u8);
            if let Some(r) = self.rint.result(i as u8) {
                snap.batteries[i].r_ohmic = r.r_ohmic;
            }
        }

        // 7. Compose `Actions`.
        let n_bat_u16 = u16::from(n_bat);
        let active_mask = if n_bat_u16 >= 16 {
            0xFFFF
        } else {
            (1u16 << n_bat_u16) - 1
        };
        let tca_set_mask = final_on_mask;
        let tca_clr_mask = !final_on_mask & active_mask;

        // Overlay `R_int` par-dessus.
        let (tca_set_mask, tca_clr_mask) = match rint_action {
            RintAction::None => (tca_set_mask, tca_clr_mask),
            RintAction::OpenContact(idx) => {
                let bit = 1u16 << idx;
                (tca_set_mask & !bit, tca_clr_mask | bit)
            }
            RintAction::CloseContact(idx) => {
                let bit = 1u16 << idx;
                (tca_set_mask | bit, tca_clr_mask & !bit)
            }
        };

        let actions = Actions {
            tca_set_mask,
            tca_clr_mask,
            rint_trigger_idx,
            request_soh_inference: self.tick_count.is_multiple_of(300),
        };

        self.cached_snapshot = snap;
        (snap, actions)
    }

    /// Applique une commande reçue via `BLE`/`UI`.
    ///
    /// # Errors
    /// `CoreError::InvalidIndex` si `idx >= MAX_BATTERIES`,
    /// `CoreError::InvalidConfig` si `SetConfig` échoue,
    /// `CoreError::RintBusy` si `TriggerRint` est rejetée.
    #[allow(clippy::indexing_slicing)]
    pub fn handle_command(&mut self, cmd: Command) -> Result<(), CoreError> {
        match cmd {
            Command::None => Ok(()),
            Command::ForceOff { idx } => {
                if (idx as usize) >= MAX_BATTERIES {
                    return Err(CoreError::InvalidIndex);
                }
                self.pending_latch[idx as usize] = Some(LatchReason::ManualForceOff);
                Ok(())
            }
            Command::ResetAh { idx } => {
                self.manager.reset_ah(idx);
                Ok(())
            }
            Command::TriggerRint { idx } => self
                .rint
                .request(idx, &self.cached_snapshot)
                .map_err(|_| CoreError::RintBusy),
            Command::ResetLatch { idx } => {
                if (idx as usize) >= MAX_BATTERIES {
                    return Err(CoreError::InvalidIndex);
                }
                self.manager.reset_counter(idx);
                self.cached_snapshot.batteries[idx as usize].state = BatteryState::Unknown;
                Ok(())
            }
            Command::SetConfig(new_cfg) => self
                .set_config(new_cfg)
                .map_err(|_| CoreError::InvalidConfig),
            Command::UpdateSoh { idx, soh_pct } => {
                if (idx as usize) >= MAX_BATTERIES {
                    return Err(CoreError::InvalidIndex);
                }
                self.cached_snapshot.batteries[idx as usize].soh_pct = soh_pct;
                Ok(())
            }
            Command::TopologyChanged { n_ina, n_tca } => {
                self.manager.set_topology(n_ina, n_tca);
                Ok(())
            }
        }
    }
}

/// Mesures parsées injectées dans `step()`.
#[derive(Debug, Clone, Copy)]
pub struct ParsedInputs {
    pub n_ina: u8,
    pub n_tca: u8,
    pub measurements: [Measurement; MAX_BATTERIES],
    pub monotonic_us: u64,
}

impl Default for ParsedInputs {
    fn default() -> Self {
        Self {
            n_ina: 0,
            n_tca: 0,
            measurements: [Measurement {
                voltage: Millivolts::ZERO,
                current: Milliamps::ZERO,
            }; MAX_BATTERIES],
            monotonic_us: 0,
        }
    }
}

/// Erreurs opérationnelles du `BmuCore`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CoreError {
    InvalidIndex,
    InvalidConfig,
    RintBusy,
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

    fn inputs(n_ina: u8, n_tca: u8, voltages: &[i32], currents: &[i32]) -> ParsedInputs {
        let mut p = ParsedInputs::default();
        p.n_ina = n_ina;
        p.n_tca = n_tca;
        for i in 0..(n_ina as usize) {
            p.measurements[i] = Measurement {
                voltage: Millivolts::from_raw(voltages[i]),
                current: Milliamps::from_raw(currents[i]),
            };
        }
        p.monotonic_us = 1_000_000;
        p
    }

    #[test]
    fn core_new_empty_snapshot() {
        let core = BmuCore::new(Config::default());
        assert_eq!(core.cached_snapshot().n_bat, 0);
    }

    #[test]
    fn core_step_nominal_4_batteries() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(
            4,
            1,
            &[27_000, 27_100, 26_900, 27_050],
            &[-500, -500, -500, -500],
        );
        let (snap, actions) = core.step(&p);
        assert_eq!(snap.n_bat, 4);
        assert!(snap.system.topology_ok);
        for i in 0..4 {
            assert_eq!(snap.batteries[i].state, BatteryState::Online);
        }
        // Actions may be 0 on the very first tick because balancer cycle_pos
        // is still at 0 for every slot (no duty yet). Après un 2e tick le
        // balancer propose, mais on ne teste ici que la convergence Online.
        let _ = actions;
    }

    #[test]
    fn core_step_topology_mismatch_latches_all() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(16, 2, &[27_000; 16], &[-500; 16]);
        let (snap, actions) = core.step(&p);
        assert_eq!(snap.n_bat, 0);
        assert!(!snap.system.topology_ok);
        assert_eq!(actions.tca_set_mask, 0);
    }

    #[test]
    fn core_step_undervoltage_battery() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[23_000, 27_000, 27_000, 27_000], &[-500; 4]);
        let (snap, _) = core.step(&p);
        assert_eq!(snap.batteries[0].state, BatteryState::Offline);
        assert_eq!(snap.batteries[0].reason, OfflineReason::UnderVoltage);
        assert_eq!(snap.batteries[1].state, BatteryState::Online);
    }

    #[test]
    fn core_force_off_latches() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-500; 4]);
        let _ = core.step(&p);
        core.handle_command(Command::ForceOff { idx: 2 }).unwrap();
        let (snap, _) = core.step(&p);
        assert_eq!(snap.batteries[2].state, BatteryState::Latched);
    }

    #[test]
    fn core_reset_latch_returns_to_unknown() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-500; 4]);
        let _ = core.step(&p);
        core.handle_command(Command::ForceOff { idx: 1 }).unwrap();
        let _ = core.step(&p);
        assert_eq!(
            core.cached_snapshot().batteries[1].state,
            BatteryState::Latched
        );
        core.handle_command(Command::ResetLatch { idx: 1 }).unwrap();
        let (snap, _) = core.step(&p);
        assert_eq!(snap.batteries[1].state, BatteryState::Online);
    }

    #[test]
    fn core_update_soh_stored() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-500; 4]);
        let _ = core.step(&p);
        core.handle_command(Command::UpdateSoh {
            idx: 0,
            soh_pct: 87,
        })
        .unwrap();
        assert_eq!(core.cached_snapshot().batteries[0].soh_pct, 87);
    }

    #[test]
    fn core_reset_ah_clears_accumulator() {
        let mut core = BmuCore::new(Config::default());
        let p = inputs(4, 1, &[27_000; 4], &[-1000; 4]);
        for _ in 0..100 {
            let _ = core.step(&p);
        }
        core.handle_command(Command::ResetAh { idx: 0 }).unwrap();
        let (snap, _) = core.step(&p);
        // Après reset, l'accumulateur ne peut pas avoir plus d'un tick intégré.
        // |Δmah| ≤ |1000 mA × 200 ms / 3 600 000| = 0, donc ~0.
        assert!(snap.batteries[0].ah_remaining.as_raw().abs() < 10);
    }
}
