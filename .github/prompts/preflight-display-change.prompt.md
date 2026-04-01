---
description: "Pré-vol avant modification ESP-IDF display/touch (ILI9341/ILI934x + GT911): checklist lock LVGL, callbacks non bloquantes, séparation UI/protection, et frontières bus BSP/BMU."
name: "Preflight Display Change"
argument-hint: "Portée du changement (ex: firmware-idf/components/bmu_display/**)"
agent: "agent"
model: "GPT-5 (copilot)"
---

Réalise un pré-vol de modification UI display/touch sur la portée suivante : ${input:Portée du changement (ex: firmware-idf/components/bmu_display/**)}.

Objectif : vérifier les garde-fous critiques avant toute édition de code ESP-IDF UI/UX.

Consignes :
- Lire d'abord [AGENTS](../../AGENTS.md), [CLAUDE](../../CLAUDE.md), et [ESP-IDF Display Safety Rules](../instructions/firmware-idf-display-safety.instructions.md).
- Identifier les fichiers critiques à lire avant implémentation (init BSP, callbacks LVGL, input touch, pont vers état batterie).
- Produire une checklist pré-modif explicite sur les points suivants :
  - discipline de lock LVGL (`bsp_display_lock` / `bsp_display_unlock`)
  - UI non bloquante (callbacks/timers courts, pas de wait long)
  - séparation stricte UI vs protection batterie
  - frontière bus BSP (display/touch) vs bus BMU (I2C capteurs/actionneurs)
- Identifier les régressions probables sur wake/dim et interaction tactile.
- Ne pas écrire de code dans cette étape.

Format de sortie attendu :
1. Périmètre utile à lire
2. Checklist pré-modif (go/no-go)
3. Risques principaux
4. Plan court d'implémentation
5. Tests et validations avant/après

Références :
- [AGENTS](../../AGENTS.md)
- [CLAUDE](../../CLAUDE.md)
- [ESP-IDF Display Safety Rules](../instructions/firmware-idf-display-safety.instructions.md)
- [ESP-IDF migration design](../../docs/superpowers/specs/2026-03-30-esp-idf-migration-design.md)
- [Display dashboard spec](../../docs/superpowers/specs/2026-03-30-phase6-display-dashboard.md)
- [Display enhancement spec](../../docs/superpowers/specs/2026-03-31-phase8-display-enhanced.md)