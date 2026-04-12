//! Types partagés du core BMU. Zéro dépendance externe.
//! Cf spec §4.1 "`bmu-types`" — fondation type-safe (CRIT-A by-design).
#![no_std]

pub mod action;
pub mod command;
pub mod config;
pub mod snapshot;
pub mod state;
pub mod units;

pub use action::Actions;
pub use command::Command;
pub use config::{Config, ConfigError, NUM_SOH_FEATURES};
pub use snapshot::{Battery, Snapshot, System, MAX_BATTERIES};
pub use state::{BatteryState, LatchReason, OfflineReason};
pub use units::{MilliampHours, Milliamps, Milliohms, Millivolts};
