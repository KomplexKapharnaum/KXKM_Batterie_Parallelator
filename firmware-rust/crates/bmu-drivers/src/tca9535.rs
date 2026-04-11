//! Driver `TCA9535` — 16-bit I²C `GPIO` expander.
//! Référence : TI `SCPS209` datasheet.
//! PCB BMU v2 : ordre des switchs inversé par canal, `LEDs` appairées.

#![allow(clippy::module_name_repetitions)]

use bmu_i2c::{I2cBus, I2cError};

pub const TCA9535_ADDR_BASE: u8 = 0x20;
pub const TCA9535_ADDR_MAX: u8 = 0x27;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    InputPort0 = 0x00,
    InputPort1 = 0x01,
    OutputPort0 = 0x02,
    OutputPort1 = 0x03,
    PolarityInv0 = 0x04,
    PolarityInv1 = 0x05,
    ConfigPort0 = 0x06,
    ConfigPort1 = 0x07,
}

/// PCB BMU v2 mapping :
/// - `P0` bits 0-3 = output (switches `MOSFET` batteries), bits 4-7 = input (alerts)
/// - `P1` bits 0-7 = output (`LEDs` rouge/verte par canal, appairées)
pub const P0_CONFIG: u8 = 0xF0;
pub const P1_CONFIG: u8 = 0x00;

/// Mapping batterie → pin output port 0 (REVERSED sur PCB v2) :
/// canal 0 (bat1) = `P0`.3, canal 1 = `P0`.2, canal 2 = `P0`.1, canal 3 = `P0`.0
pub const SWITCH_PIN: [u8; 4] = [3, 2, 1, 0];

/// Mapping batterie → pin input port 0 pour alerts (REVERSED) :
/// canal 0 = `P0`.7, canal 1 = `P0`.6, canal 2 = `P0`.5, canal 3 = `P0`.4
pub const ALERT_PIN: [u8; 4] = [7, 6, 5, 4];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tca9535Error {
    I2c(I2cError),
    InvalidChannel,
}

impl From<I2cError> for Tca9535Error {
    fn from(e: I2cError) -> Self {
        Self::I2c(e)
    }
}

/// Parse les 2 bytes des `INPUT_PORT0` et `INPUT_PORT1` en `u16`.
/// Convention : bits 0..7 = port 0 (`LSB`), bits 8..15 = port 1.
#[inline]
#[must_use]
pub fn parse_inputs(raw: [u8; 2]) -> u16 {
    u16::from(raw[0]) | (u16::from(raw[1]) << 8)
}

/// True si un alert est actif pour ce canal (active-low sur `P0`).
/// Canaux hors 0..3 retournent false.
#[inline]
#[must_use]
pub fn channel_alert_active(port0: u8, channel: u8) -> bool {
    if channel >= 4 {
        return false;
    }
    #[allow(clippy::indexing_slicing)]
    let pin = ALERT_PIN[channel as usize];
    (port0 >> pin) & 1 == 0
}

/// Modifie le bit output du canal dans `current_p0` (non persistant).
/// Canal invalide (>=4) → pas de changement.
#[inline]
#[must_use]
pub fn set_switch_bit(current_p0: u8, channel: u8, on: bool) -> u8 {
    if channel >= 4 {
        return current_p0;
    }
    #[allow(clippy::indexing_slicing)]
    let pin = SWITCH_PIN[channel as usize];
    if on {
        current_p0 | (1u8 << pin)
    } else {
        current_p0 & !(1u8 << pin)
    }
}

/// Encode la paire `LED` rouge/verte d'un canal dans le byte `OUTPUT_PORT1`.
/// Canal 0 : red=`P1`.0 green=`P1`.1 ; canal 3 : red=`P1`.6 green=`P1`.7.
/// Canal invalide → `current_p1` inchangé.
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub fn encode_led_pair(current_p1: u8, channel: u8, red: bool, green: bool) -> u8 {
    if channel >= 4 {
        return current_p1;
    }
    let red_pin: u8 = channel * 2;
    let green_pin: u8 = channel * 2 + 1;
    let mut out = current_p1;
    out &= !(1u8 << red_pin);
    out &= !(1u8 << green_pin);
    if red {
        out |= 1u8 << red_pin;
    }
    if green {
        out |= 1u8 << green_pin;
    }
    out
}

/// Wrapper bus-aware stateful. `out_p0` et `out_p1` suivent l'état courant.
///
/// ATTENTION : `out_p0`/`out_p1` sont un cache write-only. En cas de reset
/// matériel externe (brownout, glitch), le cache diverge du registre hardware
/// sans détection. Appeler `all_off()` avant tout `switch_battery()`/`set_led()`
/// post-fault pour resynchroniser avec le hardware.
#[derive(Debug, Clone, Copy)]
pub struct Tca9535 {
    addr: u8,
    out_p0: u8,
    out_p1: u8,
}

