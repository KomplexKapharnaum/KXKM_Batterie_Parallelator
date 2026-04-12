# Phase 15 — Audit normalisation `bmu_soh` FEAT_MEANS / FEAT_STDS

**Date** : 2026-04-12
**Scope** : cross-check host-side des constantes de normalisation hardcodées dans
`firmware-idf-v2/components/bmu_soh/src/bmu_soh.cpp` vs les datasets d'entraînement
réels et le checkpoint PyTorch source.

## Résumé exécutif

⚠️ **Les 13 `FEAT_MEANS` / `FEAT_STDS` hardcodés dans `bmu_soh.cpp` ne correspondent
à AUCUN checkpoint / dataset identifiable du workspace.** 10/13 features ont une
déviation > 10% vs `fpnn_soh.pt` (le seul checkpoint disponible).

Cela n'empêche PAS Phase 15 de compiler ou de dry-run (le modèle TFLite micro charge,
invoke tourne en 1244 µs), MAIS cela rend les inférences SoH **numériquement incorrectes**
dès qu'il y aura des vraies features à classifier. Impact bloquant pour Phase 16+ si non résolu.

## Mesures

### Valeurs hardcodées dans `bmu_soh.cpp` (commit `45e845a`)

```c
static const float FEAT_MEANS[13] = {
    27.3286f, 0.3091f, 0.0820f, 0.4863f, -0.0699f, 0.0025f,
    0.1654f, 0.2979f, 27.0195f, 27.6376f, 0.9870f, 59.7926f, 0.5576f
};
static const float FEAT_STDS[13] = {
    1.5719f, 0.6584f, 0.8626f, 1.4825f, 0.5787f, 0.6206f,
    6.2368f, 5.2352f, 1.6740f, 1.7339f, 1.8827f, 21.1871f, 1.5358f
};
```

### Vrai checkpoint source `models/fpnn_soh.pt` (torch)

```
feature_cols = ['V_mean','V_std','I_mean','I_std','dV_dt','dI_dt','ah_cons','ah_charge','V_min','V_max','I_max','samples','R_internal']
feature_means = [27.3381, 0.2070, 0.2388, 0.3542, -0.00098, 0.00118, 0.00676, 0.00288, 27.1311, 27.5451, 0.9603, 47.1759, 0.0585]
feature_stds  = [1.7098, 0.7784, 1.1714, 1.4683, 0.0525, 0.0964, 0.0161, 0.0892, 1.8728, 1.8845, 2.0658, 1.5550, 0.0726]
```

### Dataset d'entraînement `models/features.parquet` (15272 rows, pleins samples)

```
V_mean       mean=27.3631  std=3.4829
V_std        mean=0.7461   std=0.3582
I_mean       mean=0.0126   std=0.8723
I_std        mean=0.7887   std=1.2040
dV_dt        mean=-0.0010  std=0.0093
dI_dt        mean=0.0008   std=0.0120
ah_cons      mean=0.0797   std=2.1877
ah_charge    mean=0.0655   std=2.0179
V_min        mean=26.6170  std=3.4080
V_max        mean=28.1092  std=3.5921
I_max        mean=1.2271   std=1.5810
samples      mean=2823.43  std=133.99
R_internal   mean=0.0678   std=0.1814
```

### Dataset alternatif `models/features_with_rul.parquet` (2058 rows)

```
V_mean       mean=26.2655  std=1.0306
samples      mean=47.5496  std=0.8801
I_mean       mean=6.2274   std=1.2621
I_max        mean=8.6143   std=1.9779
```

## Tableau de comparaison

| Feature | ckpt_mean | bmu_mean | Δ | ckpt_std | bmu_std | Δ |
|---|---:|---:|---:|---:|---:|---:|
| V_mean | 27.3381 | 27.3286 | -0.0095 | 1.7098 | 1.5719 | -0.1379 |
| V_std | 0.2070 | 0.3091 | +0.1021 | 0.7784 | 0.6584 | -0.1200 ⚠ |
| I_mean | 0.2388 | 0.0820 | -0.1568 | 1.1714 | 0.8626 | -0.3088 ⚠ |
| I_std | 0.3542 | 0.4863 | +0.1321 | 1.4683 | 1.4825 | +0.0142 ⚠ |
| dV_dt | -0.0010 | -0.0699 | -0.0689 | 0.0525 | 0.5787 | +0.5262 ⚠ |
| dI_dt | 0.0012 | 0.0025 | +0.0013 | 0.0964 | 0.6206 | +0.5242 ⚠ |
| **ah_cons** | 0.0068 | 0.1654 | **+0.1586** | 0.0161 | **6.2368** | **+6.2207** ⚠ |
| **ah_charge** | 0.0029 | 0.2979 | **+0.2950** | 0.0892 | **5.2352** | **+5.1460** ⚠ |
| V_min | 27.1311 | 27.0195 | -0.1116 | 1.8728 | 1.6740 | -0.1988 ⚠ |
| V_max | 27.5451 | 27.6376 | +0.0925 | 1.8845 | 1.7339 | -0.1506 |
| I_max | 0.9603 | 0.9870 | +0.0267 | 2.0658 | 1.8827 | -0.1831 |
| **samples** | 47.1759 | 59.7926 | **+12.6167** | 1.5550 | **21.1871** | **+19.6321** ⚠ |
| **R_internal** | 0.0585 | 0.5576 | **+0.4991** | 0.0726 | **1.5358** | **+1.4632** ⚠ |

