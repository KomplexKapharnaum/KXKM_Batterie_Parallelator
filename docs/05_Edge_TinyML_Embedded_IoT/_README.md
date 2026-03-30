# Bibliothèque Edge / TinyML / Embedded IoT

Organisation par priorité de lecture pour le projet KXKM Batterie Parallelator (BMU ESP32-S3).

## Classification par priorité

### 01-HIGH-PRIORITY — Pertinence directe FPNN / ESP32

| # | Fichier | Thème |
|---|---------|-------|
| 1 | `MCUNet_ImageNet_on_MCU_MIT_2020.pdf` | Architecture réseau neuronal optimisée MCU (MIT) |
| 2 | `Lightweight_DL_Survey_TinyML_2024-04.pdf` | Survey complet Deep Learning léger pour TinyML |

**Ordre de lecture recommandé :** MCUNet d'abord (fondations), puis le survey 2024 (état de l'art).

### 02-MEDIUM-PRIORITY — Exploration SOTA 2025-2026

| # | Fichier | Thème |
|---|---------|-------|
| 3 | `Fine-Tuning_Small_Language_Models_for_Domain-Specific_AI_An_Edge_AI_Perspective_Rakshit_2025-03.pdf` | Fine-tuning SLM pour IA embarquée |
| 4 | `Shakti_SLMs_Edge_AI_2025-03.pdf` | Small Language Models pour Edge AI |
| 5 | `EmbedAgent_LLM_Benchmark_Embedded_Systems_2025-06.pdf` | Benchmark LLM sur systèmes embarqués |
| 6 | `TinyAgent_Function_Calling_Edge_2024-09.pdf` | Function calling sur edge (version courte) |
| 7 | `TinyAgent_Function_Calling_at_the_Edge_Lutfi_Eren_Erdogan_1_Nicholas_Lee_2024-09.pdf` | Function calling sur edge (version complète) |
| 8 | `TinyNav_ESP32_Navigation_2026-03.pdf` | Navigation autonome sur ESP32 (version courte) |
| 9 | `TinyNav_End-to-End_TinyML_for_Real-Time_Autonomous_Navigation_on_Microcontrollers_2026-03.pdf` | Navigation autonome TinyML (version complète) |

**Ordre de lecture recommandé :** SLMs (3-4) → Agents (5-7) → Navigation ESP32 (8-9).

### 03-LOW-PRIORITY — Contexte / Futur

| # | Fichier | Thème |
|---|---------|-------|
| 10 | `Securing_LLM_Embedded_Firmware_Validation_2025-09.pdf` | Sécurité LLM pour validation firmware |
| 11 | `Tiny_Transformers_Audio_Edge_MCU_2021-03.pdf` | Transformers audio sur MCU |
| 12 | `WakeMod_UltraLowPower_IoT_WakeUp_Radio_2025-05.pdf` | Wake-up radio ultra-basse consommation |

**Ordre de lecture recommandé :** Sécurité (10) si pertinent pour BMU, puis audio (11) et radio (12) si exploration future.

## Conventions

- Les fichiers sont nommés : `Titre_Court_AAAA-MM.pdf`
- Statut de lecture dans `_priority-matrix.json`
- Mise à jour : 2026-03-30
