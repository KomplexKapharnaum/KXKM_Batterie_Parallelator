//! Driver `AHT30` — capteur température/humidité I²C.
//! Référence : `ASAIR` `AHT30` datasheet v1.0. Adresse unique 0x38.
//!
//! Pattern trigger + wait ≈80 ms + read 6 bytes.
//! `CRC` byte (7e) ignoré en V1 (vérifiable via couche C si besoin).

#![allow(clippy::module_name_repetitions)]

use bmu_i2c::{I2cBus, I2cError};

pub const AHT30_ADDR: u8 = 0x38;

const CMD_INIT: [u8; 3] = [0xBE, 0x08, 0x00];
const CMD_TRIGGER: [u8; 3] = [0xAC, 0x33, 0x00];
const CMD_SOFT_RESET: [u8; 1] = [0xBA];

/// Erreurs du driver `AHT30`. `NotCalibrated` et `CrcMismatch` sont réservés
/// pour des vérifications ajoutées en Phase 6 (init calibration check,
/// validation `CRC`). `#[non_exhaustive]` protège les callers contre l'ajout
/// de nouveaux variants.
#[non_exhaustive]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Aht30Error {
    I2c(I2cError),
    NotCalibrated,
    StillBusy,
    CrcMismatch,
}

impl From<I2cError> for Aht30Error {
    fn from(e: I2cError) -> Self {
        Self::I2c(e)
    }
}

/// Résultat d'une mesure. `temp_c10` aligné avec `bmu_types::Snapshot::temp_c10`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ClimateReading {
    /// Température en c10 (°C × 10), ex. 25.3 °C → 253
    pub temp_c10: i16,
    /// Humidité relative en pct10 (% × 10), ex. 48.7 % → 487
    pub rh_pct10: u16,
}

/// Parse les 6 premiers bytes d'une mesure `AHT30` en `ClimateReading`.
/// Layout datasheet v1.0 :
/// - byte 0 : status (bit 7 = busy, bit 3 = calibrated)
/// - byte 1 : rh\[19..12\]
/// - byte 2 : rh\[11..4\]
/// - byte 3 : rh\[3..0\] << 4 | t\[19..16\]
/// - byte 4 : t\[15..8\]
/// - byte 5 : t\[7..0\]
///
/// (byte 6 `CRC` non vérifié en V1)
///
/// Formules datasheet :
/// - `RH` = `raw_rh` / 2^20 × 100 (%)
/// - temp = `raw_t` / 2^20 × 200 − 50 (°C)
///
/// Les valeurs sont converties en fixe `Q20` puis mises à l'échelle
/// en `u16` / `i16` (pct10 / c10).
///
/// # Errors
/// Retourne `StillBusy` si le bit 7 du status est à 1 (mesure en cours).
#[inline]
#[must_use = "parse_measurement returns a Result that must be inspected"]
#[allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::arithmetic_side_effects,
    clippy::indexing_slicing
)]
pub fn parse_measurement(raw: [u8; 6]) -> Result<ClimateReading, Aht30Error> {
    let status = raw[0];
    if status & 0x80 != 0 {
        return Err(Aht30Error::StillBusy);
    }

    let rh_raw: u32 =
        (u32::from(raw[1]) << 12) | (u32::from(raw[2]) << 4) | (u32::from(raw[3]) >> 4);
    let t_raw: u32 =
        ((u32::from(raw[3]) & 0x0F) << 16) | (u32::from(raw[4]) << 8) | u32::from(raw[5]);

    // rh_pct10 = raw_rh × 1000 / 2^20  (= % × 10)
    let rh_pct10 = ((u64::from(rh_raw) * 1000) >> 20) as u16;

    // temp_c10 = (raw_t × 2000) >> 20 − 500  (= °C × 10)
    let t_scaled = (u64::from(t_raw) * 2000) >> 20;
    let temp_c10 = (t_scaled as i32 - 500) as i16;

    Ok(ClimateReading { temp_c10, rh_pct10 })
}

/// Wrapper bus-aware stateless (adresse fixe cachée dans la struct).
#[derive(Debug, Clone, Copy)]
pub struct Aht30 {
    addr: u8,
}

impl Aht30 {
    /// Construit une instance avec l'adresse donnée.
    #[must_use]
    pub const fn new(addr: u8) -> Self {
        Self { addr }
    }

