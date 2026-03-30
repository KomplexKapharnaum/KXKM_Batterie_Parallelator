//! BMU I2C bus abstraction — placeholder for esp-hal integration.
//! In the hybrid architecture, the I2C bus is initialized by ESP-IDF C code
//! and passed to Rust via FFI. This crate provides the trait bounds.

#![no_std]

// Re-export embedded-hal I2C trait for use by driver crates
pub use embedded_hal::i2c::I2c;
pub use embedded_hal::i2c::ErrorType;
