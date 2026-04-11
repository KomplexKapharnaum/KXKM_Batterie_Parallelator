//! Newtype wrappers pour les unités électriques.
//! Empêchent by-design la confusion mV/V et mA/A (CRIT-A du spec §5.3).

/// `Millivolts` signés. Jamais confondus avec Volts ou `Milliamps`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Millivolts(i32);

impl Millivolts {
    /// Valeur nulle.
    pub const ZERO: Self = Self(0);

    /// Construit un `Millivolts` depuis une valeur brute en mV.
    #[inline]
    #[must_use]
    pub const fn from_raw(mv: i32) -> Self {
        Self(mv)
    }

    /// Retourne la valeur brute en mV.
    #[inline]
    #[must_use]
    pub const fn as_raw(self) -> i32 {
        self.0
    }

    /// Différence absolue entre deux `Millivolts`, en mV non signé.
    /// Symétrique et correct sur tout le domaine `i32` (utilise
    /// `i32::abs_diff` qui retourne `u32`, évitant la troncature à
    /// `i32::MAX` de l'ancienne implémentation `saturating_sub`).
    #[inline]
    #[must_use]
    pub const fn abs_diff(self, other: Self) -> u32 {
        self.0.abs_diff(other.0)
    }
}

/// `Milliamps` signés (positif = discharge par convention projet KXKM).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Milliamps(i32);

impl Milliamps {
    /// Valeur nulle.
    pub const ZERO: Self = Self(0);

    /// Construit un `Milliamps` depuis une valeur brute en mA.
    #[inline]
    #[must_use]
    pub const fn from_raw(ma: i32) -> Self {
        Self(ma)
    }

    /// Retourne la valeur brute en mA.
    #[inline]
    #[must_use]
    pub const fn as_raw(self) -> i32 {
        self.0
    }

    /// Valeur absolue en mA non signé.
    #[inline]
    #[must_use]
    pub const fn abs(self) -> u32 {
        self.0.unsigned_abs()
    }
}

/// `Milliohms` non signés. `UNKNOWN` = valeur sentinelle `u32::MAX`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Milliohms(u32);

impl Milliohms {
    /// Sentinelle indiquant une résistance inconnue (`u32::MAX`).
    pub const UNKNOWN: Self = Self(u32::MAX);

    /// Construit un `Milliohms` depuis une valeur brute en mΩ.
    #[inline]
    #[must_use]
    pub const fn from_raw(r: u32) -> Self {
        Self(r)
    }

    /// Retourne la valeur brute en mΩ.
    #[inline]
    #[must_use]
    pub const fn as_raw(self) -> u32 {
        self.0
    }

    /// Vrai si la valeur est connue (différente de la sentinelle `u32::MAX`).
    #[inline]
    #[must_use]
    pub const fn is_known(self) -> bool {
        self.0 != u32::MAX
    }
}

/// Milliampères-heures signés (charge accumulée, peut être négatif).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct MilliampHours(i32);

impl MilliampHours {
    /// Valeur nulle.
    pub const ZERO: Self = Self(0);

    /// Construit un `MilliampHours` depuis une valeur brute en mAh.
    #[inline]
    #[must_use]
    pub const fn from_raw(mah: i32) -> Self {
        Self(mah)
    }

    /// Retourne la valeur brute en mAh.
    #[inline]
    #[must_use]
    pub const fn as_raw(self) -> i32 {
        self.0
    }

    /// Addition saturante : ne déborde pas au-delà de `i32::MAX`.
    #[inline]
    #[must_use]
    pub const fn saturating_add(self, other: Self) -> Self {
        Self(self.0.saturating_add(other.0))
    }

    /// Soustraction saturante : ne déborde pas en-dessous de `i32::MIN`.
    #[inline]
    #[must_use]
    pub const fn saturating_sub(self, other: Self) -> Self {
        Self(self.0.saturating_sub(other.0))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn millivolts_from_raw_roundtrip() {
        let v = Millivolts::from_raw(24500);
        assert_eq!(v.as_raw(), 24500);
    }

    #[test]
    fn millivolts_zero_constant() {
        assert_eq!(Millivolts::ZERO.as_raw(), 0);
    }

    #[test]
    fn millivolts_ordering() {
        assert!(Millivolts::from_raw(24000) < Millivolts::from_raw(25000));
        assert!(Millivolts::from_raw(-500) < Millivolts::ZERO);
    }

    #[test]
    fn millivolts_abs_diff_positive() {
        let a = Millivolts::from_raw(27000);
        let b = Millivolts::from_raw(25500);
        assert_eq!(a.abs_diff(b), 1500);
        assert_eq!(b.abs_diff(a), 1500);
    }

    #[test]
    fn millivolts_abs_diff_spans_zero() {
        let a = Millivolts::from_raw(-100);
        let b = Millivolts::from_raw(200);
        assert_eq!(a.abs_diff(b), 300);
    }

    /// Régression : l'ancienne impl `saturating_sub(other).unsigned_abs()`
    /// tronquait à `i32::MAX` quand le calcul traversait 0 sur des valeurs
    /// extrêmes, donnant `2^31 - 1` au lieu de `2^31` et cassant la symétrie.
    /// `i32::abs_diff` retourne `u32` et ne souffre pas de ce bug.
    #[test]
    fn millivolts_abs_diff_symmetric_at_i32_extremes() {
        let min = Millivolts::from_raw(i32::MIN);
        let zero = Millivolts::ZERO;
        // |0 - i32::MIN| = 2^31 = 2_147_483_648
        assert_eq!(zero.abs_diff(min), 2_147_483_648_u32);
        assert_eq!(min.abs_diff(zero), 2_147_483_648_u32);
        // Symétrie aussi sur le haut
        let max = Millivolts::from_raw(i32::MAX);
        assert_eq!(max.abs_diff(min), u32::MAX);
        assert_eq!(min.abs_diff(max), u32::MAX);
    }

    #[test]
    fn milliamps_from_raw_signed() {
        let charge = Milliamps::from_raw(-1500);
        let discharge = Milliamps::from_raw(2000);
        assert_eq!(charge.as_raw(), -1500);
        assert_eq!(discharge.as_raw(), 2000);
    }

    #[test]
    fn milliamps_abs() {
        assert_eq!(Milliamps::from_raw(-1500).abs(), 1500);
        assert_eq!(Milliamps::from_raw(1500).abs(), 1500);
        assert_eq!(Milliamps::from_raw(0).abs(), 0);
    }

    #[test]
    fn milliohms_nonneg() {
        let r = Milliohms::from_raw(42);
        assert_eq!(r.as_raw(), 42);
        assert!(Milliohms::UNKNOWN.as_raw() == u32::MAX);
    }

    #[test]
    fn milliamp_hours_roundtrip() {
        let ah = MilliampHours::from_raw(4500);
        assert_eq!(ah.as_raw(), 4500);
    }

    #[test]
    fn milliamp_hours_saturating_add() {
        let a = MilliampHours::from_raw(i32::MAX - 10);
        let b = MilliampHours::from_raw(100);
        assert_eq!(a.saturating_add(b).as_raw(), i32::MAX);
    }

    #[test]
    fn milliamp_hours_saturating_sub() {
        let a = MilliampHours::from_raw(i32::MIN + 10);
        let b = MilliampHours::from_raw(100);
        assert_eq!(a.saturating_sub(b).as_raw(), i32::MIN);
    }
}