**10/13 features** avec delta > 10%. Cas extrêmes :
- `ah_cons std` : facteur **388×** (0.016 vs 6.24)
- `ah_charge std` : facteur **59×** (0.089 vs 5.24)
- `dV_dt std` : facteur **11×** (0.052 vs 0.579)
- `samples std` : facteur **14×** (1.55 vs 21.19)

## Origine probable des valeurs hardcodées

Hypothèse la plus plausible : les valeurs ont été **fabriquées à la main** ou reprises
d'un modèle tiers (peut-être une version préliminaire ou un mix de plusieurs checkpoints).
Aucune correspondance trouvée avec :
- `features.parquet` (mean_samples=2823)
- `features_adapted.parquet` (idem)
- `features_with_rul.parquet` (mean_samples=47.5, mais V_mean/I_mean/I_max complètement différents)
- `fpnn_soh.pt` (checkpoint v1 available)

## Impact

### Phase 15 (actuel)
- ✅ **Compilation, load, dry-run OK** — le modèle charge et Invoke() tourne sur tenseurs zéros
- ✅ **Pas de crash runtime** — les tenseurs sont dimensionnés correctement (13 × int8 input, 1 × int8 output)
- ⚠️ **Inférence SoH numériquement incorrecte** dès qu'on aura des features non-zéro

### Phase 16+ (bench populé)
- 🔴 **Bloquant** : les SoH pct calculés seront aléatoires, pas corrélés à l'état réel des batteries
- 🔴 **Non détectable** par les tests host actuels — il faudrait un golden-set de (features, expected_soh) captés sur bench réel
- 🟡 **Workaround partiel** : continuer en Phase 15 sans push-back vers `bmu_core_command(UpdateSoh)` (déjà le cas — deferred à Phase 16)

## Actions recommandées

### Immédiat (Phase 15.1 ou fix dans Phase 16)

**Option A — Utiliser les valeurs du `.pt`** : remplacer les 13 constantes dans `bmu_soh.cpp`
par celles extraites de `fpnn_soh.pt`. Risque : si `fpnn_soh_v3_int8.tflite` n'a PAS été produit
depuis ce checkpoint, le modèle sortira des valeurs fausses d'une autre façon.

**Option B — Ré-entraîner v3 et documenter** : lancer `train_fpnn.py` → `convert_to_tflite.py`,
capturer le new checkpoint `.pt` + le `.tflite` dans `models/`, copier les means/stds dans
`bmu_soh.cpp` avec un commentaire pointant vers la date/hash du checkpoint. **Recommandé**.

**Option C — Charger les means/stds au runtime** : créer un second fichier `.bin` packé
avec les 26 floats, flasher en SPIFFS, lire au boot dans `bmu_soh_init()`. Évite le re-flash
firmware quand on change de modèle. Overhead marginal.

### À faire avant la première vraie inférence (Phase 16 bench populé)

1. **Reproduire la chaîne training complète** avec `train_fpnn.py` + `convert_to_tflite.py`
2. **Capturer les vraies means/stds** depuis le checkpoint produit
3. **Écrire un test Python** qui compare l'inférence TFLite (via tensorflow-lite) à l'inférence
   PyTorch f32 sur le même calibration set — différence < 5% MAPE
4. **Capturer un golden-set** de (features_13, expected_soh_pct) sur bench réel
5. **Patcher `bmu_soh.cpp`** avec les nouvelles constantes + commentaire SHA256 du `.tflite`
6. **Ajouter un test d'intégration** qui mocke `bmu_soh_infer_battery` avec des features du
   golden-set et vérifie la sortie int8

## Follow-up tickets

- [ ] `#bmu-soh-norm-1` — Re-entraîner FPNN v3.1 et documenter means/stds
- [ ] `#bmu-soh-norm-2` — Test d'intégration Python torch vs tflite < 5% MAPE
- [ ] `#bmu-soh-norm-3` — Golden set captured on bench post Phase 16
- [ ] `#bmu-soh-norm-4` — Patch `bmu_soh.cpp` avec means/stds corrigés + SHA256 pinned

## Simulation source

Le script de validation est dans `/tmp/bmu_soh_features.c` (300 lignes C) + le Python d'audit
dans cette session (non committé, reproductible depuis les données publiées).

**Reproduire l'audit** :
```bash
/Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/ml_venv/bin/python << 'EOF'
import torch
ckpt = torch.load('models/fpnn_soh.pt', map_location='cpu', weights_only=False)
print('means:', ckpt['feature_means'])
print('stds:',  ckpt['feature_stds'])
EOF
```
