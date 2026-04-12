//! Driver `INA237` — parseurs purs + wrapper bus-aware.
//! Référence : TI `SBOSA20A` datasheet (`INA237`, Feb 2021, rev May 2022),
//! tables 7-3 (register map), 7-9 (`VBUS`), 7-10 (`DIETEMP`), 7-11 (`CURRENT`),
//! 7-20 (`MANUFACTURER_ID`), équations 1-3 section 8.1.2.

#![allow(clippy::module_name_repetitions)]

use bmu_i2c::{I2cBus, I2cError};
use bmu_types::{Milliamps, Millivolts};

pub const INA237_ADDR_BASE: u8 = 0x40;
pub const INA237_ADDR_MAX: u8 = 0x4F;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    Config = 0x00,
    AdcConfig = 0x01,
    ShuntCal = 0x02,
    VShunt = 0x04,
    VBus = 0x05,
    DieTemp = 0x06,
    Current = 0x07,
    Power = 0x08,
    Diag = 0x0B,
    ManufId = 0x3E,
    // Note: `INA237` has NO `DeviceId` register (0x3F reserved per Table 7-3).
    // `INA238`/239 have it at 0x3F. See deviation #12 in the plan.
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ina237Error {
    I2c(I2cError),
    UnexpectedManufacturerId(u16),
    CalibrationInvalid,
}

impl From<I2cError> for Ina237Error {
    fn from(e: I2cError) -> Self {
        Self::I2c(e)
    }
}

/// Parse `VBUS` register (0x05).
/// 16-bit two's complement. `LSB` = 3.125 mV/bit. No shift.
/// Datasheet `SBOSA20A` Table 7-9 p.23 ; nominal range 0-85 V
/// ("always positive" in normal operation, negative only on fault).
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub fn parse_vbus(raw: [u8; 2]) -> Millivolts {
    let word = i16::from_be_bytes(raw);
    Millivolts::from_raw((i32::from(word) * 3125) / 1000)
}

/// Encode les 2 bytes `SHUNT_CAL` (0x02) à partir du shunt et du max current.
///
/// Formule datasheet `SBOSA20A` eq. (1), section 8.1.2 p.29 :
/// `SHUNT_CAL` = 819.2e6 × `CURRENT_LSB` × `R_shunt`
/// avec `CURRENT_LSB` = `max_current` / 2^15.
///
/// Forme entière utilisée ici (nA/µΩ) :
/// `SHUNT_CAL` = (`lsb_na` × `shunt_uΩ` × 8192) / 1e10
/// car 819.2e6 × 1e-9 (A/nA) × 1e-6 (Ω/µΩ) = 8.192e-7 = 8192/1e10.
///
/// Hardcodé pour `ADCRANGE` = 0 (config KXKM). Si `ADCRANGE` = 1 un jour,
/// multiplier le résultat par 4. Bit 15 est réservé, masqué implicitement
/// par le clip à 0x7FFF.
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub fn encode_shunt_cal(shunt_micro_ohms: u32, max_current_ma: u32) -> [u8; 2] {
    let current_lsb_na: u64 = (u64::from(max_current_ma) * 1_000_000) >> 15;
    let num: u64 = current_lsb_na
        .saturating_mul(u64::from(shunt_micro_ohms))
        .saturating_mul(8192);
    let shuntcal: u64 = num / 10_000_000_000;
    #[allow(clippy::cast_possible_truncation)]
    let clipped = shuntcal.min(0x7FFF) as u16;
    clipped.to_be_bytes()
}

/// Calcule le `CURRENT_LSB` (nA/bit) correspondant à un `max_current` donné.
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub const fn current_lsb_na(max_current_ma: u32) -> u32 {
    #[allow(clippy::cast_possible_truncation)]
    let lsb = ((max_current_ma as u64 * 1_000_000) >> 15) as u32;
    lsb
}

