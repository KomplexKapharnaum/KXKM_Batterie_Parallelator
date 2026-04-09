# Sim-Host Tests (PlatformIO)

13 tests Unity host-based, exécutés via `pio test -e sim-host`. Pas de hardware requis.

## Running

```bash
pio test -e sim-host                # toutes les suites
pio test -e sim-host -f test_X      # une suite
pio test -e sim-host -vv            # verbose
```

## Suites

| Suite | Tests | Couvre |
|-------|------:|--------|
| `test_protection` | 10 | V/I/imbalance/lock state machine |
| `test_battery_route_validation` | - | Index bounds + state checks |
| `test_influx_buffer_codec` | - | InfluxDB line-protocol |
| `test_web_mutation_rate_limit` | 3 | Sliding window + multi-IP |
| `test_web_route_security` | 9 | Constant-time token + Bearer |
| `test_ws_auth_flow` | 7 | Auth + rate limit combinés |
| `test_mqtt_influx_codec` | 3 | JSON MQTT + topic parsing |
| `test_emulation_bench` | - | Emulation benchmark |

Tests `test_tca_ina*` : hardware-only, ERRORED en sim-host = normal (ignorer).

## CRITICAL: build_src_filter

Le `[env:sim-host]` dans `platformio.ini` a un `build_src_filter` qui liste explicitement les `.cpp` compilés. **Quand tu ajoutes un nouveau module testable**, tu DOIS :

1. Ajouter le `.cpp` au `build_src_filter` de `[env:sim-host]`
2. Sinon le linker échoue : `undefined reference to ...`

Exemple :
```ini
[env:sim-host]
build_src_filter =
  +<*>
  -<.git/>
  -<.svn/>
  +<../src/MonNouveauModule.cpp>
```

## Pattern

```cpp
#include <unity.h>
void setUp() {}
void tearDown() {}
void test_my_func() { TEST_ASSERT_EQUAL(42, my_func()); }
int main() { UNITY_BEGIN(); RUN_TEST(test_my_func); return UNITY_END(); }
```

## Anti-Patterns

- Ne pas dépendre des libs Arduino (Wire, etc.) — stub manuellement
- Ne pas tester l'I2C réel — mock les handlers
- Ne pas mélanger code de test et code source dans le même fichier