impl Tca9535 {
    /// Init complet : outputs OFF puis config directions puis polarity clear.
    ///
    /// # Errors
    /// Propage `I2c` si une transaction bus échoue.
    pub fn init<B: I2cBus>(bus: &mut B, addr: u8) -> Result<Self, Tca9535Error> {
        bus.write(addr, &[Reg::OutputPort0 as u8, 0x00])?;
        bus.write(addr, &[Reg::OutputPort1 as u8, 0x00])?;
        bus.write(addr, &[Reg::ConfigPort0 as u8, P0_CONFIG])?;
        bus.write(addr, &[Reg::ConfigPort1 as u8, P1_CONFIG])?;
        bus.write(addr, &[Reg::PolarityInv0 as u8, 0x00])?;
        bus.write(addr, &[Reg::PolarityInv1 as u8, 0x00])?;
        Ok(Self {
            addr,
            out_p0: 0x00,
            out_p1: 0x00,
        })
    }

    /// Construit une instance sans faire les writes d'init.
    /// Test-only helper.
    #[cfg(test)]
    #[must_use]
    pub fn new_without_init(addr: u8) -> Self {
        Self {
            addr,
            out_p0: 0x00,
            out_p1: 0x00,
        }
    }

    /// Adresse I²C.
    #[must_use]
    pub const fn addr(&self) -> u8 {
        self.addr
    }

    /// Commute une batterie ON ou OFF. Canal 0..3. Écrit immédiatement `OUTPUT_PORT0`.
    ///
    /// # Errors
    /// `InvalidChannel` si `channel >= 4`, sinon propage `I2c`.
    pub fn switch_battery<B: I2cBus>(
        &mut self,
        bus: &mut B,
        channel: u8,
        on: bool,
    ) -> Result<(), Tca9535Error> {
        if channel >= 4 {
            return Err(Tca9535Error::InvalidChannel);
        }
        self.out_p0 = set_switch_bit(self.out_p0, channel, on);
        bus.write(self.addr, &[Reg::OutputPort0 as u8, self.out_p0])?;
        Ok(())
    }

    /// Définit la paire `LED` rouge/verte d'un canal. Écrit immédiatement `OUTPUT_PORT1`.
    ///
    /// # Errors
    /// `InvalidChannel` si `channel >= 4`, sinon propage `I2c`.
    pub fn set_led<B: I2cBus>(
        &mut self,
        bus: &mut B,
        channel: u8,
        red: bool,
        green: bool,
    ) -> Result<(), Tca9535Error> {
        if channel >= 4 {
            return Err(Tca9535Error::InvalidChannel);
        }
        self.out_p1 = encode_led_pair(self.out_p1, channel, red, green);
        bus.write(self.addr, &[Reg::OutputPort1 as u8, self.out_p1])?;
        Ok(())
    }

    /// **Fix bug scaffold firmware-rs :** écrit EXPLICITEMENT les deux output ports.
    /// Le `TCA9535` ne supporte pas l'auto-increment de registre → il faut 2
    /// transactions distinctes. Cf §4.3 spec "Bug potentiel identifié dans
    /// le scaffold actuel".
    ///
    /// # Errors
    /// Propage `I2c` si une transaction bus échoue.
    pub fn all_off<B: I2cBus>(&mut self, bus: &mut B) -> Result<(), Tca9535Error> {
        self.out_p0 = 0x00;
        self.out_p1 = 0x00;
        bus.write(self.addr, &[Reg::OutputPort0 as u8, 0x00])?;
        bus.write(self.addr, &[Reg::OutputPort1 as u8, 0x00])?;
        Ok(())
    }

    /// Lit `INPUT_PORT0` et retourne un tableau d'alerts par canal (active-low).
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn read_alerts<B: I2cBus>(&self, bus: &mut B) -> Result<[bool; 4], Tca9535Error> {
        let mut buf = [0u8; 1];
        bus.write_read(self.addr, &[Reg::InputPort0 as u8], &mut buf)?;
        let port0 = buf[0];
        Ok([
            channel_alert_active(port0, 0),
            channel_alert_active(port0, 1),
            channel_alert_active(port0, 2),
            channel_alert_active(port0, 3),
        ])
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::panic)]
mod tests {
    extern crate std;
    use std::vec;

    use super::*;
    use bmu_test_fixtures::MockBus;

    #[test]
    fn parse_inputs_all_low() {
        assert_eq!(parse_inputs([0x00, 0x00]), 0x0000);
    }

    #[test]
    fn parse_inputs_all_high() {
        assert_eq!(parse_inputs([0xFF, 0xFF]), 0xFFFF);
    }

    #[test]
    fn parse_inputs_endianness() {
        // port 0 = LSB, port 1 = MSB
        assert_eq!(parse_inputs([0x12, 0x34]), 0x3412);
    }

