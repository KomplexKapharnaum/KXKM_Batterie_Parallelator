# ESP-IDF Host Tests

Tests Unity exécutés sur host (Linux/macOS), pas de hardware. **13 suites**, build via `cd test && make all` ou `idf.py build` dans la suite.

## Running

```bash
cd firmware-idf/test
make all                    # toutes les suites
make test_protection        # une suite
```

## Suites

| Suite | Couvre |
|-------|--------|
| `test_protection` | State machine V/I/imbalance/lock (13 tests) |
| `test_ble_victron` | Battery/solar payload encoding |
| `test_victron_gatt` | GATT encoding (8 tests : V, SOC, alarm, TTG, T) |
| `test_victron_scan` | Scan parsing (5 tests : solar, battery, expiry, MAC, CID) |
| `test_vedirect_parser` | VE.Direct frame parsing |
| `test_ble_soh` | SOH prediction |
| `test_config_labels` | Battery label management |
| `test_rint` | R_int calculation |
| `test_vrm_topics` | VRM topic generation |

## Pattern

```cpp
#include "unity.h"
void setUp(void) {}
void tearDown(void) {}
void test_my_thing(void) { TEST_ASSERT_EQUAL(42, my_func()); }
int main(void) { UNITY_BEGIN(); RUN_TEST(test_my_thing); return UNITY_END(); }
```

## Anti-Patterns

- Ne pas dépendre de `idf.py build` réel — les tests host ont leur propre Makefile
- Ne pas lier au stack ESP-IDF complet — stub I2C/BLE manuellement
- Tests hardware (`test_tca_ina`) : ERRORED en host est normal, ignorer
