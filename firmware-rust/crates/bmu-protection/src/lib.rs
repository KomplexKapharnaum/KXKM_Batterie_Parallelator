//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]

pub mod checks;
pub mod latch;
pub mod manager;
pub mod state;

pub use manager::{integrate_charge, topology_ok, BatteryManager};
pub use state::{transition, Measurement, Transition, TransitionContext};
