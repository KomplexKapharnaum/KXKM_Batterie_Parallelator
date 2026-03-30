//! Battery protection state machine (F01-F08) — pure no_std logic.
//! No I/O, no mutex, no heap. Takes readings → returns actions.
//! Testable on host without any hardware or OS.

#![no_std]

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
pub enum BatteryState {
    Connected,
    Disconnected,
    Reconnecting,
    Error,
    Locked,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
pub enum BatteryAction {
    None,
    SwitchOn,
    SwitchOff,
    EmergencyOff,
    PermanentLock,
}

#[derive(Debug, Clone)]
pub struct ProtectionConfig {
    pub min_voltage_mv: u16,
    pub max_voltage_mv: u16,
    pub max_current_ma: u16,
    pub voltage_diff_mv: u16,
    pub reconnect_delay_ms: u32,
    pub nb_switch_max: u8,
    pub overcurrent_factor: u16, // x1000 (2000 = 2.0x)
}

impl Default for ProtectionConfig {
    fn default() -> Self {
        Self {
            min_voltage_mv: 24000,
            max_voltage_mv: 30000,
            max_current_ma: 10000,
            voltage_diff_mv: 1000,
            reconnect_delay_ms: 10000,
            nb_switch_max: 5,
            overcurrent_factor: 2000,
        }
    }
}

pub struct Protection<const N: usize> {
    config: ProtectionConfig,
    voltages_mv: [f32; N],
    nb_switch: [u8; N],
    reconnect_time_ms: [u64; N],
    state: [BatteryState; N],
}

impl<const N: usize> Protection<N> {
    pub fn new(config: ProtectionConfig) -> Self {
        Self {
            config,
            voltages_mv: [0.0; N],
            nb_switch: [0; N],
            reconnect_time_ms: [0; N],
            state: [BatteryState::Disconnected; N],
        }
    }

    /// Core state machine. Returns the action to execute.
    /// v_mv: bus voltage in millivolts
    /// i_a: current in amps (positive = discharge)
    /// now_ms: monotonic milliseconds since boot
    pub fn check_battery(&mut self, idx: usize, v_mv: f32, i_a: f32, now_ms: u64) -> BatteryAction {
        if idx >= N {
            return BatteryAction::None;
        }

        // Cache voltage
        self.voltages_mv[idx] = v_mv;

        // Overcurrent ERROR: |I| > factor * max_current
        let overcurrent_a = (self.config.overcurrent_factor as f32 / 1000.0)
            * (self.config.max_current_ma as f32 / 1000.0);

        if v_mv < 0.0 || abs_f32(i_a) > overcurrent_a {
            self.state[idx] = BatteryState::Error;
            return BatteryAction::EmergencyOff;
        }

        // No voltage
        if v_mv < 1000.0 {
            self.state[idx] = BatteryState::Disconnected;
            return BatteryAction::SwitchOff;
        }

        // Permanent lock (F08)
        if self.nb_switch[idx] > self.config.nb_switch_max {
            self.state[idx] = BatteryState::Locked;
            return BatteryAction::PermanentLock;
        }

        // Protection checks
        let v_ok = v_mv >= self.config.min_voltage_mv as f32
            && v_mv <= self.config.max_voltage_mv as f32;
        let i_ok = abs_f32(i_a) <= self.config.max_current_ma as f32 / 1000.0;
        let fleet_max = self.fleet_max_mv();
        let imbalance_ok = (fleet_max - v_mv) <= self.config.voltage_diff_mv as f32;

        if !v_ok || !i_ok || !imbalance_ok {
            self.state[idx] = BatteryState::Disconnected;
            return BatteryAction::SwitchOff;
        }

        // Reconnection logic
        let nb = self.nb_switch[idx];
        let delay_elapsed = now_ms.saturating_sub(self.reconnect_time_ms[idx])
            > self.config.reconnect_delay_ms as u64;

        let should_reconnect = nb == 0
            || (nb < self.config.nb_switch_max && delay_elapsed)
            || (nb == self.config.nb_switch_max && delay_elapsed);

        if should_reconnect && v_ok && i_ok {
            self.state[idx] = BatteryState::Reconnecting;
            self.nb_switch[idx] = nb.saturating_add(1);
            self.reconnect_time_ms[idx] = now_ms;
            return BatteryAction::SwitchOn;
        }

        self.state[idx] = BatteryState::Connected;
        BatteryAction::None
    }

