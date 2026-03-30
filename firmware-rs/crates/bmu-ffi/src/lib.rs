//! C FFI bridge for Rust protection state machine.
//! Exports extern "C" functions callable from ESP-IDF C code.

#![no_std]

use bmu_protection::{BatteryAction, BatteryState, Protection, ProtectionConfig};
use core::ptr;

/// FFI-safe battery action enum
#[repr(C)]
pub enum BmuAction {
    None = 0,
    SwitchOn = 1,
    SwitchOff = 2,
    EmergencyOff = 3,
    PermanentLock = 4,
}

/// FFI-safe battery state enum
#[repr(C)]
pub enum BmuState {
    Connected = 0,
    Disconnected = 1,
    Reconnecting = 2,
    Error = 3,
    Locked = 4,
}

/// FFI-safe protection config
#[repr(C)]
pub struct BmuProtectionConfig {
    pub min_voltage_mv: u16,
    pub max_voltage_mv: u16,
    pub max_current_ma: u16,
    pub voltage_diff_mv: u16,
    pub reconnect_delay_ms: u32,
    pub nb_switch_max: u8,
    pub overcurrent_factor: u16,
}

fn action_to_ffi(a: BatteryAction) -> BmuAction {
    match a {
        BatteryAction::None => BmuAction::None,
        BatteryAction::SwitchOn => BmuAction::SwitchOn,
        BatteryAction::SwitchOff => BmuAction::SwitchOff,
        BatteryAction::EmergencyOff => BmuAction::EmergencyOff,
        BatteryAction::PermanentLock => BmuAction::PermanentLock,
    }
}

fn state_to_ffi(s: BatteryState) -> BmuState {
    match s {
        BatteryState::Connected => BmuState::Connected,
        BatteryState::Disconnected => BmuState::Disconnected,
        BatteryState::Reconnecting => BmuState::Reconnecting,
        BatteryState::Error => BmuState::Error,
        BatteryState::Locked => BmuState::Locked,
    }
}

/// Create protection context using a static buffer (no heap).
/// Only one instance supported (single BMU per device).
/// Returns null on invalid config.
#[no_mangle]
pub extern "C" fn bmu_protection_new(config: *const BmuProtectionConfig) -> *mut Protection<16> {
    if config.is_null() {
        return ptr::null_mut();
    }
    let cfg = unsafe { &*config };
    let prot = Protection::<16>::new(ProtectionConfig {
        min_voltage_mv: cfg.min_voltage_mv,
        max_voltage_mv: cfg.max_voltage_mv,
        max_current_ma: cfg.max_current_ma,
        voltage_diff_mv: cfg.voltage_diff_mv,
        reconnect_delay_ms: cfg.reconnect_delay_ms,
        nb_switch_max: cfg.nb_switch_max,
        overcurrent_factor: cfg.overcurrent_factor,
    });
    // Use a static buffer — no heap allocation in no_std
    unsafe {
        static mut PROT: core::mem::MaybeUninit<Protection<16>> = core::mem::MaybeUninit::uninit();
        PROT.write(prot);
        PROT.as_mut_ptr()
    }
}

/// Run one protection check for battery idx.
#[no_mangle]
pub extern "C" fn bmu_protection_check(
    ctx: *mut Protection<16>,
    idx: u8,
    v_mv: f32,
    i_a: f32,
    now_ms: u64,
) -> BmuAction {
    if ctx.is_null() {
        return BmuAction::None;
    }
    let prot = unsafe { &mut *ctx };
    action_to_ffi(prot.check_battery(idx as usize, v_mv, i_a, now_ms))
}

/// Get current state of a battery.
#[no_mangle]
pub extern "C" fn bmu_protection_get_state(ctx: *const Protection<16>, idx: u8) -> BmuState {
    if ctx.is_null() {
        return BmuState::Disconnected;
    }
    let prot = unsafe { &*ctx };
    state_to_ffi(prot.get_state(idx as usize))
}

/// Get cached voltage for a battery (mV).
#[no_mangle]
pub extern "C" fn bmu_protection_get_voltage(ctx: *const Protection<16>, idx: u8) -> f32 {
    if ctx.is_null() {
        return 0.0;
    }
    let prot = unsafe { &*ctx };
    prot.get_voltage_mv(idx as usize)
}

/// Reset switch count for a battery.
#[no_mangle]
pub extern "C" fn bmu_protection_reset(ctx: *mut Protection<16>, idx: u8) {
    if ctx.is_null() {
        return;
    }
    let prot = unsafe { &mut *ctx };
    prot.reset_switch_count(idx as usize);
}

// Panic handler required for no_std
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
