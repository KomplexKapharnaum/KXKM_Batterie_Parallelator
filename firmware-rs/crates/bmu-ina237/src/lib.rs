//! Driver INA237 no_std — TI SBOS945 datasheet
//! Generique sur embedded_hal::i2c::I2c — testable sur host sans hardware.

#![no_std]

use embedded_hal::i2c::I2c;

/// INA237 register addresses (SBOS945 Table 7-5)
pub mod reg {
    pub const CONFIG: u8 = 0x00;
    pub const ADC_CONFIG: u8 = 0x01;
    pub const SHUNT_CAL: u8 = 0x02;
    pub const VSHUNT: u8 = 0x04;
    pub const VBUS: u8 = 0x05;
    pub const DIETEMP: u8 = 0x06;
    pub const CURRENT: u8 = 0x07;
    pub const POWER: u8 = 0x08;
    pub const DIAG_ALRT: u8 = 0x0B;
    pub const SOVL: u8 = 0x0C;
    pub const SUVL: u8 = 0x0D;
    pub const BOVL: u8 = 0x0E;
    pub const BUVL: u8 = 0x0F;
    pub const MANUFACTURER_ID: u8 = 0x3E;
    pub const DEVICE_ID: u8 = 0x3F;
}

/// Bus voltage LSB = 3.125 mV/bit
const BUS_VOLTAGE_LSB_MV: f32 = 3.125;

/// Expected manufacturer ID ("TI" in ASCII)
const TI_MANUFACTURER_ID: u16 = 0x5449;

/// Expected device ID
const INA237_DEVICE_ID: u16 = 0x2370;

#[derive(Debug)]
pub enum Error<E> {
    I2c(E),
    InvalidDevice,
    CalibrationFailed,
}

impl<E> From<E> for Error<E> {
    fn from(e: E) -> Self {
        Error::I2c(e)
    }
}

pub struct Ina237<I2C> {
    i2c: I2C,
    addr: u8,
    current_lsb: f32,
}

impl<I2C: I2c> Ina237<I2C> {
    /// Create and initialize an INA237.
    /// shunt_ohm: shunt resistance (e.g. 0.002 for 2mΩ)
    /// max_current_a: max expected current for calibration
    pub fn new(i2c: I2C, addr: u8, shunt_ohm: f32, max_current_a: f32) -> Result<Self, Error<I2C::Error>> {
        let mut dev = Self {
            i2c,
            addr,
            current_lsb: max_current_a / 32768.0,
        };

        // Verify manufacturer ID
        let mfr = dev.read_reg(reg::MANUFACTURER_ID)?;
        if mfr != TI_MANUFACTURER_ID {
            return Err(Error::InvalidDevice);
        }

        // Reset
        dev.write_reg(reg::CONFIG, 0x8000)?;
        // Small delay needed after reset — caller should ensure

        // ADCRANGE=0 (±163.84mV), no CONVDLY
        dev.write_reg(reg::CONFIG, 0x0000)?;

        // Calibration: SHUNT_CAL = 819.2e6 * current_lsb * r_shunt
        let cal = (819.2e6 * dev.current_lsb * shunt_ohm) as u16;
        if cal == 0 {
            return Err(Error::CalibrationFailed);
        }
        dev.write_reg(reg::SHUNT_CAL, cal)?;

        // ADC_CONFIG: continuous bus+shunt, 540µs conversion, 64 averages
        // MODE=1011(0xB), VBUSCT=100(540µs), VSHCT=100(540µs), VTCT=000(50µs), AVG=011(64)
        let adc_config: u16 = (0xB << 12) | (0x4 << 9) | (0x4 << 6) | (0x0 << 3) | 0x3;
        dev.write_reg(reg::ADC_CONFIG, adc_config)?;

        Ok(dev)
    }

    /// Read bus voltage in millivolts.
    pub fn read_voltage_mv(&mut self) -> Result<f32, Error<I2C::Error>> {
        let raw = self.read_reg(reg::VBUS)?;
        Ok(raw as f32 * BUS_VOLTAGE_LSB_MV)
    }

    /// Read current in amps (signed, positive = discharge).
    pub fn read_current_a(&mut self) -> Result<f32, Error<I2C::Error>> {
        let raw = self.read_reg(reg::CURRENT)?;
        Ok(raw as i16 as f32 * self.current_lsb)
    }

    /// Read voltage (mV) and current (A) together.
    pub fn read_voltage_current(&mut self) -> Result<(f32, f32), Error<I2C::Error>> {
        let v = self.read_voltage_mv()?;
        let i = self.read_current_a()?;
        Ok((v, i))
    }

    /// Read die temperature in °C.
    pub fn read_temperature_c(&mut self) -> Result<f32, Error<I2C::Error>> {
        let raw = self.read_reg(reg::DIETEMP)?;
        // Bits 15-4 valid, right-shift 4, LSB = 125 m°C
        Ok((raw as i16 >> 4) as f32 * 0.125)
    }

    /// Set bus over/under voltage alert thresholds (in mV).
    pub fn set_bus_alerts(&mut self, ov_mv: u16, uv_mv: u16) -> Result<(), Error<I2C::Error>> {
        let ov_reg = (ov_mv as f32 / BUS_VOLTAGE_LSB_MV) as u16;
        let uv_reg = (uv_mv as f32 / BUS_VOLTAGE_LSB_MV) as u16;
        self.write_reg(reg::BOVL, ov_reg)?;
        self.write_reg(reg::BUVL, uv_reg)?;
        Ok(())
    }

    /// Read diagnostic/alert flags.
    pub fn read_diag_alert(&mut self) -> Result<u16, Error<I2C::Error>> {
        self.read_reg(reg::DIAG_ALRT)
    }

    /// Get I2C address.
    pub fn addr(&self) -> u8 {
        self.addr
    }

    fn write_reg(&mut self, reg: u8, val: u16) -> Result<(), Error<I2C::Error>> {
        let buf = [reg, (val >> 8) as u8, (val & 0xFF) as u8];
        self.i2c.write(self.addr, &buf)?;
        Ok(())
    }

    fn read_reg(&mut self, reg: u8) -> Result<u16, Error<I2C::Error>> {
        let mut buf = [0u8; 2];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(u16::from_be_bytes(buf))
    }
}

#[cfg(test)]
mod tests {
    // Tests will use embedded-hal-mock
}
