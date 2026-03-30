//! Driver TCA9535 no_std — TI SCPS209 datasheet
//! PCB BMU v2 pin mapping: reversed switch order, paired LEDs.

#![no_std]

use embedded_hal::i2c::I2c;

pub mod reg {
    pub const INPUT_PORT0: u8 = 0x00;
    pub const INPUT_PORT1: u8 = 0x01;
    pub const OUTPUT_PORT0: u8 = 0x02;
    pub const OUTPUT_PORT1: u8 = 0x03;
    pub const POLARITY_INV0: u8 = 0x04;
    pub const POLARITY_INV1: u8 = 0x05;
    pub const CONFIG_PORT0: u8 = 0x06;
    pub const CONFIG_PORT1: u8 = 0x07;
}

/// PCB BMU v2 port directions
/// P0: bits 0-3 = output (switches), bits 4-7 = input (alerts)
const P0_CONFIG: u8 = 0xF0;
/// P1: all output (LEDs)
const P1_CONFIG: u8 = 0x00;

/// PCB switch mapping (REVERSED): channel 0 (bat1) = P0.3, channel 3 (bat4) = P0.0
const SWITCH_PIN: [u8; 4] = [3, 2, 1, 0];
/// Alert mapping (REVERSED): channel 0 (bat1) = P0.7, channel 3 (bat4) = P0.4
const ALERT_PIN: [u8; 4] = [7, 6, 5, 4];

#[derive(Debug)]
pub enum Error<E> {
    I2c(E),
    InvalidChannel,
}

impl<E> From<E> for Error<E> {
    fn from(e: E) -> Self {
        Error::I2c(e)
    }
}

pub struct Tca9535<I2C> {
    i2c: I2C,
    addr: u8,
    out_p0: u8,
    out_p1: u8,
}

impl<I2C: I2c> Tca9535<I2C> {
    pub fn new(i2c: I2C, addr: u8) -> Result<Self, Error<I2C::Error>> {
        let mut dev = Self {
            i2c,
            addr,
            out_p0: 0x00,
            out_p1: 0x00,
        };

        // Set outputs LOW before configuring direction (prevent glitches)
        dev.write_reg(reg::OUTPUT_PORT0, 0x00)?;
        dev.write_reg(reg::OUTPUT_PORT1, 0x00)?;

        // Configure directions
        dev.write_reg(reg::CONFIG_PORT0, P0_CONFIG)?;
        dev.write_reg(reg::CONFIG_PORT1, P1_CONFIG)?;

        // No polarity inversion
        dev.write_reg(reg::POLARITY_INV0, 0x00)?;
        dev.write_reg(reg::POLARITY_INV1, 0x00)?;

        Ok(dev)
    }

    /// Switch battery MOSFET on/off. channel: 0-3 (bat1-bat4).
    pub fn switch_battery(&mut self, channel: u8, on: bool) -> Result<(), Error<I2C::Error>> {
        if channel > 3 {
            return Err(Error::InvalidChannel);
        }
        let pin = SWITCH_PIN[channel as usize];
        if on {
            self.out_p0 |= 1 << pin;
        } else {
            self.out_p0 &= !(1 << pin);
        }
        self.write_reg(reg::OUTPUT_PORT0, self.out_p0)?;
        Ok(())
    }

    /// Set LED state. channel 0: red=P1.0, green=P1.1, etc.
    pub fn set_led(&mut self, channel: u8, red: bool, green: bool) -> Result<(), Error<I2C::Error>> {
        if channel > 3 {
            return Err(Error::InvalidChannel);
        }
        let red_pin = channel * 2;
        let green_pin = channel * 2 + 1;

        if red {
            self.out_p1 |= 1 << red_pin;
        } else {
            self.out_p1 &= !(1 << red_pin);
        }
        if green {
            self.out_p1 |= 1 << green_pin;
        } else {
            self.out_p1 &= !(1 << green_pin);
        }
        self.write_reg(reg::OUTPUT_PORT1, self.out_p1)?;
        Ok(())
    }

    /// Read alert input for a channel. Returns true if alert active (LOW).
    pub fn read_alert(&mut self, channel: u8) -> Result<bool, Error<I2C::Error>> {
        if channel > 3 {
            return Err(Error::InvalidChannel);
        }
        let data = self.read_reg(reg::INPUT_PORT0)?;
        let pin = ALERT_PIN[channel as usize];
        // Active LOW: alert = bit is 0
        Ok((data >> pin) & 1 == 0)
    }

    /// All switches OFF + all LEDs OFF (fail-safe bulk write).
    pub fn all_off(&mut self) -> Result<(), Error<I2C::Error>> {
        self.out_p0 = 0x00;
        self.out_p1 = 0x00;
        // Bulk write both ports
        self.i2c.write(self.addr, &[reg::OUTPUT_PORT0, 0x00, 0x00])?;
        Ok(())
    }

    pub fn addr(&self) -> u8 {
        self.addr
    }

    fn write_reg(&mut self, reg: u8, val: u8) -> Result<(), Error<I2C::Error>> {
        self.i2c.write(self.addr, &[reg, val])?;
        Ok(())
    }

    fn read_reg(&mut self, reg: u8) -> Result<u8, Error<I2C::Error>> {
        let mut buf = [0u8; 1];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(buf[0])
    }
}
