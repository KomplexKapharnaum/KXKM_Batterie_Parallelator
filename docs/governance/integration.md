# Gouvernance Integration BMU

## Objectif
Cadre d'execution unifie pour firmware, QA, ML et documentation afin de garantir surete, tracabilite et progression continue.

## RACI
| Domaine | Responsable | Approbateur | Support |
|---|---|---|---|
| Securite firmware | Firmware BMU Team | BMU Safety Review | SE: Security |
| QA/CI | QA Team | QA Gate | SE: DevOps/CI |
| ML pipeline | ML Team | SE: Architect | Explore |
| Plans/docs | Tech Writer | Tech Lead | Explore |

## Regles d'execution
1. Toute tache fermee doit fournir une evidence (commande, artefact, diff).
2. Tout blocage doit inclure cause + prochaine action concrete.
3. Les routes mutatrices web restent sous controle securite strict.
4. Les sorties ML restent consultatives, jamais autorite de securite.

## Escalade
- P0 surete: BMU Safety Review immediat.
- P1 QA/CI: QA Gate + DevOps/CI.
- P2 doc/process: Tech Writer.
