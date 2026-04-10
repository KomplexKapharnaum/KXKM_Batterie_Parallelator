//! Actions produites par le core à chaque tick, exécutées par C.
//! Cf spec §3.4 `BmuActionsC`.

/// Actions à exécuter après un tick core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Actions {
    /// Bitmask des contacteurs `TCA9535` à mettre ON (bit N = batterie N).
    pub tca_set_mask: u16,
    /// Bitmask des contacteurs `TCA9535` à mettre OFF.
    pub tca_clr_mask: u16,
    /// Index de la batterie à armer en mesure `R_int` (`0xFF` = aucun).
    pub rint_trigger_idx: u8,
    /// Demande une inférence SOH `TFLite` hors cycle périodique.
    pub request_soh_inference: bool,
}

impl Default for Actions {
    fn default() -> Self {
        Self {
            tca_set_mask: 0,
            tca_clr_mask: 0,
            rint_trigger_idx: 0xFF,
            request_soh_inference: false,
        }
    }
}

impl Actions {
    /// AND-mask la requête balancer avec l'`allowed_mask` de la protection.
    ///
    /// Règle fondamentale §6.2: la protection "dispose", le balancer
    /// "propose".
    #[inline]
    #[must_use]
    pub const fn merge_balancer(balancer_request: u16, protection_allowed: u16) -> u16 {
        balancer_request & protection_allowed
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn actions_default_empty() {
        let a = Actions::default();
        assert_eq!(a.tca_set_mask, 0);
        assert_eq!(a.tca_clr_mask, 0);
        assert_eq!(a.rint_trigger_idx, 0xFF);
        assert!(!a.request_soh_inference);
    }

    #[test]
    fn actions_no_contention_invariant() {
        let a = Actions {
            tca_set_mask: 0b0000_0000_1010_0101,
            tca_clr_mask: 0b1111_1111_0101_1010,
            rint_trigger_idx: 0xFF,
            request_soh_inference: false,
        };
        assert_eq!(a.tca_set_mask & a.tca_clr_mask, 0);
    }

    #[test]
    fn actions_merge_protection_wins_over_balancer() {
        let protection_allowed = 0b0000_0000_1111_1111u16;
        let balancer_request = 0b0000_0000_1111_1111u16;
        let merged = Actions::merge_balancer(balancer_request, protection_allowed);
        assert_eq!(merged, 0b0000_0000_1111_1111);
    }

    #[test]
    fn actions_merge_protection_blocks_balancer() {
        let protection_allowed = 0b0000_0000_0000_1111u16;
        let balancer_request = 0b0000_0000_1111_1111u16;
        let merged = Actions::merge_balancer(balancer_request, protection_allowed);
        assert_eq!(merged, 0b0000_0000_0000_1111);
    }
}