    #[test]
    fn channel_alert_active_low_mapping() {
        // bit 4 low → alert TRUE pour canal 3 (ALERT_PIN[3]=4)
        let port0 = 0b1110_1111;
        assert!(channel_alert_active(port0, 3));
        assert!(!channel_alert_active(port0, 0));
        assert!(!channel_alert_active(port0, 1));
        assert!(!channel_alert_active(port0, 2));
    }

    #[test]
    fn channel_alert_canal_0() {
        // ALERT_PIN[0] = 7 → bit 7 low
        let port0 = 0b0111_1111;
        assert!(channel_alert_active(port0, 0));
    }

    #[test]
    fn set_switch_bit_reversed_mapping() {
        assert_eq!(set_switch_bit(0x00, 0, true), 0b0000_1000);
        assert_eq!(set_switch_bit(0x00, 1, true), 0b0000_0100);
        assert_eq!(set_switch_bit(0x00, 3, true), 0b0000_0001);
    }

    #[test]
    fn set_switch_bit_clear() {
        assert_eq!(set_switch_bit(0b0000_1111, 0, false), 0b0000_0111);
        assert_eq!(set_switch_bit(0b0000_1111, 3, false), 0b0000_1110);
    }

    #[test]
    fn set_switch_bit_invalid_channel_noop() {
        assert_eq!(set_switch_bit(0xAA, 4, true), 0xAA);
        assert_eq!(set_switch_bit(0xAA, 15, true), 0xAA);
    }

    #[test]
    fn encode_led_pair_red_only() {
        // Canal 0 : red = P1.0, green = P1.1
        assert_eq!(encode_led_pair(0x00, 0, true, false), 0b0000_0001);
    }

    #[test]
    fn encode_led_pair_green_only() {
        assert_eq!(encode_led_pair(0x00, 0, false, true), 0b0000_0010);
    }

    #[test]
    fn encode_led_pair_canal_3() {
        // Canal 3 : red = P1.6, green = P1.7
        assert_eq!(encode_led_pair(0x00, 3, true, true), 0b1100_0000);
    }

    #[test]
    fn tca9535_init_writes_direction() {
        let mut bus = MockBus::new();
        bus.expect_write(0x20, vec![Reg::OutputPort0 as u8, 0x00]);
        bus.expect_write(0x20, vec![Reg::OutputPort1 as u8, 0x00]);
        bus.expect_write(0x20, vec![Reg::ConfigPort0 as u8, P0_CONFIG]);
        bus.expect_write(0x20, vec![Reg::ConfigPort1 as u8, P1_CONFIG]);
        bus.expect_write(0x20, vec![Reg::PolarityInv0 as u8, 0x00]);
        bus.expect_write(0x20, vec![Reg::PolarityInv1 as u8, 0x00]);

        assert!(Tca9535::init(&mut bus, 0x20).is_ok());
    }

    #[test]
    fn tca9535_all_off_writes_both_ports_explicitly() {
        let mut bus = MockBus::new();
        let mut tca = Tca9535::new_without_init(0x21);
        // Fix bug scaffold firmware-rs : écriture EXPLICITE des deux ports
        bus.expect_write(0x21, vec![Reg::OutputPort0 as u8, 0x00]);
        bus.expect_write(0x21, vec![Reg::OutputPort1 as u8, 0x00]);
        tca.all_off(&mut bus).unwrap();
    }

    #[test]
    fn tca9535_switch_battery_single_write() {
        let mut bus = MockBus::new();
        let mut tca = Tca9535::new_without_init(0x20);
        // Canal 0 ON → P0.3 set → 0b0000_1000
        bus.expect_write(0x20, vec![Reg::OutputPort0 as u8, 0b0000_1000]);
        tca.switch_battery(&mut bus, 0, true).unwrap();

        // Canal 3 ON (cumul) → 0b0000_1001
        bus.expect_write(0x20, vec![Reg::OutputPort0 as u8, 0b0000_1001]);
        tca.switch_battery(&mut bus, 3, true).unwrap();
    }

    #[test]
    fn tca9535_switch_battery_invalid_channel() {
        let mut bus = MockBus::new();
        let mut tca = Tca9535::new_without_init(0x20);
        assert_eq!(
            tca.switch_battery(&mut bus, 4, true).err(),
            Some(Tca9535Error::InvalidChannel)
        );
    }

    #[test]
    fn tca9535_read_alerts() {
        let mut bus = MockBus::new();
        let tca = Tca9535::new_without_init(0x20);
        // Alert sur canal 0 (bit 7 low) et canal 3 (bit 4 low) → port0 = 0b0110_1111
        bus.expect_write_read(0x20, vec![Reg::InputPort0 as u8], vec![0b0110_1111]);
        let alerts = tca.read_alerts(&mut bus).unwrap();
        assert!(alerts[0]);
        assert!(!alerts[1]);
        assert!(!alerts[2]);
        assert!(alerts[3]);
    }
}