    /// Adresse I²C.
    #[must_use]
    pub const fn addr(&self) -> u8 {
        self.addr
    }

    /// Envoie la commande de déclenchement de mesure. L'appelant doit
    /// attendre environ 80 ms avant d'appeler `read_measurement()`.
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn trigger<B: I2cBus>(&self, bus: &mut B) -> Result<(), Aht30Error> {
        bus.write(self.addr, &CMD_TRIGGER)?;
        Ok(())
    }

    /// Lit 6 bytes (status + data) et parse en `ClimateReading`.
    /// `CRC` byte ignoré (V1).
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue, `StillBusy` si la mesure
    /// n'est pas prête.
    pub fn read_measurement<B: I2cBus>(&self, bus: &mut B) -> Result<ClimateReading, Aht30Error> {
        let mut buf = [0u8; 6];
        bus.read(self.addr, &mut buf)?;
        parse_measurement(buf)
    }

    /// Envoie un soft reset (`0xBA`).
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn soft_reset<B: I2cBus>(&self, bus: &mut B) -> Result<(), Aht30Error> {
        bus.write(self.addr, &CMD_SOFT_RESET)?;
        Ok(())
    }

    /// Envoie la commande d'initialisation calibration (`0xBE 0x08 0x00`).
    /// À appeler une fois après power-on ou soft reset.
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn init_calibration<B: I2cBus>(&self, bus: &mut B) -> Result<(), Aht30Error> {
        bus.write(self.addr, &CMD_INIT)?;
        Ok(())
    }
}

impl Default for Aht30 {
    fn default() -> Self {
        Self::new(AHT30_ADDR)
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
    fn parse_measurement_room_conditions() {
        // rh = 50 %, t = 25 °C
        // raw_rh = 50 × 2^20 / 100 = 524288 = 0x80000
        // raw_t  = (25+50) × 2^20 / 200 = 393216 = 0x60000
        // rh bits 19..12 = 0x80, bits 11..4 = 0x00, bits 3..0 = 0x0
        // t  bits 19..16 = 0x6,  bits 15..8 = 0x00, bits 7..0 = 0x00
        // bytes: status=0x00, 0x80, 0x00, (0x0<<4)|0x6=0x06, 0x00, 0x00
        let raw = [0x00, 0x80, 0x00, 0x06, 0x00, 0x00];
        let reading = parse_measurement(raw).unwrap();
        // Math is exact for these raw bytes (no truncation): assert_eq!
        assert_eq!(reading.temp_c10, 250);
        assert_eq!(reading.rh_pct10, 500);
    }

    #[test]
    fn parse_measurement_freezing() {
        // rh = 0 %, t = 0 °C → raw_rh = 0, raw_t = 50 × 2^20 / 200 = 0x040000
        // bytes: 0x00 0x00 0x00 0x04 0x00 0x00
        let raw = [0x00, 0x00, 0x00, 0x04, 0x00, 0x00];
        let reading = parse_measurement(raw).unwrap();
        assert_eq!(reading.temp_c10, 0);
        assert_eq!(reading.rh_pct10, 0);
    }

    #[test]
    fn parse_measurement_rejects_busy() {
        let raw = [0x80, 0x00, 0x00, 0x00, 0x00, 0x00]; // bit 7 = busy
        assert_eq!(parse_measurement(raw).err(), Some(Aht30Error::StillBusy));
    }

    #[test]
    fn parse_measurement_ok_even_if_cal_bit_clear() {
        // Le bit calibration (bit 3) est vérifié à init_calibration, pas à la lecture
        let raw = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        assert!(parse_measurement(raw).is_ok());
    }

    #[test]
    fn aht30_trigger_sequence() {
        let mut bus = MockBus::new();
        let dev = Aht30::new(0x38);
        bus.expect_write(0x38, CMD_TRIGGER.to_vec());
        dev.trigger(&mut bus).unwrap();
    }

    #[test]
    fn aht30_read_measurement_sequence() {
        let mut bus = MockBus::new();
        let dev = Aht30::new(0x38);
        bus.expect_read(0x38, vec![0x00, 0x80, 0x00, 0x06, 0x00, 0x00]);
        let reading = dev.read_measurement(&mut bus).unwrap();
        assert_eq!(reading.temp_c10, 250);
    }
}
