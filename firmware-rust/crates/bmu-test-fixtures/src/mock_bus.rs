//! `MockBus` scripted pour tests host.

use bmu_i2c::{I2cBus, I2cError};
use std::collections::VecDeque;

#[derive(Debug, Clone)]
enum Transaction {
    WriteRead { addr: u8, wr: Vec<u8>, rd: Vec<u8> },
    Write { addr: u8, wr: Vec<u8> },
    Read { addr: u8, rd: Vec<u8> },
    Error(I2cError),
}

/// Bus I²C scripted pour tests host.
///
/// Les transactions attendues sont enfilées via `expect_write_read()`,
/// `expect_write()`, `expect_read()` ou `expect_error()`, puis rejouées
/// dans l'ordre par les méthodes du trait `I2cBus`.
///
/// Codes d'erreur spéciaux renvoyés par `MockBus` :
/// - `Hardware(0xBAD0)` : script épuisé (plus de transaction attendue)
/// - `Hardware(0xBAD1)` : mismatch adresse
/// - `Hardware(0xBAD2)` : mismatch write buffer
/// - `Hardware(0xBAD3)` : mismatch buffer length
/// - `Hardware(0xBAD4)` : mauvais type de transaction
#[derive(Debug, Default)]
pub struct MockBus {
    script: VecDeque<Transaction>,
    calls: usize,
}

impl MockBus {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn expect_write_read(&mut self, addr: u8, wr: Vec<u8>, rd: Vec<u8>) {
        self.script
            .push_back(Transaction::WriteRead { addr, wr, rd });
    }

    pub fn expect_write(&mut self, addr: u8, wr: Vec<u8>) {
        self.script.push_back(Transaction::Write { addr, wr });
    }

    pub fn expect_read(&mut self, addr: u8, rd: Vec<u8>) {
        self.script.push_back(Transaction::Read { addr, rd });
    }

    pub fn expect_error(&mut self, err: I2cError) {
        self.script.push_back(Transaction::Error(err));
    }

    #[must_use]
    pub fn call_count(&self) -> usize {
        self.calls
    }

    #[must_use]
    pub fn script_remaining(&self) -> usize {
        self.script.len()
    }
}

#[allow(clippy::print_stderr)]
impl I2cBus for MockBus {
    fn write_read(&mut self, addr: u8, wr: &[u8], rd: &mut [u8]) -> Result<(), I2cError> {
        let tx = self.script.pop_front().ok_or(I2cError::Hardware(0xBAD0))?;
        self.calls = self.calls.saturating_add(1);
        match tx {
            Transaction::WriteRead {
                addr: a,
                wr: w,
                rd: r,
            } => {
                if a != addr {
                    eprintln!(
                        "[MockBus] addr mismatch (call #{}): expected 0x{:02X}, got 0x{:02X}",
                        self.calls, a, addr
                    );
                    return Err(I2cError::Hardware(0xBAD1));
                }
                if w != wr {
                    eprintln!(
                        "[MockBus] write mismatch (call #{}): expected {:02X?}, got {:02X?}",
                        self.calls, w, wr
                    );
                    return Err(I2cError::Hardware(0xBAD2));
                }
                if r.len() != rd.len() {
                    eprintln!(
                        "[MockBus] read length mismatch (call #{}): expected {}, got {}",
                        self.calls,
                        r.len(),
                        rd.len()
                    );
                    return Err(I2cError::Hardware(0xBAD3));
                }
                rd.copy_from_slice(&r);
                Ok(())
            }
            Transaction::Error(e) => Err(e),
            other => {
                eprintln!(
                    "[MockBus] wrong transaction type (call #{}): expected WriteRead, got {:?}",
                    self.calls, other
                );
                Err(I2cError::Hardware(0xBAD4))
            }
        }
    }

    fn write(&mut self, addr: u8, wr: &[u8]) -> Result<(), I2cError> {
        let tx = self.script.pop_front().ok_or(I2cError::Hardware(0xBAD0))?;
        self.calls = self.calls.saturating_add(1);
        match tx {
            Transaction::Write { addr: a, wr: w } => {
                if a != addr {
                    eprintln!(
                        "[MockBus] addr mismatch (call #{}): expected 0x{:02X}, got 0x{:02X}",
                        self.calls, a, addr
                    );
                    return Err(I2cError::Hardware(0xBAD1));
                }
                if w != wr {
                    eprintln!(
                        "[MockBus] write mismatch (call #{}): expected {:02X?}, got {:02X?}",
                        self.calls, w, wr
                    );
                    return Err(I2cError::Hardware(0xBAD2));
                }
                Ok(())
            }
            Transaction::Error(e) => Err(e),
            other => {
                eprintln!(
                    "[MockBus] wrong transaction type (call #{}): expected Write, got {:?}",
                    self.calls, other
                );
                Err(I2cError::Hardware(0xBAD4))
            }
        }
    }

