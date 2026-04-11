//! Façade FFI C exposée comme `staticlib` `libbmu_core.a` + `rlib` + header `bmu_core.h`.
//! Cf spec §3.4.
//!
//! Task 9.1 : types `#[repr(C)]` avec conversions `From`.
//! Task 9.2 : runtime `BmuCore` avec `step`, `handle_command`.
//! Task 9.3 : façade `extern "C"` + `panic_handler` + `build.rs` `cbindgen`.

#![cfg_attr(all(not(test), target_os = "none"), no_std)]

// `alloc` est nécessaire pour `Box::into_raw` (allocation opaque du `BmuCore`).
// Sur host (tests/clippy), `std` fournit déjà tout, mais le `extern crate`
// explicite est accepté en no-op. Sur `xtensa`, un `#[global_allocator]` doit
// être fourni par le binaire final (`esp-alloc` via `esp-idf-sys` en Part 2).
extern crate alloc;

pub mod core_impl;
pub mod ffi_types;

pub use core_impl::{BmuCore, CoreError, ParsedInputs};

/// Allocateur bump minimal pour la cross-compile `xtensa` standalone en Part 1.
///
/// **Temporaire** : Part 2 le remplacera par un allocateur basé sur
/// `esp_idf_sys::heap_caps_malloc`. Cet allocateur n'est compilé QUE pour
/// target `xtensa-esp32s3-none-elf` (`cfg(target_os = "none")` hors tests) ;
/// sur host les tests utilisent le `std` allocator natif.
///
/// Caractéristiques :
/// - 16 `KiB` de heap statique (suffit largement pour 1 `BmuCore` ≈ 1 `KiB`).
/// - `compare_exchange` atomique pour la thread-safety (bien que `BmuCore`
///   soit utilisé single-threaded dans le runtime `ESP-IDF`).
/// - Aucun `dealloc` : la seule allocation est le `BmuCore` via
///   `Box::into_raw` au boot, et `bmu_core_destroy` ne devrait jamais être
///   appelé sur un système embarqué à instance unique.
#[cfg(all(not(test), target_os = "none"))]
#[allow(clippy::arithmetic_side_effects)]
mod bump_alloc {
    use core::alloc::{GlobalAlloc, Layout};
    use core::cell::UnsafeCell;
    use core::sync::atomic::{AtomicUsize, Ordering};

    const HEAP_SIZE: usize = 16 * 1024;

    struct Heap {
        data: UnsafeCell<[u8; HEAP_SIZE]>,
        offset: AtomicUsize,
    }

    // SAFETY: accès concurrent protégé par `compare_exchange` atomique
    // sur `offset`. Le tableau `data` n'est lu qu'après que son offset
    // ait été réservé par un seul thread.
    unsafe impl Sync for Heap {}

    unsafe impl GlobalAlloc for Heap {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            let align = layout.align();
            let size = layout.size();
            loop {
                let current = self.offset.load(Ordering::Relaxed);
                let aligned = (current + align - 1) & !(align - 1);
                let new_offset = aligned + size;
                if new_offset > HEAP_SIZE {
                    return core::ptr::null_mut();
                }
                if self
                    .offset
                    .compare_exchange(current, new_offset, Ordering::AcqRel, Ordering::Relaxed)
                    .is_ok()
                {
                    // SAFETY: `aligned < HEAP_SIZE` garanti par la check plus haut ;
                    // `data` est un `UnsafeCell<[u8; HEAP_SIZE]>` donc `add(aligned)`
                    // est dans les bornes du tableau.
                    return unsafe { (*self.data.get()).as_mut_ptr().add(aligned) };
                }
            }
        }

        unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
            // Bump allocator ne libère rien — OK pour Part 1 (instance unique).
        }
    }

    #[global_allocator]
    static HEAP: Heap = Heap {
        data: UnsafeCell::new([0u8; HEAP_SIZE]),
        offset: AtomicUsize::new(0),
    };
}

