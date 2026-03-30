---
goal: "Plan Wizard Agents Coordination: 1 agent réel, installation et QA CI/CD par environnement"
version: "1.1"
date_created: "2026-03-29"
last_updated: "2026-03-29"
owner: "Firmware BMU Team"
status: "In progress"
tags: ["process", "ci", "cd", "qa", "github-actions", "platformio"]
---

# Introduction

![Status: In progress](https://img.shields.io/badge/status-In%20progress-yellow)

Ce plan formalise une exécution réelle (pas de simulation) avec une orchestration unique, puis des contrôles QA CI/CD séparés par environnement PlatformIO.

## 1. Requirements & Constraints

- **REQ-001**: Exécuter QA CI/CD par environnement distinct (`sim-host`, `kxkm-s3-16MB`, `kxkm-v3-16MB`).
- **REQ-002**: Inclure une étape d’installation outillée explicite (Python + PlatformIO).
- **REQ-003**: Inclure un garde-fou mémoire pour l’environnement production S3.
- **CON-001**: Ne pas réduire les contrôles de sécurité firmware existants.
- **CON-002**: Garder des jobs reproductibles sur `ubuntu-latest`.
- **PAT-001**: Utiliser une matrice CI pour éviter la duplication de logique.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Mettre en place un workflow QA multi-environnements exécutable à chaque push/PR.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Créer une matrice `qa-by-environment` dans `.github/workflows/sim-host-tests.yml`. | ✅ | 2026-03-29 |
| TASK-002 | Ajouter installation outillée (`scripts/ci/install_tooling.sh`). | ✅ | 2026-03-29 |
| TASK-003 | Ajouter cache PlatformIO (`actions/cache`) pour stabiliser les temps CI. | ✅ | 2026-03-29 |
| TASK-004 | Ajouter commandes QA par environnement via `scripts/ci/run_qa_env.sh`. | ✅ | 2026-03-29 |
| TASK-005 | Tester localement l’exécution QA (`sim-host`, `kxkm-s3-memory-budget`). | ✅ | 2026-03-29 |

### Implementation Phase 2

- GOAL-002: Vérifier la conformité et l’observabilité des gates.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-006 | Exécuter le workflow sur PR/push et collecter les preuves GitHub Actions pour chaque environnement. | blocked | 2026-03-29 |

## 3. Dependencies

- **DEP-001**: GitHub Actions runner Ubuntu.
- **DEP-002**: PlatformIO + environnements définis dans `platformio.ini`.
- **DEP-003**: Script `scripts/check_memory_budget.sh`.

## 4. Files

- **FILE-001**: `.github/workflows/sim-host-tests.yml` — pipeline QA CI/CD par environnement.
- **FILE-002**: `scripts/ci/install_tooling.sh` — installation outillage.
- **FILE-003**: `scripts/ci/run_qa_env.sh` — orchestration QA par environnement.
- **FILE-004**: `docs/QA_CICD_ENVIRONNEMENTS.md` — documentation opératoire.


## 5. Evidence

- `scripts/ci/install_tooling.sh` -> PlatformIO détecté/installe selon contexte.
- `scripts/ci/run_qa_env.sh sim-host` -> OK.
- `scripts/ci/run_qa_env.sh kxkm-s3-memory-budget` -> OK (RAM 8.2%, Flash 29.1%).
## Execution Update — 2026-03-29 (Go: full local QA coverage)

- Exécution réelle terminée sur tous les environnements QA locaux via `scripts/ci/run_qa_env.sh` :
- `sim-host` ✅
- `kxkm-s3-build` ✅
- `kxkm-v3-build` ✅
- `kxkm-s3-memory-budget` ✅

### Evidence

- `scripts/ci/install_tooling.sh` -> PlatformIO détecté (`6.1.19`)
- `scripts/ci/run_qa_env.sh sim-host` -> PASS
- `scripts/ci/run_qa_env.sh kxkm-s3-build` -> PASS
- `scripts/ci/run_qa_env.sh kxkm-v3-build` -> PASS (RAM 9.5%, Flash 28.9%)
- `scripts/ci/run_qa_env.sh kxkm-s3-memory-budget` -> PASS (RAM 8.2%, Flash 29.1%)

### Remaining (remote CI proof only)

- Déclencher un push/PR pour exécuter `.github/workflows/sim-host-tests.yml` et collecter les preuves GitHub Actions par environnement.
## Execution Update — 2026-03-29 (Create-Implementation-Plan-Lancer-Implementation)

- TASK-006: `blocked` (preuve CI distante non collectable immédiatement).
- Validation locale complète relancée avec succès sur tous les environnements via `bash scripts/ci/run_qa_all.sh`.
- Outillage ajouté pour collecte de preuve distante: `scripts/ci/collect_remote_qa_evidence.sh`.

### Evidence

- `bash scripts/ci/run_qa_all.sh` -> PASS (`sim-host`, `kxkm-s3-build`, `kxkm-v3-build`, `kxkm-s3-memory-budget`).
- `gh workflow list --repo KomplexKapharnaum/KXKM_Batterie_Parallelator` -> workflows visibles: `CI`, `Copilot code review`, `Copilot coding agent` (pas de `qa-cicd-environments`).
- `gh workflow run .github/workflows/sim-host-tests.yml --ref object-orriented` -> HTTP 404 (workflow absent de la branche par défaut distante).
- `gh workflow run CI --ref object-orriented` -> HTTP 422 (`workflow_dispatch` non activé).
- `bash scripts/ci/collect_remote_qa_evidence.sh qa-cicd-environments object-orriented 10` -> exit `4` avec message "Aucun run trouve".

### Next Action

- Ouvrir PR contenant `.github/workflows/sim-host-tests.yml` (nom job `qa-cicd-environments`) vers `master`, puis relancer:
```bash
bash scripts/ci/collect_remote_qa_evidence.sh qa-cicd-environments object-orriented 10
```
- Après premier run distant visible, basculer TASK-006 à `completed` avec URLs de runs.
## Execution Update — 2026-03-29 (Remote proof snapshot automation)

- Nouveau lot implémenté: automatisation de capture de preuve distante via `scripts/ci/request_remote_qa_proof.sh`.
- Artefact généré: `docs/QA_REMOTE_PROOF_LATEST.md` (snapshot UTC + inventaire workflows + diagnostic dispatch).
- TASK-006 reste `blocked` tant que le workflow `qa-cicd-environments` n’est pas présent côté branche par défaut distante.

### Evidence

- `bash -n scripts/ci/request_remote_qa_proof.sh` -> `OK`.
- `bash scripts/ci/request_remote_qa_proof.sh KomplexKapharnaum/KXKM_Batterie_Parallelator object-orriented qa-cicd-environments docs/QA_REMOTE_PROOF_LATEST.md` -> fichier généré.
- Snapshot courant: `Dispatch status: blocked`; workflows distants listés: `CI`, `Copilot code review`, `Copilot coding agent`.

### Next Action

- Publier `.github/workflows/sim-host-tests.yml` sur `master` (ou via PR) pour rendre `qa-cicd-environments` visible à distance.
- Relancer ensuite `scripts/ci/request_remote_qa_proof.sh` puis marquer TASK-006 `completed` avec URLs de runs.
## Execution Update — 2026-03-30 (analyse approfondie + gouvernance projet)

- Consolidation des analyses multi-composants via agents: `BMU Safety Review`, `QA Gate`, `Explore`.
- Blocker QA principal maintenu: preuve CI distante complete non stabilisee (TASK-006).
- Nouveaux artefacts de pilotage crees dans `docs/` : gouvernance, diagrammes Mermaid, feature map, assignation agents/taches.
- Script de synchronisation contexte ajoute: `scripts/project/update-context-snapshot.sh`.

### Evidence

- Analyse surete: findings High/Medium classes avec recommandations actionnables.
- Analyse QA: verdict global `FAIL` tant que preuves distantes/alignement workflows non finalises.
- Cartographie projet: structure documentaire cible et artefacts manquants identifies puis crees.


### Next Action (P0)

- Aligner les workflows CI sur `main` et produire un run distant verifiable (URL + statut) pour cloturer TASK-006.
- Verifier env `sim-host` vs `native` dans `platformio.ini` et scripts CI pour supprimer l'ambiguite.
## Audit Quotidien — 2026-03-30 (delta coherence CI)

### Statut critique

- TASK-006 reste `blocked` tant que la preuve distante n'est pas archivee avec URL de run CI.

### Evidence fraiche

- Verification cloud/containers: `scripts/ml/remote_kxkm_ai_pipeline.sh check` -> host + conteneurs OK.
- Le blocage dataset est confirme cote conteneur (`CONTAINER_DATASET_MISSING`), ce qui n'impacte pas la nature `blocked` de TASK-006 mais confirme l'absence de preuve distante complete.

### Next Action unique

- Generer un run CI distant sur la branche par defaut et archiver les URLs de runs dans ce plan, puis passer TASK-006 a `completed`. 