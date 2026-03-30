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