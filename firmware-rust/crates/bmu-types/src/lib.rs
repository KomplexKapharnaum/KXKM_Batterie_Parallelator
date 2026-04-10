//! Types partagés du core BMU. Zéro dépendance externe.
//! Cf spec §4.1 "`bmu-types`" — fondation type-safe (CRIT-A by-design).
#![no_std]

pub mod state;
pub mod units;

pub use state::{BatteryState, LatchReason, OfflineReason};
pub use units::{MilliampHours, Milliamps, Milliohms, Millivolts};
