//! Mirrors `#[repr(C)]` de `bmu-types` pour l'ABI C. Conversions `From` vers
//! les types Rust internes. Aucune logique métier, juste des structs packed.
//!
//! Ce module est consommé par la façade `extern "C"` (Task 9.3) et utilisé
//! par `cbindgen` pour générer `bmu_core.h`.

#![allow(clippy::module_name_repetitions)]

use bmu_types::{Battery, Config, Milliamps, Millivolts, Snapshot, System, NUM_SOH_FEATURES};

/// Mirror `C` de `Config`. Inclut les normalisations `SoH` pour le modèle
/// `TFLite` Micro (cf `fpnn_soh_v3_int8.tflite`).
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuConfigC {
    pub umin_mv: u32,
    pub umax_mv: u32,
    pub imax_ma: i32,
    pub vdiff_imbalance_mv: u32,
    pub nb_switch_max: u8,
    pub reconnect_delay_ms: u32,
    pub tick_period_ms: u32,
    /// Moyennes des 13 features `SoH` (normalisation `TFLite`). Default = 0.0.
    pub soh_feat_means: [f32; 13],
    /// Écarts-type des 13 features `SoH` (normalisation `TFLite`). Default = 1.0.
    pub soh_feat_stds: [f32; 13],
}

impl From<&BmuConfigC> for Config {
    #[allow(clippy::cast_possible_wrap)]
    fn from(c: &BmuConfigC) -> Self {
        Self {
            umin: Millivolts::from_raw(c.umin_mv as i32),
            umax: Millivolts::from_raw(c.umax_mv as i32),
            imax: Milliamps::from_raw(c.imax_ma),
            vdiff_imbalance: Millivolts::from_raw(c.vdiff_imbalance_mv as i32),
            nb_switch_max: c.nb_switch_max,
            reconnect_delay_ms: c.reconnect_delay_ms,
            tick_period_ms: c.tick_period_ms,
            soh_feat_means: c.soh_feat_means,
            soh_feat_stds: c.soh_feat_stds,
        }
    }
}

impl From<&Config> for BmuConfigC {
    #[allow(clippy::cast_sign_loss)]
    fn from(c: &Config) -> Self {
        Self {
            umin_mv: c.umin.as_raw() as u32,
            umax_mv: c.umax.as_raw() as u32,
            imax_ma: c.imax.as_raw(),
            vdiff_imbalance_mv: c.vdiff_imbalance.as_raw() as u32,
            nb_switch_max: c.nb_switch_max,
            reconnect_delay_ms: c.reconnect_delay_ms,
            tick_period_ms: c.tick_period_ms,
            soh_feat_means: c.soh_feat_means,
            soh_feat_stds: c.soh_feat_stds,
        }
    }
}

// Compile-time sanity : verify the FFI struct uses 13 features (matches Rust).
const _: () = assert!(NUM_SOH_FEATURES == 13);

/// Dump brut des registres `INA`/`TCA` lus par la couche C avant d'appeler `tick()`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuRawInputs {
    pub n_ina: u8,
    pub n_tca: u8,
    pub ina_registers: [[u8; 18]; 16],
    pub tca_inputs: [u8; 4],
    pub climate_temp_c10: i16,
    pub climate_rh_pct10: u16,
    pub monotonic_us: u64,
}

/// Mirror `C` de `Battery`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuBatteryC {
    pub idx: u8,
    pub state: u8,
    pub state_reason: u8,
    pub switch_count: u8,
    pub voltage_mv: i32,
    pub current_ma: i32,
    pub ah_remaining_ma_h: i32,
    pub temp_c10: i16,
    pub soh_pct: u8,
    pub balancer_duty_pct: u8,
    pub r_ohmic_m_ohms: u32,
}

impl From<&Battery> for BmuBatteryC {
    fn from(b: &Battery) -> Self {
        Self {
            idx: b.idx,
            state: b.state as u8,
            state_reason: b.reason as u8,
            switch_count: b.switch_count,
            voltage_mv: b.voltage.as_raw(),
            current_ma: b.current.as_raw(),
            ah_remaining_ma_h: b.ah_remaining.as_raw(),
            temp_c10: b.temp_c10,
            soh_pct: b.soh_pct,
            balancer_duty_pct: b.balancer_duty_pct,
            r_ohmic_m_ohms: b.r_ohmic.as_raw(),
        }
    }
}

