//! Façade FFI C exposée comme `staticlib` `libbmu_core.a` + `rlib`.
//! Cf spec §3.4.
//!
//! Task 9.1 : types `#[repr(C)]` avec conversions `From`.
//! Task 9.2 : runtime `BmuCore` avec `step`, `handle_command`.
//! Task 9.3 : façade `extern "C"` + `panic_handler` + `build.rs` `cbindgen`.

#![cfg_attr(all(not(test), target_os = "none"), no_std)]

pub mod core_impl;
pub mod ffi_types;

pub use core_impl::{BmuCore, CoreError, ParsedInputs};