use crate::ffi_types::{
    BmuActionsC, BmuCommandC, BmuConfigC, BmuRawInputs, BmuSnapshotC, BMU_ERR_BUSY,
    BMU_ERR_INVALID_CONFIG, BMU_ERR_INVALID_INDEX, BMU_ERR_NULL, BMU_ERR_UNSUPPORTED, BMU_OK,
};
use bmu_types::{Command, Config};

/// `Panic` handler requis par `#![no_std]` sur target `xtensa`.
/// Sur host (tests/clippy), `std` fournit son propre handler.
#[cfg(all(not(test), target_os = "none"))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// Alloue un `BmuCore` et retourne un handle opaque. Retourne `NULL` si `cfg` est
/// `NULL` ou si la config échoue la validation.
///
/// # Safety
/// Le caller doit appeler `bmu_core_destroy` exactement une fois pour libérer.
/// `cfg` doit pointer sur un `BmuConfigC` valide.
///
/// **Exclusion mutuelle** : `BmuCore` n'est PAS protégé contre les accès
/// concurrents. Toutes les fonctions opérant sur un même handle (`tick`,
/// `command`, `get_cached_snapshot`, `set_config`, `serialize_battery`,
/// `destroy`) doivent être appelées depuis une seule tâche à la fois.
/// L'appelant est responsable de la synchronisation inter-tâches
/// (ex : `xSemaphoreTake` côté `ESP-IDF` avant chaque appel). La convention
/// KXKM est que seul `task_protection` possède le handle ; les autres
/// tâches (`task_ble`, `task_display`, `task_soh`) appellent exclusivement
/// `get_cached_snapshot` et `serialize_battery` derrière un mutex.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_init(cfg: *const BmuConfigC) -> *mut BmuCore {
    if cfg.is_null() {
        return core::ptr::null_mut();
    }
    // SAFETY: cfg vient d'être vérifié non-null ; le caller garantit validité.
    let c_cfg: &BmuConfigC = unsafe { &*cfg };
    let rust_cfg: Config = c_cfg.into();
    if rust_cfg.validate().is_err() {
        return core::ptr::null_mut();
    }
    let boxed = alloc::boxed::Box::new(BmuCore::new(rust_cfg));
    alloc::boxed::Box::into_raw(boxed)
}

/// Libère un `BmuCore` alloué par `bmu_core_init`.
///
/// # Safety
/// `core` doit être un pointeur retourné par `bmu_core_init`, non-`NULL`, et
/// non encore libéré. Après cet appel, le pointeur ne doit plus être utilisé.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_destroy(core: *mut BmuCore) {
    if core.is_null() {
        return;
    }
    // SAFETY: caller garantit que `core` vient de `bmu_core_init` et n'a pas été libéré.
    drop(unsafe { alloc::boxed::Box::from_raw(core) });
}

