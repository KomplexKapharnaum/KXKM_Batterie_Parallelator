# scripts/ml

ML pipeline scripts for BMU advisory intelligence (SOH/RUL).

Safety rule:
- Outputs are advisory only.
- Firmware protections remain authoritative.

## Main workflow

1. Feature extraction
```bash
python3 scripts/ml/extract_features.py --input models/consolidated.parquet --output models/features_v2.parquet --window 60
```

2. Feature adaptation
```bash
python3 scripts/ml/adapt_features.py --input models/features_v2.parquet --output models/features_adapted_v2.parquet
```

3. SOH model training
```bash
python3 scripts/ml/train_fpnn.py --input models/features_adapted_v2.parquet --output-dir models --epochs 50 --hidden 64 --degree 2 --soh-mode capacity --test-device k-led1 --val-ratio 0.1
```

4. Quantization
```bash
python3 scripts/ml/quantize_tflite.py --model models/fpnn_soh.pt --features models/features_adapted_v2.parquet --output models/fpnn_soh_v2_quantized.onnx --backend onnxrt
```

5. Final metrics
```bash
python3 scripts/ml/finalize_phase2_metrics.py --features models/features_adapted_v2.parquet --quantized models/fpnn_soh_v2_quantized.onnx --rul-model models/rul_sambamixer.pt --train-log phase2_fpnn_train_v2.log --output models/phase2_metrics.json
```

## Remote execution

Use orchestrator:
```bash
scripts/ml/remote_kxkm_ai_pipeline.sh check
scripts/ml/remote_kxkm_ai_pipeline.sh bootstrap-container
scripts/ml/remote_kxkm_ai_pipeline.sh discover-dataset
scripts/ml/remote_kxkm_ai_pipeline.sh run-container
```

## Artifacts

- models/features_v2.parquet
- models/features_adapted_v2.parquet
- models/fpnn_soh_v2_quantized.onnx
- models/phase2_metrics.json
## Quantization iteration

Advanced quantization iteration:

```bash
python3 scripts/ml/quantize_tflite.py \
  --model models/fpnn_soh.pt \
  --features models/features_adapted_v2.parquet \
  --output models/fpnn_soh_v2_quantized.onnx \
  --backend onnxrt \
  --quant-format qdq \
  --calib-strategy stratified \
  --calib-samples 500 \
  --percentile-clip 1 99
```

Bootstrap verification now checks `pandas`, `numpy`, `torch`, `onnxruntime`, `pyarrow`, and `onnx`.

Latest verified remote status on `kxkm-ai`:
- Technical unblock: done.
- Remaining blocker: quantized quality gate on the latest remote dataset split.
## Promoted baseline on kxkm-ai

Winning configuration promoted on `2026-03-30 16:17 UTC`:
- `--quant-format qdq`
- `--calib-strategy stratified`
- `--calib-samples 500`
- `--percentile-clip 1 99`

Promoted remote metrics:
- float32 MAPE: `7.7289%`
- quantized MAPE: `10.7734%`
- degradation: `+3.0446 pp`
- quantized size: `15.99 KB`
- `overall_gate_pass=true`