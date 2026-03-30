#!/usr/bin/env python3
"""Generate _priority-matrix.json for TinyML literature library."""
import json
import sys

priority_matrix = {
  "metadata": {
    "project": "KXKM Batterie Parallelator",
    "date_created": "2026-03-30",
    "total_papers": 12,
    "scoring_version": "v1.0"
  },
  "scoring_criteria": {
    "relevance_to_fpnn_quantization": {
      "weight": 0.40,
      "description": "How directly applicable to FPNN edge inference on ESP32"
    },
    "sota_2026_importance": {
      "weight": 0.30,
      "description": "Relevance to roadmap expansion (LLM advisory, multi-modal)"
    },
    "read_priority": {
      "weight": 0.20,
      "description": "Urgency for team to review (baseline vs. future)"
    },
    "practical_techniques": {
      "weight": 0.10,
      "description": "Number of actionable code/config changes"
    }
  },
  "papers": [
    {"id": "HIGH-1", "filename": "01-HIGH-PRIORITY/MCUNet_ImageNet_on_MCU_MIT_2020.pdf", "title": "MCUNet: Memory-Efficient Convolutional Neural Networks for Embedded Vision", "year": 2020, "priority": "HIGH", "score": 0.94, "applicability_to_kxkm": "Direct — validates current quantization + memory strategy", "status": "Read (Phase 1)"},
    {"id": "HIGH-2", "filename": "01-HIGH-PRIORITY/Lightweight_DL_Survey_TinyML_2024-04.pdf", "title": "Lightweight Deep Learning: A Survey on Efficient Deep Neural Networks", "year": 2024, "priority": "HIGH", "score": 0.92, "applicability_to_kxkm": "Direct — all 5 techniques mapped to Phase 2 baseline", "status": "Read (Phase 1)"},
    {"id": "MEDIUM-1", "filename": "02-MEDIUM-PRIORITY/Fine-Tuning_Small_Language_Models_for_Domain-Specific_AI_An_Edge_AI_Perspective_Rakshit_2025-03.pdf", "title": "Fine-Tuning Small Language Models for Domain-Specific AI", "year": 2025, "priority": "MEDIUM", "score": 0.68, "applicability_to_kxkm": "Future — informs hybrid FPNN + LLM advisory architecture", "status": "Queued for Phase 3"},
    {"id": "MEDIUM-2", "filename": "02-MEDIUM-PRIORITY/Shakti_SLMs_Edge_AI_2025-03.pdf", "title": "Shakti: Small Language Models on Edge Devices", "year": 2025, "priority": "MEDIUM", "score": 0.65, "applicability_to_kxkm": "Future — validates SLM feasibility on hardware", "status": "Queued for Phase 3"},
    {"id": "MEDIUM-3", "filename": "02-MEDIUM-PRIORITY/TinyAgent_Function_Calling_Edge_2024-09.pdf", "title": "TinyAgent: Function Calling on Edge Devices", "year": 2024, "priority": "MEDIUM", "score": 0.62, "applicability_to_kxkm": "Future — enables battery advisor on edge", "status": "Queued for Phase 3"},
    {"id": "MEDIUM-4", "filename": "02-MEDIUM-PRIORITY/TinyAgent_Function_Calling_at_the_Edge_Lutfi_Eren_Erdogan_1_Nicholas_Lee_2024-09.pdf", "title": "TinyAgent: Function Calling at the Edge (Extended)", "year": 2024, "priority": "MEDIUM", "score": 0.60, "applicability_to_kxkm": "Future — design phase Q2 2026", "status": "Queued for Phase 3"},
    {"id": "MEDIUM-5", "filename": "02-MEDIUM-PRIORITY/TinyNav_ESP32_Navigation_2026-03.pdf", "title": "TinyNav: Real-Time Autonomous Navigation on ESP32", "year": 2026, "priority": "MEDIUM", "score": 0.70, "applicability_to_kxkm": "Immediate reference — FPNN could use pipeline patterns", "status": "Queued for Phase 3"},
    {"id": "MEDIUM-6", "filename": "02-MEDIUM-PRIORITY/TinyNav_End-to-End_TinyML_for_Real-Time_Autonomous_Navigation_on_Microcontrollers_2026-03.pdf", "title": "TinyNav: End-to-End TinyML for Real-Time Autonomous Navigation", "year": 2026, "priority": "MEDIUM", "score": 0.72, "applicability_to_kxkm": "Immediate reference — systems design for Phase 2", "status": "Queued for Phase 3"},
    {"id": "MEDIUM-7", "filename": "02-MEDIUM-PRIORITY/EmbedAgent_LLM_Benchmark_Embedded_Systems_2025-06.pdf", "title": "EmbedAgent: LLM Benchmarking on Embedded Systems", "year": 2025, "priority": "MEDIUM", "score": 0.64, "applicability_to_kxkm": "Future — validation framework for Phase 3", "status": "Queued for Phase 3"},
    {"id": "LOW-1", "filename": "03-LOW-PRIORITY/Securing_LLM_Embedded_Firmware_Validation_2025-09.pdf", "title": "Securing LLM Embeddings: Firmware Validation Techniques", "year": 2025, "priority": "LOW", "score": 0.45, "applicability_to_kxkm": "Future (Q3 2026+) — consider only if LLM chosen", "status": "Low priority archive"},
    {"id": "LOW-2", "filename": "03-LOW-PRIORITY/Tiny_Transformers_Audio_Edge_MCU_2021-03.pdf", "title": "Tiny Transformers: Audio Processing on Edge MCUs", "year": 2021, "priority": "LOW", "score": 0.40, "applicability_to_kxkm": "Theoretical interest — attention techniques transfer", "status": "Low priority archive"},
    {"id": "LOW-3", "filename": "03-LOW-PRIORITY/WakeMod_UltraLowPower_IoT_WakeUp_Radio_2025-05.pdf", "title": "WakeMod: Ultra-Low-Power Wake-Up Radio for IoT", "year": 2025, "priority": "LOW", "score": 0.38, "applicability_to_kxkm": "Future (Phase 4, 2027+) — power optimization if critical", "status": "Low priority archive"}
  ]
}

with open('docs/05_Edge_TinyML_Embedded_IoT/_priority-matrix.json', 'w') as f:
    json.dump(priority_matrix, f, indent=2)
print("✅ _priority-matrix.json created successfully")