    fn read(&mut self, addr: u8, rd: &mut [u8]) -> Result<(), I2cError> {
        let tx = self.script.pop_front().ok_or(I2cError::Hardware(0xBAD0))?;
        self.calls = self.calls.saturating_add(1);
        match tx {
            Transaction::Read { addr: a, rd: r } => {
                if a != addr {
                    eprintln!(
                        "[MockBus] addr mismatch (call #{}): expected 0x{:02X}, got 0x{:02X}",
                        self.calls, a, addr
                    );
                    return Err(I2cError::Hardware(0xBAD1));
                }
                if r.len() != rd.len() {
                    eprintln!(
                        "[MockBus] read length mismatch (call #{}): expected {}, got {}",
                        self.calls,
                        r.len(),
                        rd.len()
                    );
                    return Err(I2cError::Hardware(0xBAD3));
                }
                rd.copy_from_slice(&r);
                Ok(())
            }
            Transaction::Error(e) => Err(e),
            other => {
                eprintln!(
                    "[MockBus] wrong transaction type (call #{}): expected Read, got {:?}",
                    self.calls, other
                );
                Err(I2cError::Hardware(0xBAD4))
            }
        }
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::panic)]
mod tests {
    use super::*;

    #[test]
    fn mock_bus_write_read_success() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![0x05], vec![0x12, 0x34]);

        let mut rd = [0u8; 2];
        bus.write_read(0x40, &[0x05], &mut rd).unwrap();
        assert_eq!(rd, [0x12, 0x34]);
    }

    #[test]
    fn mock_bus_write_success() {
        let mut bus = MockBus::new();
        bus.expect_write(0x20, vec![0x06, 0xF0]);
        bus.write(0x20, &[0x06, 0xF0]).unwrap();
    }

    #[test]
    fn mock_bus_read_success() {
        let mut bus = MockBus::new();
        bus.expect_read(0x38, vec![0x1C, 0x5A]);
        let mut rd = [0u8; 2];
        bus.read(0x38, &mut rd).unwrap();
        assert_eq!(rd, [0x1C, 0x5A]);
    }

    #[test]
    fn mock_bus_inject_nack() {
        let mut bus = MockBus::new();
        bus.expect_error(I2cError::Nack);
        let mut rd = [0u8; 2];
        assert_eq!(bus.write_read(0x40, &[0x05], &mut rd), Err(I2cError::Nack));
    }

    #[test]
    fn mock_bus_address_mismatch_fails() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![0x05], vec![0, 0]);
        let mut rd = [0u8; 2];
        assert_eq!(
            bus.write_read(0x41, &[0x05], &mut rd),
            Err(I2cError::Hardware(0xBAD1))
        );
    }

    #[test]
    fn mock_bus_exhausted_returns_hardware_error() {
        let mut bus = MockBus::new();
        let mut rd = [0u8; 2];
        assert_eq!(
            bus.write_read(0x40, &[0x05], &mut rd),
            Err(I2cError::Hardware(0xBAD0))
        );
    }

    #[test]
    fn mock_bus_records_call_count() {
        let mut bus = MockBus::new();
        bus.expect_write_read(0x40, vec![0x05], vec![0, 0]);
        bus.expect_write_read(0x40, vec![0x05], vec![0, 0]);
        let mut rd = [0u8; 2];
        bus.write_read(0x40, &[0x05], &mut rd).unwrap();
        bus.write_read(0x40, &[0x05], &mut rd).unwrap();
        assert_eq!(bus.call_count(), 2);
    }

    #[test]
    fn mock_bus_probe_idle_always_ok() {
        let mut bus = MockBus::new();
        assert_eq!(bus.probe_idle(), Ok(true));
    }

    #[test]
    fn mock_bus_recover_noop() {
        let mut bus = MockBus::new();
        assert_eq!(bus.recover(), Ok(()));
    }
}
