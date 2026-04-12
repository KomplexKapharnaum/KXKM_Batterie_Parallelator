# bmu_soh model — fpnn_soh_v3_int8.tflite

Modèle FPNN INT8 (~12 KB) pour prédiction SOH par batterie. Embarqué dans le binaire ESP32 via `EMBED_FILES` de `idf_component_register` (CMakeLists.txt parent).

## Origine

Source : `models/fpnn_soh_v3_int8.tflite` (racine du repo). Entraîné via `scripts/ml/train_fpnn.py` puis quantisé INT8 via `scripts/ml/convert_to_tflite.py`.

## SHA256

À régénérer par :
```bash
shasum -a 256 fpnn_soh_v3_int8.tflite
```

(Le SHA256 figé sera ajouté Phase 16+ avec un hook pre-commit qui assert l'intégrité du blob embarqué.)

## Architecture

13 features → polynomial expansion (degré 2) → FC(104, 64) → ReLU → FC(64, 1) → Sigmoid

Sortie : SOH ∈ [0, 1] dé-quantisé puis converti en pourcentage 0..100 avant push vers le core Rust via `BmuCommandC UpdateSoh`.

## Symboles ELF

L'EMBED_FILES expose deux symboles globaux dans le binaire :
- `_binary_fpnn_soh_v3_int8_tflite_start`
- `_binary_fpnn_soh_v3_int8_tflite_end`

Référencés dans `src/bmu_soh.cpp` via `extern "C"`.
