//! Types partagés du core BMU. Zéro dépendance externe.
//! Cf spec §4.1 "`bmu-types`" — fondation type-safe (CRIT-A by-design).
#![no_std]

pub mod units;

pub use units::{MilliampHours, Milliamps, Milliohms, Millivolts};
