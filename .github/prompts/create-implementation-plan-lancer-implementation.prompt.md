---
description: "Créer un plan d'implémentation actionnable puis lancer immédiatement l'implémentation avec validations et preuves."
name: "Create Implementation Plan + Lancer Implementation"
argument-hint: "Portée et objectif (ex: plan/refactor-safety-core-web-remote-1.md + TASK-010..014)"
agent: "agent"
---

Tu vas exécuter un workflow en 2 étapes sur: ${input:Portée et objectif (ex: plan/refactor-safety-core-web-remote-1.md + TASK-010..014)}.

## Étape 1 — Plan d'implémentation exécutable
- Produire un mini-plan orienté exécution (pas de brainstorming long).
- Identifier les TODO actionnables immédiatement.
- Définir pour chaque item: action, fichier cible, validation, preuve attendue.
- Marquer les dépendances et bloqueurs.

## Étape 2 — Lancer l'implémentation maintenant
- Implémenter tout de suite le premier lot prioritaire.
- Mettre à jour les statuts TODO/plan au fur et à mesure.
- Exécuter les validations pertinentes (build/tests/gates).
- Capturer les preuves (commande, résultat, artefact/fichier).

## Format de sortie attendu
1. Plan court (3 à 7 items max)
2. Exécution réalisée (fichiers modifiés + pourquoi)
3. Validations exécutées et résultats
4. Preuves collectées
5. Reste à faire priorisé

## Règles
- Si l'utilisateur dit "start implementation" ou "lancer l'implémentation", ne pas rester en mode plan-only.
- Ne pas déclarer terminé tant qu'il reste des bloqueurs critiques non traités.
- Ne pas inventer de preuves.
- Préférer des incréments petits et vérifiables.

## Références
- [Prompt audit quotidien](./plan-audit-quotidien.prompt.md)
- [Instruction Plan TODO](../instructions/plan-todo-implementation.instructions.md)