/// Parse `CURRENT` register (0x07) en `Milliamps`.
/// raw = signed 16-bit ; valeur = raw × `lsb_na` / `1_000_000`.
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub fn parse_current(raw: [u8; 2], lsb_na: u32) -> Milliamps {
    let word = i32::from(i16::from_be_bytes(raw));
    let nano_amps = i64::from(word) * i64::from(lsb_na);
    #[allow(clippy::cast_possible_truncation)]
    let milli_amps = (nano_amps / 1_000_000) as i32;
    Milliamps::from_raw(milli_amps)
}

/// Parse `DIETEMP` register (0x06) en dixièmes de °C (c10).
/// bits 15:4 signés 12-bit, bits 3:0 réservés, `LSB` = 125 m°C/bit.
/// Datasheet `SBOSA20A` Table 7-10 p.24.
#[inline]
#[must_use]
#[allow(clippy::arithmetic_side_effects)]
pub fn parse_dietemp_c10(raw: [u8; 2]) -> i16 {
    let word = i16::from_be_bytes(raw);
    let steps = word >> 4;
    #[allow(clippy::cast_possible_truncation)]
    let c10 = (i32::from(steps) * 125 / 100) as i16;
    c10
}

/// Vérifie `MANUFACTURER_ID` register (0x3E). Doit être "TI" `ASCII` = 0x5449.
/// Datasheet `SBOSA20A` Table 7-20 p.27.
///
/// Note : l'`INA237` n'a PAS de registre `DEVICE_ID` (0x3F est réservé,
/// Table 7-3 p.20-21). Seul `MANUFACTURER_ID` est un identifiant fiable.
///
/// # Errors
/// Retourne `Ina237Error::UnexpectedManufacturerId` si la valeur lue
/// n'est pas 0x5449 ("TI" en `ASCII`).
#[inline]
pub fn check_manufacturer_id(raw: [u8; 2]) -> Result<(), Ina237Error> {
    let id = u16::from_be_bytes(raw);
    if id == 0x5449 {
        Ok(())
    } else {
        Err(Ina237Error::UnexpectedManufacturerId(id))
    }
}

/// Wrapper bus-aware stateful. `current_lsb_na` est caché après init.
///
/// Ce wrapper est utilisé par les tests host et les outils de diagnostic
/// (ex. `cargo xtask probe-bus`). Le cœur Rust opérationnel (§4.3 spec)
/// utilise les fonctions pures ci-dessus sur les bytes déjà lus par la
/// couche C.
#[derive(Debug, Clone, Copy)]
pub struct Ina237 {
    addr: u8,
    current_lsb_na: u32,
}

impl Ina237 {
    /// Séquence d'init : verify `MANUFACTURER_ID`, reset, `ADC_CONFIG`,
    /// `SHUNT_CAL`. Retourne une instance prête à lire.
    ///
    /// # Errors
    /// - `I2c` si le bus échoue
    /// - `UnexpectedManufacturerId` si le device ne répond pas "TI" (0x5449)
    /// - `CalibrationInvalid` si `max_current_ma` est trop petit pour
    ///   produire un `CURRENT_LSB` non nul
    pub fn init<B: I2cBus>(
        bus: &mut B,
        addr: u8,
        shunt_micro_ohms: u32,
        max_current_ma: u32,
    ) -> Result<Self, Ina237Error> {
        // 1. Verify MANUFACTURER_ID (seul ID fiable sur INA237)
        let mut buf = [0u8; 2];
        bus.write_read(addr, &[Reg::ManufId as u8], &mut buf)?;
        check_manufacturer_id(buf)?;

        // 2. Precheck calibration AVANT toute écriture hardware pour éviter
        //    de laisser le chip à moitié configuré (SHUNT_CAL=0) en cas d'erreur.
        let lsb = current_lsb_na(max_current_ma);
        if lsb == 0 {
            return Err(Ina237Error::CalibrationInvalid);
        }

        // 3. Reset (CONFIG bit 15 = 1)
        bus.write(addr, &[Reg::Config as u8, 0x80, 0x00])?;
        // 4. Clear CONFIG (ADCRANGE=0 for ±163.84 mV full scale)
        bus.write(addr, &[Reg::Config as u8, 0x00, 0x00])?;
        // 5. ADC_CONFIG : continuous bus+shunt, 540 µs, avg 64
        bus.write(addr, &[Reg::AdcConfig as u8, 0xB9, 0x03])?;
        // 6. SHUNT_CAL
        let cal = encode_shunt_cal(shunt_micro_ohms, max_current_ma);
        bus.write(addr, &[Reg::ShuntCal as u8, cal[0], cal[1]])?;

        Ok(Self {
            addr,
            current_lsb_na: lsb,
        })
    }

