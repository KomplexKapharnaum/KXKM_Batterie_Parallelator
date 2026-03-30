---
description: "Use when editing ML battery health scripts, feature extraction, training, quantization, or SOH/RUL evaluation artifacts in scripts/ml, docs/ML_BATTERY_HEALTH_SPEC.md, and models."
name: "ML Pipeline Governance"
applyTo: ["scripts/ml/**/*.py", "docs/ML_BATTERY_HEALTH_SPEC.md", "models/**"]
---

# ML Pipeline Governance

- Keep ML advisory: model outputs must assist operators, never replace hard safety guards in firmware.
- Prefer compact edge models with predictable runtime and memory (INT8 quantization for MCU deployment paths).
- Preserve reproducibility: version datasets, features, training config, and model artifact names.
- Track both in-domain and out-of-domain metrics; do not report a single global score without split context.
- Include drift checks and calibration/confidence reporting when proposing SOH/RUL claims.
- Clearly separate high-confidence results (validated on project data) from exploratory results (literature-only).
- Avoid embedding secrets or credentials in scripts/config.
- When updating docs/ML_BATTERY_HEALTH_SPEC.md, link existing repo docs rather than duplicating architecture details.

See: docs/ML_BATTERY_HEALTH_SPEC.md, AGENTS.md, CLAUDE.md.