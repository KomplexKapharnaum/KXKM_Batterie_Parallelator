//! State machine protection F01-F11 + battery manager (Ah counting).
#![no_std]

pub mod checks;
pub mod latch;
pub mod state;

pub use state::{transition, Measurement, Transition, TransitionContext};