/// Mirror `C` de `System`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuSystemC {
    pub topology_ok: u8,
    pub n_bat: u8,
    pub tick_us_p50: u32,
    pub tick_us_p99: u32,
    pub wdt_feeds: u32,
    pub monotonic_us: u64,
}

impl From<&System> for BmuSystemC {
    fn from(s: &System) -> Self {
        Self {
            topology_ok: u8::from(s.topology_ok),
            n_bat: s.n_bat,
            tick_us_p50: s.tick_us_p50,
            tick_us_p99: s.tick_us_p99,
            wdt_feeds: s.wdt_feeds,
            monotonic_us: s.monotonic_us,
        }
    }
}

/// Mirror `C` de `Snapshot`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuSnapshotC {
    pub n_bat: u8,
    pub batteries: [BmuBatteryC; MAX_BATTERIES],
    pub system: BmuSystemC,
}

impl From<&Snapshot> for BmuSnapshotC {
    fn from(s: &Snapshot) -> Self {
        let mut batteries = [BmuBatteryC {
            idx: 0,
            state: 0,
            state_reason: 0,
            switch_count: 0,
            voltage_mv: 0,
            current_ma: 0,
            ah_remaining_ma_h: 0,
            temp_c10: 0,
            soh_pct: 0xFF,
            balancer_duty_pct: 0,
            r_ohmic_m_ohms: u32::MAX,
        }; MAX_BATTERIES];
        for (slot, battery) in batteries.iter_mut().zip(s.batteries.iter()) {
            *slot = BmuBatteryC::from(battery);
        }
        Self {
            n_bat: s.n_bat,
            batteries,
            system: BmuSystemC::from(&s.system),
        }
    }
}

/// Mirror `C` de `Actions` produites par `BmuCore::tick`.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuActionsC {
    pub tca_set_mask: u16,
    pub tca_clr_mask: u16,
    pub rint_trigger_idx: u8,
    pub request_soh_inference: u8,
}

/// Discriminant pour `BmuCommandC::kind`, aligné avec `bmu_types::Command`.
#[repr(u8)]
#[derive(Debug, Clone, Copy)]
pub enum BmuCommandKind {
    None = 0,
    ForceOff = 1,
    ResetAh = 2,
    TriggerRint = 3,
    ResetLatch = 4,
    SetConfig = 6,
    UpdateSoh = 7,
    TopologyChanged = 8,
}

/// Mirror `C` d'une commande entrante (`BLE`, `UI`, auto).
///
/// **Note Phase 15.1** : le `payload` est passé de `[u8; 32]` à `[u8; 256]`
/// pour absorber la nouvelle taille de `BmuConfigC` (qui inclut maintenant
/// les 26 floats de normalisation `SoH`). Cet élargissement est compatible
/// car Phase 19 (`BLE` Control `SetConfig`) n'a pas encore shipped.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct BmuCommandC {
    /// Discriminant : voir `BmuCommandKind` pour les valeurs valides.
    pub kind: u8,
    pub target_idx: u8,
    /// Payload interprété selon `kind` :
    /// - `UpdateSoh` : `payload[0]` = `soh_pct`
    /// - `SetConfig` : `payload[0..size_of::<BmuConfigC>()]` = `BmuConfigC` packed
    ///   (vérification statique dans le test `setconfig_payload_fits`)
    /// - `TopologyChanged` : `payload[0]` = `n_ina`, `payload[1]` = `n_tca`
    pub payload: [u8; 256],
}

/// Nombre maximum de batteries supporté (miroir de `bmu_types::MAX_BATTERIES`).
///
/// **Littéral explicite obligatoire** pour que `cbindgen` émette un
/// `#define MAX_BATTERIES 16` dans `bmu_core.h` — il n'évalue pas les
/// expressions const (il ne suivrait pas `= bmu_types::MAX_BATTERIES`).
/// Une assertion statique vérifie la cohérence avec la source authoritative.
pub const MAX_BATTERIES: usize = 16;

const _: () = assert!(MAX_BATTERIES == bmu_types::MAX_BATTERIES);

/// Codes de retour C-friendly exposés par la façade `extern "C"`.
pub const BMU_OK: i32 = 0;
pub const BMU_ERR_NULL: i32 = -1;
pub const BMU_ERR_INVALID_CONFIG: i32 = -2;
pub const BMU_ERR_INVALID_INDEX: i32 = -3;
pub const BMU_ERR_BUSY: i32 = -4;
pub const BMU_ERR_UNSUPPORTED: i32 = -5;

