---
description: "Normaliser les TODO journaliers dans docs/plans avec statut, priorité, preuves et prochaines actions."
name: "Plan Audit Quotidien"
argument-hint: "Chemin du fichier plan (ex: docs/plans/2026-03-28.md)"
agent: "agent"
---

Analyse et normalise le plan journalier situé ici: ${input:Chemin du fichier plan (ex: docs/plans/2026-03-28.md)}.

Objectif:
- Standardiser tous les TODO pour une revue quotidienne rapide et actionnable.

Règles de normalisation:
- Créer un identifiant stable par item: TODO-001, TODO-002, etc.
- Uniformiser les statuts: not-started, in-progress, blocked, completed.
- Ajouter une priorité: P0, P1, P2, P3.
- Ajouter une colonne Evidence avec preuve vérifiable (fichier, commande, sortie, artefact).
- Ajouter une colonne Next Action avec une action concrète, unique et immédiatement exécutable.
- Dédupliquer les TODO équivalents.
- Signaler toute ambiguïté dans une section Hypothèses.

Format de sortie attendu:
1. Résumé journalier (5 lignes max)
2. Tableau TODO normalisé
3. Blocages et dépendances
4. Check de cohérence (manques de preuve, statuts incohérents, TODO sans prochaine action)
5. Proposition de patch du fichier cible

Contraintes:
- Ne pas inventer de preuves.
- Si une information manque, marquer explicitement UNKNOWN.
- Préserver le sens métier original.

Références utiles:
- [Plan principal](../../plan/refactor-safety-core-web-remote-1.md)
- [Workflow firmware](../instructions/firmware-workflow.instructions.md)