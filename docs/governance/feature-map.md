# Feature Map BMU

## Inventaire F01-F11
| Feature | Priorite | Module | Test principal |
|---|---|---|---|
| F01 Mesure tension | MUST | INA_NRJ_lib | firmware/test/test_protection |
| F02 Mesure courant | MUST | INA_NRJ_lib | firmware/test/test_protection |
| F03 Coupure sur surtension | MUST | BatteryParallelator | firmware/test/test_protection |
| F04 Coupure sur sous-tension | MUST | BatteryParallelator | firmware/test/test_protection |
| F05 Coupure sur sur-courant | MUST | BatteryParallelator | firmware/test/test_protection |
| F06 Coupure sur desequilibre | MUST | BatteryParallelator | firmware/test/test_protection |
| F07 Reconnexion automatique | MUST | BatteryParallelator | firmware/test/test_protection |
| F08 Verrouillage permanent | MUST | BatteryParallelator | firmware/test/test_protection |
| F09 Observabilite logs | SHOULD | KxLogger/main | validation runtime |
| F10 Supervision web | COULD | WebServerHandler | firmware/test/test_web_route_security |
| F11 Support 16 batteries | MUST | main/BatteryManager | tests integration |

## Gaps prioritaires
1. Ajouter des tests integration sur routes web mutatrices.
2. Stabiliser la preuve CI distante pour cloture TASK-006.
3. Clarifier sim-host vs native dans tous les scripts et plans.