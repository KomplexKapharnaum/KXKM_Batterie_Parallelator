# QA CI/CD par environnement

> Statut: local OK, preuve distante en attente d’activation/présence du workflow sur la branche par défaut.

## Objectif
- Exécuter QA de manière déterministe par environnement (`sim-host`, `kxkm-s3-build`, `kxkm-v3-build`, `kxkm-s3-memory-budget`).
- Capturer des preuves exploitables localement et côté GitHub Actions.

## Exécution locale
```bash
bash scripts/ci/run_qa_all.sh
```

Commande équivalente par environnement:
```bash
bash scripts/ci/run_qa_env.sh sim-host
bash scripts/ci/run_qa_env.sh kxkm-s3-build
bash scripts/ci/run_qa_env.sh kxkm-v3-build
bash scripts/ci/run_qa_env.sh kxkm-s3-memory-budget
```

## Preuve distante GitHub Actions
1. Vérifier les workflows disponibles:
```bash
gh workflow list --repo KomplexKapharnaum/KXKM_Batterie_Parallelator
```
2. Si le workflow `qa-cicd-environments` existe et expose `workflow_dispatch`, le déclencher:
```bash
gh workflow run qa-cicd-environments --ref object-orriented --repo KomplexKapharnaum/KXKM_Batterie_Parallelator
```
3. Exporter une table de preuves:
```bash
bash scripts/ci/collect_remote_qa_evidence.sh qa-cicd-environments object-orriented 10
```

## Blocage connu (2026-03-29)
- Le fichier `.github/workflows/sim-host-tests.yml` n’existe pas encore sur la branche par défaut distante.
- Le workflow `CI` distant ne possède pas de trigger `workflow_dispatch`.
- Prochaine action: pousser/ouvrir PR contenant le workflow `qa-cicd-environments`, puis relancer la collecte de preuves.
## Automatisation de preuve distante (nouveau)

Commande:
```bash
bash scripts/ci/request_remote_qa_proof.sh \
  KomplexKapharnaum/KXKM_Batterie_Parallelator \
  object-orriented \
  qa-cicd-environments \
  docs/QA_REMOTE_PROOF_LATEST.md
```

Résultat actuel: un snapshot horodaté est produit dans `docs/QA_REMOTE_PROOF_LATEST.md` avec inventaire workflows, runs disponibles, diagnostic dispatch, et prochaine action.