/// Exécute un tick core : parse `BmuRawInputs`, appelle `step()`, écrit snapshot et actions.
///
/// # Safety
/// `core`, `inputs`, `out_snapshot`, `out_actions` doivent être non-`NULL` et pointer
/// sur des objets valides et correctement alignés. `core` doit être un handle actif.
#[no_mangle]
#[allow(clippy::indexing_slicing, clippy::cast_possible_truncation)]
pub unsafe extern "C" fn bmu_core_tick(
    core: *mut BmuCore,
    inputs: *const BmuRawInputs,
    out_snapshot: *mut BmuSnapshotC,
    out_actions: *mut BmuActionsC,
) -> i32 {
    if core.is_null() || inputs.is_null() || out_snapshot.is_null() || out_actions.is_null() {
        return BMU_ERR_NULL;
    }
    // SAFETY: pointeur vérifié non-null.
    let core = unsafe { &mut *core };
    // SAFETY: pointeur vérifié non-null.
    let inputs = unsafe { &*inputs };

    let mut parsed = ParsedInputs {
        n_ina: inputs.n_ina,
        n_tca: inputs.n_tca,
        monotonic_us: inputs.monotonic_us,
        ..ParsedInputs::default()
    };

    // Parse chaque `INA237`. Convention du caller C :
    // - `ina_registers[i][0..2]` = `VBUS` big-endian
    // - `ina_registers[i][2..4]` = `CURRENT` big-endian
    //
    // `current_lsb_na` hardcodé sur 100 A max (dimensionnement KXKM hardware).
    // **CRITIQUE** : cette valeur doit être cohérente avec le `SHUNT_CAL`
    // encodé côté C via `encode_shunt_cal(shunt_uΩ, 100_000)`. Toute
    // divergence silencieuse produit des mesures de courant erronées et
    // désarme l'over-current protection F05.
    // TODO Part 2 : passer `max_current_ma` via `BmuConfigC` pour éviter le
    // hardcode et synchroniser automatiquement avec la calibration C.
    let lsb = bmu_drivers::ina237::current_lsb_na(100_000);
    for i in 0..(inputs.n_ina as usize).min(16) {
        let regs = &inputs.ina_registers[i];
        let vbus_bytes = [regs[0], regs[1]];
        let current_bytes = [regs[2], regs[3]];
        parsed.measurements[i].voltage = bmu_drivers::ina237::parse_vbus(vbus_bytes);
        parsed.measurements[i].current = bmu_drivers::ina237::parse_current(current_bytes, lsb);
    }

    let (snap, actions) = core.step(&parsed);

    // SAFETY: out_snapshot vérifié non-null.
    unsafe {
        *out_snapshot = BmuSnapshotC::from(&snap);
    }
    // SAFETY: out_actions vérifié non-null.
    unsafe {
        *out_actions = BmuActionsC {
            tca_set_mask: actions.tca_set_mask,
            tca_clr_mask: actions.tca_clr_mask,
            rint_trigger_idx: actions.rint_trigger_idx,
            request_soh_inference: u8::from(actions.request_soh_inference),
        };
    }

    BMU_OK
}

/// Reçoit une commande depuis `BLE`/`UI`.
///
/// # Safety
/// `core` et `cmd` doivent être non-`NULL` et valides.
#[no_mangle]
#[allow(clippy::indexing_slicing)]
pub unsafe extern "C" fn bmu_core_command(core: *mut BmuCore, cmd: *const BmuCommandC) -> i32 {
    if core.is_null() || cmd.is_null() {
        return BMU_ERR_NULL;
    }
    // SAFETY: pointeur vérifié non-null.
    let core = unsafe { &mut *core };
    // SAFETY: pointeur vérifié non-null.
    let cmd = unsafe { &*cmd };

    let rust_cmd = match cmd.kind {
        0 => Command::None,
        1 => Command::ForceOff {
            idx: cmd.target_idx,
        },
        2 => Command::ResetAh {
            idx: cmd.target_idx,
        },
        3 => Command::TriggerRint {
            idx: cmd.target_idx,
        },
        4 => Command::ResetLatch {
            idx: cmd.target_idx,
        },
        6 => {
            if cmd.payload.len() < core::mem::size_of::<BmuConfigC>() {
                return BMU_ERR_INVALID_CONFIG;
            }
            #[allow(clippy::cast_ptr_alignment)]
            let cfg_ptr = cmd.payload.as_ptr().cast::<BmuConfigC>();
            // SAFETY: `read_unaligned` évite les exigences d'alignement ; taille vérifiée.
            let c_cfg = unsafe { core::ptr::read_unaligned(cfg_ptr) };
            Command::SetConfig((&c_cfg).into())
        }
        7 => Command::UpdateSoh {
            idx: cmd.target_idx,
            soh_pct: cmd.payload[0],
        },
        8 => Command::TopologyChanged {
            n_ina: cmd.payload[0],
            n_tca: cmd.payload[1],
        },
        _ => return BMU_ERR_UNSUPPORTED,
    };

    match core.handle_command(rust_cmd) {
        Ok(()) => BMU_OK,
        Err(CoreError::InvalidIndex) => BMU_ERR_INVALID_INDEX,
        Err(CoreError::InvalidConfig) => BMU_ERR_INVALID_CONFIG,
        Err(CoreError::RintBusy) => BMU_ERR_BUSY,
    }
}

