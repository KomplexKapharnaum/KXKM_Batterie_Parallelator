# Runbook Deployment KXKM-AI

Statut: operational draft  
Host: `kxkm@kxkm-ai`  
Container cible: `mascarade-platformio`

## 1. Pre-checks

- SSH access must be valid: key-based auth, no interactive prompt.
- Target container must exist and be running.
- Dataset file must be available on host: `consolidated.parquet`.

## 2. Remote checks

```bash
scripts/ml/remote_kxkm_ai_pipeline.sh check
scripts/ml/remote_kxkm_ai_pipeline.sh discover-dataset
```

Expected:
- container visible in `docker ps`
- dataset either found on host or reported missing in container path

## 3. Bootstrap repo in container

```bash
scripts/ml/remote_kxkm_ai_pipeline.sh bootstrap-container
```

This syncs tracked repository files to:
- `/workspace/KXKM_Batterie_Parallelator`

## 4. Inject dataset (if missing)

```bash
scripts/ml/remote_kxkm_ai_pipeline.sh inject-dataset \
  --dataset-path /abs/path/on/host/models/consolidated.parquet
```

## 5. Run pipeline in container

```bash
scripts/ml/remote_kxkm_ai_pipeline.sh run-container
```

Expected artifacts:
- `models/phase2_metrics.json`
- `models/fpnn_soh_v2_quantized.onnx`

## 6. Blockers and recovery

- `CONTAINER_DATASET_MISSING`: run inject step with valid host path.
- Python dependency missing in container: install dependency in container, then rerun bootstrap + run.
- Any runtime error: keep logs and update `plan/refactor-safety-core-web-remote-1.md` with blocker and next action.

## 7. Evidence to store

- command used
- date/time
- resulting artifact paths
- CI link or remote session log reference