    /// Adresse I²C configurée.
    #[must_use]
    pub const fn addr(&self) -> u8 {
        self.addr
    }

    /// Lit `VBUS` et parse en `Millivolts`.
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn read_vbus<B: I2cBus>(&self, bus: &mut B) -> Result<Millivolts, Ina237Error> {
        let mut buf = [0u8; 2];
        bus.write_read(self.addr, &[Reg::VBus as u8], &mut buf)?;
        Ok(parse_vbus(buf))
    }

    /// Lit `CURRENT` et parse en `Milliamps`.
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn read_current<B: I2cBus>(&self, bus: &mut B) -> Result<Milliamps, Ina237Error> {
        let mut buf = [0u8; 2];
        bus.write_read(self.addr, &[Reg::Current as u8], &mut buf)?;
        Ok(parse_current(buf, self.current_lsb_na))
    }

    /// Lit `DIETEMP` et parse en dixièmes de °C (c10).
    ///
    /// # Errors
    /// Propage `I2c` si la transaction bus échoue.
    pub fn read_dietemp_c10<B: I2cBus>(&self, bus: &mut B) -> Result<i16, Ina237Error> {
        let mut buf = [0u8; 2];
        bus.write_read(self.addr, &[Reg::DieTemp as u8], &mut buf)?;
        Ok(parse_dietemp_c10(buf))
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::panic)]
mod tests {
    use super::*;

    // ---------- parse_vbus ----------

    #[test]
    fn parse_vbus_zero() {
        assert_eq!(parse_vbus([0x00, 0x00]), Millivolts::ZERO);
    }

    #[test]
    fn parse_vbus_24v() {
        assert_eq!(parse_vbus([0x1E, 0x00]), Millivolts::from_raw(24000));
    }

    #[test]
    fn parse_vbus_48v() {
        assert_eq!(parse_vbus([0x3C, 0x00]), Millivolts::from_raw(48000));
    }

    #[test]
    fn parse_vbus_85v_full_scale() {
        assert_eq!(parse_vbus([0x6A, 0x40]), Millivolts::from_raw(85000));
    }

    #[test]
    fn parse_vbus_negative_fault() {
        assert_eq!(parse_vbus([0xFF, 0xFF]), Millivolts::from_raw(-3));
    }

    // ---------- encode_shunt_cal ----------

    #[test]
    fn encode_shunt_cal_datasheet_example_10a_2mohm() {
        let bytes = encode_shunt_cal(2_000, 10_000);
        assert_eq!(u16::from_be_bytes(bytes), 499);
    }

    #[test]
    fn encode_shunt_cal_kxkm_100a_500u_ohm() {
        let bytes = encode_shunt_cal(500, 100_000);
        assert_eq!(u16::from_be_bytes(bytes), 1249);
    }

    #[test]
    fn encode_shunt_cal_clip_guard() {
        let bytes = encode_shunt_cal(1_000_000, 10_000);
        assert_eq!(u16::from_be_bytes(bytes), 0x7FFF);
    }

    // ---------- parse_current ----------

    #[test]
    fn parse_current_with_lsb() {
        let ma = parse_current([0x04, 0x00], 30_517);
        assert_eq!(ma, Milliamps::from_raw(31));
    }

    #[test]
    fn parse_current_negative() {
        let ma = parse_current([0xFC, 0x00], 30_517);
        assert_eq!(ma, Milliamps::from_raw(-31));
    }

    #[test]
    fn parse_current_zero() {
        assert_eq!(parse_current([0x00, 0x00], 30_517), Milliamps::ZERO);
    }

    // ---------- parse_dietemp_c10 ----------