/// Copie le dernier snapshot caché vers `out`. Utilisé par `task_ble` / `task_display` / `task_soh`.
///
/// # Safety
/// `core` et `out` doivent être non-`NULL` et valides.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_get_cached_snapshot(
    core: *const BmuCore,
    out: *mut BmuSnapshotC,
) -> i32 {
    if core.is_null() || out.is_null() {
        return BMU_ERR_NULL;
    }
    // SAFETY: pointeur vérifié non-null.
    let core = unsafe { &*core };
    // SAFETY: pointeur vérifié non-null.
    unsafe {
        *out = BmuSnapshotC::from(core.cached_snapshot());
    }
    BMU_OK
}

/// Applique une nouvelle config (avec validation bornes).
///
/// # Safety
/// Les deux pointeurs doivent être valides et non-`NULL`.
#[no_mangle]
pub unsafe extern "C" fn bmu_core_set_config(core: *mut BmuCore, cfg: *const BmuConfigC) -> i32 {
    if core.is_null() || cfg.is_null() {
        return BMU_ERR_NULL;
    }
    // SAFETY: pointeur vérifié non-null.
    let core = unsafe { &mut *core };
    // SAFETY: pointeur vérifié non-null.
    let c_cfg: &BmuConfigC = unsafe { &*cfg };
    let rust_cfg: Config = c_cfg.into();
    match core.set_config(rust_cfg) {
        Ok(()) => BMU_OK,
        Err(_) => BMU_ERR_INVALID_CONFIG,
    }
}

/// Sérialise la batterie `idx` en 24 bytes packed big-endian pour la characteristic `BLE`.
/// Cf spec §7.2 layout.
///
/// Layout : `[idx, state, reason, switch_count, voltage_mv_be32, current_ma_be32,
/// ah_remaining_be32, temp_c10_be16, soh_pct, balancer_duty_pct, r_ohmic_be32]`
/// = 1 + 1 + 1 + 1 + 4 + 4 + 4 + 2 + 1 + 1 + 4 = 24 bytes.
///
/// # Safety
/// `core` doit être non-`NULL` et valide. `out_buf` doit pointer sur au moins
/// 24 bytes writable et correctement alignés pour `u8`.
#[no_mangle]
#[allow(clippy::indexing_slicing, clippy::cast_possible_truncation)]
pub unsafe extern "C" fn bmu_core_serialize_battery(
    core: *const BmuCore,
    idx: u8,
    out_buf: *mut u8,
) -> i32 {
    if core.is_null() || out_buf.is_null() {
        return BMU_ERR_NULL;
    }
    if (idx as usize) >= bmu_types::MAX_BATTERIES {
        return BMU_ERR_INVALID_INDEX;
    }
    // SAFETY: pointeur vérifié non-null.
    let core = unsafe { &*core };
    let b = &core.cached_snapshot().batteries[idx as usize];
    // SAFETY: caller garantit ≥24 bytes writable.
    let buf = unsafe { core::slice::from_raw_parts_mut(out_buf, 24) };
    buf[0] = b.idx;
    buf[1] = b.state as u8;
    buf[2] = b.reason as u8;
    buf[3] = b.switch_count;
    buf[4..8].copy_from_slice(&b.voltage.as_raw().to_be_bytes());
    buf[8..12].copy_from_slice(&b.current.as_raw().to_be_bytes());
    buf[12..16].copy_from_slice(&b.ah_remaining.as_raw().to_be_bytes());
    buf[16..18].copy_from_slice(&b.temp_c10.to_be_bytes());
    buf[18] = b.soh_pct;
    buf[19] = b.balancer_duty_pct;
    buf[20..24].copy_from_slice(&b.r_ohmic.as_raw().to_be_bytes());
    BMU_OK
}
