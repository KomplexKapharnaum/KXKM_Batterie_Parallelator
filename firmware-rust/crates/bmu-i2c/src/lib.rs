//! Trait `I2cBus` maison (pas embedded-hal, cf §3.3 spec).
//! Zéro dépendance. Utilisé par `bmu-drivers` (parseurs purs + wrappers bus-aware)
//! et impl séparées `MockBus` (tests) et `EspIdfBus` (hors workspace, côté ESP-IDF).
#![no_std]

/// Erreurs I²C platform-agnostiques. `Hardware(code)` encapsule un code opaque.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum I2cError {
    /// Device n'a pas ACK son adresse.
    Nack,
    /// Arbitrage perdu (multi-master non utilisé, mais glitch possible).
    ArbLost,
    /// Bus bloqué (`SDA` ou `SCL` maintenu).
    Timeout,
    /// Bus déjà busy au start de la transaction.
    BusBusy,
    /// Longueur de buffer invalide.
    InvalidLength,
    /// Code d'erreur opaque de la plateforme.
    Hardware(u16),
}

/// Trait I²C synchrone. Chaque méthode correspond à une transaction atomique.
///
/// Les implémentations doivent :
/// - garantir un `STOP` en fin de transaction (même en erreur)
/// - retourner `Nack` si le device ne répond pas à l'adresse
/// - retourner `BusBusy` si une autre task tient le bus
///
/// Le trait n'impose PAS de lifetime, donc les impls peuvent être stateful
/// (`MockBus` script, `EspIdfBus` handle).
pub trait I2cBus {
    /// Write `wr` puis read `rd` en une seule transaction (repeated start).
    ///
    /// # Errors
    /// Retourne `I2cError` si la transaction échoue.
    fn write_read(&mut self, addr: u8, wr: &[u8], rd: &mut [u8]) -> Result<(), I2cError>;

    /// Write seul.
    ///
    /// # Errors
    /// Retourne `I2cError` si la transaction échoue.
    fn write(&mut self, addr: u8, wr: &[u8]) -> Result<(), I2cError>;

    /// Read seul.
    ///
    /// # Errors
    /// Retourne `I2cError` si la transaction échoue.
    fn read(&mut self, addr: u8, rd: &mut [u8]) -> Result<(), I2cError>;

    /// Retourne `true` si le bus semble sain (`SDA`+`SCL` high au repos).
    /// Les impls qui ne peuvent pas vérifier PEUVENT retourner `Ok(true)`.
    ///
    /// # Errors
    /// Retourne `I2cError` si la vérification échoue.
    fn probe_idle(&mut self) -> Result<bool, I2cError> {
        Ok(true)
    }

    /// Séquence de bus recovery (9 clock pulses `SCL` + `STOP`).
    /// Les impls qui ne la supportent pas PEUVENT no-op en `Ok(())`.
    ///
    /// # Errors
    /// Retourne `I2cError` si la récupération échoue.
    fn recover(&mut self) -> Result<(), I2cError> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn i2c_error_is_copy() {
        fn assert_copy<T: Copy>() {}
        assert_copy::<I2cError>();
        let e = I2cError::Nack;
        let copy = e;
        assert_eq!(e, copy);
        assert_eq!(e, I2cError::Nack);
    }

    #[test]
    fn i2c_error_discriminants_distinct() {
        assert_ne!(I2cError::Nack, I2cError::ArbLost);
        assert_ne!(I2cError::Timeout, I2cError::BusBusy);
        assert_ne!(I2cError::InvalidLength, I2cError::Hardware(0));
    }

    #[test]
    fn i2c_error_hardware_wraps_code() {
        let e = I2cError::Hardware(0xDEAD);
        assert!(matches!(e, I2cError::Hardware(0xDEAD)));
        if let I2cError::Hardware(code) = e {
            assert_eq!(code, 0xDEAD);
        }
    }
}