    #[test]
    fn parse_dietemp_zero() {
        assert_eq!(parse_dietemp_c10([0x00, 0x00]), 0);
    }

    #[test]
    fn parse_dietemp_25c() {
        assert_eq!(parse_dietemp_c10([0x0C, 0x80]), 250);
    }

    #[test]
    fn parse_dietemp_125c_full_scale() {
        assert_eq!(parse_dietemp_c10([0x3E, 0x80]), 1250);
    }

    #[test]
    fn parse_dietemp_minus_5c() {
        assert_eq!(parse_dietemp_c10([0xFD, 0x80]), -50);
    }

    #[test]
    fn parse_dietemp_minus_40c() {
        assert_eq!(parse_dietemp_c10([0xEC, 0x00]), -400);
    }

    // ---------- check_manufacturer_id ----------

    #[test]
    fn check_manuf_id_ti() {
        assert!(check_manufacturer_id([0x54, 0x49]).is_ok());
    }

    #[test]
    fn check_manuf_id_rejects_other() {
        assert_eq!(
            check_manufacturer_id([0x00, 0x00]),
            Err(Ina237Error::UnexpectedManufacturerId(0x0000))
        );
    }

    // ---------- Ina237 wrapper ----------

    extern crate std;
    use bmu_test_fixtures::MockBus;
    use std::vec;

    fn setup_mock_init(addr: u8) -> MockBus {
        let mut bus = MockBus::new();
        // 1. read MANUFACTURER_ID → "TI" (seul ID fiable sur INA237)
        bus.expect_write_read(addr, vec![Reg::ManufId as u8], vec![0x54, 0x49]);
        // 2. write CONFIG reset bit (0x8000)
        bus.expect_write(addr, vec![Reg::Config as u8, 0x80, 0x00]);
        // 3. write CONFIG clear (0x0000, ADCRANGE=0)
        bus.expect_write(addr, vec![Reg::Config as u8, 0x00, 0x00]);
        // 4. write ADC_CONFIG (cont bus+shunt, 540us, avg 64)
        //    MODE=1011, VBUSCT=100, VSHCT=100, VTCT=000, AVG=011 = 0xB903
        bus.expect_write(addr, vec![Reg::AdcConfig as u8, 0xB9, 0x03]);
        // 5. write SHUNT_CAL
        let cal = encode_shunt_cal(2_000, 1_000);
        bus.expect_write(addr, vec![Reg::ShuntCal as u8, cal[0], cal[1]]);
        bus
    }

    #[test]
    fn ina237_init_success() {
        let mut bus = setup_mock_init(0x40);
        let result = Ina237::init(&mut bus, 0x40, 2_000, 1_000);
        assert!(result.is_ok());
    }

    #[test]
    fn ina237_init_rejects_bad_manuf() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![Reg::ManufId as u8], vec![0x00, 0x00]);
        assert_eq!(
            Ina237::init(&mut bus, 0x40, 2_000, 1_000).err(),
            Some(Ina237Error::UnexpectedManufacturerId(0x0000))
        );
    }

    #[test]
    fn ina237_read_vbus_after_init() {
        let mut bus = setup_mock_init(0x40);
        // raw 0x1E00 = 7680 → 24000 mV (cf parse_vbus_24v)
        bus.expect_write_read(0x40, vec![Reg::VBus as u8], vec![0x1E, 0x00]);
        let ina = Ina237::init(&mut bus, 0x40, 2_000, 1_000).unwrap();
        assert_eq!(
            ina.read_vbus(&mut bus).unwrap(),
            Millivolts::from_raw(24000)
        );
    }

    #[test]
    fn ina237_read_current_after_init() {
        let mut bus = setup_mock_init(0x40);
        bus.expect_write_read(0x40, vec![Reg::Current as u8], vec![0x04, 0x00]);
        let ina = Ina237::init(&mut bus, 0x40, 2_000, 1_000).unwrap();
        // current_lsb_na for 1000 mA = 30517
        // raw 0x0400 = 1024 → 31 mA
        let ma = ina.read_current(&mut bus).unwrap();
        assert_eq!(ma, Milliamps::from_raw(31));
    }
}