    pub fn get_state(&self, idx: usize) -> BatteryState {
        if idx < N { self.state[idx] } else { BatteryState::Disconnected }
    }

    pub fn get_voltage_mv(&self, idx: usize) -> f32 {
        if idx < N { self.voltages_mv[idx] } else { 0.0 }
    }

    pub fn reset_switch_count(&mut self, idx: usize) {
        if idx < N {
            self.nb_switch[idx] = 0;
            self.reconnect_time_ms[idx] = 0;
            self.state[idx] = BatteryState::Disconnected;
        }
    }

    fn fleet_max_mv(&self) -> f32 {
        let mut max = 0.0f32;
        for &v in &self.voltages_mv {
            if v > max { max = v; }
        }
        max
    }
}

fn abs_f32(x: f32) -> f32 {
    if x < 0.0 { -x } else { x }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn default_prot() -> Protection<16> {
        Protection::new(ProtectionConfig::default())
    }

    #[test]
    fn test_undervoltage_disconnects() {
        let mut p = default_prot();
        assert_eq!(p.check_battery(0, 23999.0, 0.0, 0), BatteryAction::SwitchOff);
    }

    #[test]
    fn test_nominal_voltage_connects() {
        let mut p = default_prot();
        assert_eq!(p.check_battery(0, 27000.0, 0.0, 0), BatteryAction::SwitchOn);
    }

    #[test]
    fn test_overvoltage_disconnects() {
        let mut p = default_prot();
        assert_eq!(p.check_battery(0, 30001.0, 0.0, 0), BatteryAction::SwitchOff);
    }

    #[test]
    fn test_overcurrent_positive_disconnects() {
        let mut p = default_prot();
        // Overcurrent = 2.0 * 10A = 20A
        assert_eq!(p.check_battery(0, 27000.0, 20.1, 0), BatteryAction::EmergencyOff);
    }

    #[test]
    fn test_overcurrent_negative_disconnects() {
        let mut p = default_prot();
        assert_eq!(p.check_battery(0, 27000.0, -20.1, 0), BatteryAction::EmergencyOff);
    }

    #[test]
    fn test_nominal_current_connects() {
        let mut p = default_prot();
        assert_eq!(p.check_battery(0, 27000.0, 5.0, 0), BatteryAction::SwitchOn);
    }

    #[test]
    fn test_voltage_imbalance_disconnects() {
        let mut p = default_prot();
        // Set fleet max at 27000
        p.voltages_mv[1] = 27000.0;
        // Battery 0 at 25000 -> diff=2000 > 1000 -> disconnect
        assert_eq!(p.check_battery(0, 25000.0, 0.0, 0), BatteryAction::SwitchOff);
    }

    #[test]
    fn test_voltage_imbalance_within_threshold() {
        let mut p = default_prot();
        p.voltages_mv[1] = 27000.0;
        // Battery 0 at 26500 -> diff=500 < 1000 -> connect
        assert_eq!(p.check_battery(0, 26500.0, 0.0, 0), BatteryAction::SwitchOn);
    }

    #[test]
    fn test_permanent_lock() {
        let mut p = default_prot();
        p.nb_switch[0] = 6; // > 5
        assert_eq!(p.check_battery(0, 27000.0, 0.0, 0), BatteryAction::PermanentLock);
    }

    #[test]
    fn test_no_lock_at_max() {
        let mut p = default_prot();
        p.nb_switch[0] = 5; // == max, should reconnect with delay
        assert_eq!(p.check_battery(0, 27000.0, 0.0, 20000), BatteryAction::SwitchOn);
    }
}
