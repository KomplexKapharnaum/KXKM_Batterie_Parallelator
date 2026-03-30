---
description: "Lancer un audit sécurité/fiabilité BMU sur firmware ESP32, FreeRTOS, WebServer, Influx et I2C avec findings priorisés."
name: "Audit BMU Safety"
argument-hint: "Portée de l'audit (ex: firmware/src/WebServerHandler.cpp, firmware/src/main.cpp)"
agent: "agent"
---

Réalise un audit ciblé BMU sur la portée suivante: ${input:Portée de l'audit (fichiers/dossiers ou 'firmware/src/**')}.

Objectif: identifier les risques de sécurité, fiabilité et régression fonctionnelle dans un firmware batterie parallèle ESP32.

Contraintes audit:
- Prioriser les findings par sévérité: Critique, Haute, Moyenne, Faible.
- Inclure des références de fichiers précises et vérifiables.
- Se concentrer sur: routes web mutantes, validation des index, I2C mutex, tâches FreeRTOS non bloquantes, cohérence des seuils de protection, mode TLS cloud.
- Ne pas proposer de weakening des protections batterie.

Format de sortie attendu:
1. Findings ordonnés par sévérité (avec impact + preuve).
2. Hypothèses / questions ouvertes.
3. Plan de correction minimal par ordre de priorité.
4. Tests à ajouter (bornes, concurrence, fault injection, soak test).

Contexte utile:
- [AGENTS](../../AGENTS.md)
- [Firmware Safety Instructions](../instructions/firmware-safety.instructions.md)
- [ML Pipeline Instructions](../instructions/ml-pipeline.instructions.md)