#[cfg(test)]
#[allow(
    clippy::unwrap_used,
    clippy::panic,
    clippy::field_reassign_with_default
)]
mod tests {
    use super::*;
    use bmu_types::{BatteryState, MilliampHours, Milliohms, OfflineReason};

    #[test]
    fn config_roundtrip() {
        let rust_cfg = Config::default();
        let c_cfg = BmuConfigC::from(&rust_cfg);
        let back: Config = (&c_cfg).into();
        assert_eq!(back, rust_cfg);
    }

    #[test]
    fn battery_roundtrip() {
        let mut b = Battery::default();
        b.idx = 7;
        b.state = BatteryState::Online;
        b.reason = OfflineReason::Ok;
        b.voltage = Millivolts::from_raw(27_000);
        b.current = Milliamps::from_raw(-500);
        b.ah_remaining = MilliampHours::from_raw(4500);
        b.soh_pct = 92;
        b.r_ohmic = Milliohms::from_raw(1234);
        let c = BmuBatteryC::from(&b);
        assert_eq!(c.idx, 7);
        assert_eq!(c.state, BatteryState::Online as u8);
        assert_eq!(c.voltage_mv, 27_000);
        assert_eq!(c.current_ma, -500);
        assert_eq!(c.ah_remaining_ma_h, 4500);
        assert_eq!(c.soh_pct, 92);
        assert_eq!(c.r_ohmic_m_ohms, 1234);
    }

    #[test]
    fn snapshot_carries_n_bat() {
        let mut s = Snapshot::default();
        s.n_bat = 8;
        let c = BmuSnapshotC::from(&s);
        assert_eq!(c.n_bat, 8);
        assert_eq!(c.batteries.len(), MAX_BATTERIES);
    }

    #[test]
    fn return_codes_distinct() {
        assert_ne!(BMU_OK, BMU_ERR_NULL);
        assert_ne!(BMU_ERR_NULL, BMU_ERR_INVALID_CONFIG);
        assert_ne!(BMU_ERR_INVALID_CONFIG, BMU_ERR_INVALID_INDEX);
    }

    /// Garantit que `BmuConfigC` tient dans `BmuCommandC::payload[u8;256]`.
    /// Si ce test casse, un champ a été ajouté à `BmuConfigC` qui dépasse
    /// le payload `SetConfig` → soit shrink `BmuConfigC`, soit grow le payload.
    #[test]
    fn setconfig_payload_fits() {
        assert!(
            core::mem::size_of::<BmuConfigC>() <= 256,
            "BmuConfigC ({} bytes) ne tient pas dans payload [u8;256]",
            core::mem::size_of::<BmuConfigC>()
        );
    }

    /// Garantit que les arrays `SoH` sont exactement 13 elements de chaque côté.
    #[test]
    fn soh_norm_arrays_have_13_elements() {
        let c = BmuConfigC::from(&Config::default());
        assert_eq!(c.soh_feat_means.len(), 13);
        assert_eq!(c.soh_feat_stds.len(), 13);
        // Default identity : means = 0.0, stds = 1.0
        for v in &c.soh_feat_means {
            assert_eq!(v.to_bits(), 0.0_f32.to_bits());
        }
        for v in &c.soh_feat_stds {
            assert_eq!(v.to_bits(), 1.0_f32.to_bits());
        }
    }

    /// Roundtrip avec valeurs de `SoH` non-default.
    #[test]
    fn config_roundtrip_with_soh_norm() {
        let mut rust = Config::default();
        rust.soh_feat_means = [
            27.3381, 0.2070, 0.2388, 0.3542, -0.00098, 0.00118,
            0.00676, 0.00288, 27.1311, 27.5451, 0.9603, 47.1759, 0.0585,
        ];
        rust.soh_feat_stds = [
            1.7098, 0.7784, 1.1714, 1.4683, 0.0525, 0.0964,
            0.0161, 0.0892, 1.8728, 1.8845, 2.0658, 1.5550, 0.0726,
        ];
        let c: BmuConfigC = (&rust).into();
        let back: Config = (&c).into();
        assert_eq!(back, rust);
    }
}
