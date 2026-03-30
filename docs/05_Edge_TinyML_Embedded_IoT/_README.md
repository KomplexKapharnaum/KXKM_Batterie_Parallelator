# TinyML & Edge AI Literature Library

**Project:** KXKM Batterie Parallelator — Battery Health Prediction  
**Date Created:** 2026-03-30  
**Total Papers:** 12 (2020–2026)

---

## Overview

This folder contains peer-reviewed research papers and technical reports relevant to:
- **Edge inference optimization** for battery SOH/RUL prediction on MCUs
- **Quantization strategies** for neural network deployment
- **SOTA techniques** for TinyML and embedded AI systems

All papers have been classified by priority based on relevance to KXKM project goals (FPNN quantization on ESP32 + cloud SambaMixer).

---

## Organization by Priority

### 01-HIGH-PRIORITY

**Papers to read first for FPNN/quantization understanding:**
- `MCUNet_ImageNet_on_MCU_MIT_2020.pdf` — Memory-efficient architecture design
- `Lightweight_DL_Survey_TinyML_2024-04.pdf` — Quantization taxonomy & benchmarks

**Why:** Direct applicability to edge FPNN deployment. Validates current quantization approach (stratified calibration + per-channel + QDQ format).

### 02-MEDIUM-PRIORITY

**Papers for SOTA 2026 exploration (Q2–Q3 roadmap):**
- `Fine-Tuning_Small_Language_Models_*_2025-03.pdf` — SLM domain adaptation
- `Shakti_SLMs_Edge_AI_2025-03.pdf` — SLM benchmarks on edge
- `EmbedAgent_LLM_Benchmark_Embedded_Systems_2025-06.pdf` — LLM inference metrics
- `TinyAgent_Function_Calling_Edge_2024-09.pdf` and `_Lutfi_Eren_Erdogan_*.pdf` — Local task execution
- `TinyNav_*_2026-03.pdf` (x2) — Autonomous inference patterns

**Why:** Potential expansion of KXKM health prediction to multi-modal (advisory LLM, local decision-making). Not critical for baseline, but inform longer roadmap.

### 03-LOW-PRIORITY

**Context & future-use papers:**
- `Securing_LLM_Embedded_Firmware_Validation_2025-09.pdf` — Security considerations (if future LLM adoption)
- `Tiny_Transformers_Audio_Edge_MCU_2021-03.pdf` — Attention mechanisms (techniques transfer)
- `WakeMod_UltraLowPower_IoT_WakeUp_Radio_2025-05.pdf` — Ultra-low-power circuits (future)

**Why:** Useful for understanding landscape; lower immediate applicability.

---

## How to Use

1. **For immediate work:** Start with HIGH-PRIORITY papers (3–4 hours reading + synthesis)
2. **For planning:** Review SOTA roadmap table in [../ml-battery-health-spec.md](../ml-battery-health-spec.md#5-literature-review--sota-baseline) section 5
3. **For deep dives:** See `_priority-matrix.json` for detailed scoring and reading time estimates

---

## Related Documentation

- **ML Spec:** [../ml-battery-health-spec.md](../ml-battery-health-spec.md) — Integration spec with Literature Review section
- **Phase 1 Findings:** Strategy Notes (internal) — Technical synthesis of MCUNet + TinyML Survey
- **PDF Analysis:** [../pdf-library-analysis.md](../pdf-library-analysis.md) — Detailed classification and relevance matrix

---

## Metadata

- **Total papers:** 12
- **Date span:** 2020–2026
- **Domains:** TinyML, quantization, edge AI, embedded inference, battery health
- **Priority breakdown:** 2 HIGH, 7 MEDIUM, 3 LOW

Last updated: 2026-03-30