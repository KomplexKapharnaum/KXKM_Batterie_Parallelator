---
description: "Gate pre-merge sécurité firmware: exécute tests sim-host, build ciblé, budget mémoire et audit rapide des routes mutables."
name: "Pre-Merge Safety Gate"
argument-hint: "Portée optionnelle (ex: firmware/src/**, firmware-idf/**)"
agent: "agent"
model: "GPT-5 (copilot)"
---

Exécute une gate pre-merge sécurité sur la portée: ${input:Portée optionnelle (ex: firmware/src/**, firmware-idf/**)}.

Objectif:
- empêcher une régression de sécurité/sûreté avant PR.

Routine minimale à lancer:
1. Tests hôte: pio test -e sim-host.
2. Build firmware cible: pio run -e kxkm-s3-16MB.
3. Budget mémoire: scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85.
4. Audit routes mutables: vérifier que les endpoints de mutation batterie appliquent auth token, rate-limit et validation d’index.

Checklist d’analyse routes mutables:
- Vérifier l’enregistrement des routes de mutation (switch_on/switch_off, HTTP et WebSocket si mutation).
- Vérifier l’usage de WebRouteSecurity::isMutationTokenAuthorized.
- Vérifier l’usage de WebMutationRateLimit::mutationRateLimitExceeded.
- Vérifier l’usage de BatteryRouteValidation pour index et préconditions tension.

Format de sortie attendu:
1. Verdict global: PASS | CONDITIONAL PASS | FAIL
2. Résultat des commandes (succès/échec + points saillants)
3. Findings sécurité/sûreté (Critique -> Faible)
4. Risques résiduels
5. Go/No-Go merge

Contraintes:
- Ne pas modifier les seuils de protection ni relâcher les contrôles fail-safe.
- S’appuyer sur les règles de AGENTS et les instructions sécurité firmware/web.

Références:
- [AGENTS](../../AGENTS.md)
- [Firmware Safety Instructions](../instructions/firmware-safety.instructions.md)
- [WebServer Safety Instructions](../instructions/webserver-safety.instructions.md)
- [CI README](../../scripts/ci/README.md)
