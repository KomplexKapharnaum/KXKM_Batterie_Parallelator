---
description: "Pré-vol avant modification sensible du BMU: lire les fichiers critiques, rappeler les garde-fous safety et proposer un plan court avant implémentation."
name: "Preflight BMU Change"
argument-hint: "Portée du changement (ex: firmware/src/WebServerHandler.cpp ou firmware/src/**)"
agent: "agent"
model: "GPT-5 (copilot)"
---

Réalise un pré-vol de modification sur la portée suivante : ${input:Portée du changement (ex: firmware/src/WebServerHandler.cpp ou firmware/src/**)}.

Objectif : préparer une intervention sûre et minimale avant toute édition dans ce dépôt BMU ESP32.

Consignes :
- Lire d'abord [AGENTS](../../AGENTS.md) et les instructions ciblées pertinentes dans [instructions](../instructions/).
- Identifier les fichiers critiques, les dépendances proches et les tests existants à consulter avant changement.
- Rappeler les garde-fous non négociables : topologie INA/TCA, protections batterie, I2CLockGuard, routes web mutantes authentifiées, tâches non bloquantes.
- Repérer les risques de régression probables pour la portée fournie.
- Ne pas écrire de code dans cette étape.

Format de sortie attendu :
1. Périmètre utile à lire.
2. Garde-fous à préserver.
3. Risques principaux.
4. Plan court d'implémentation.
5. Tests et validations à exécuter avant et après modification.

Références de départ :
- [AGENTS](../../AGENTS.md)
- [CLAUDE](../../CLAUDE.md)
- [Firmware Safety Instructions](../instructions/firmware-safety.instructions.md)
- [README](../../README.md)