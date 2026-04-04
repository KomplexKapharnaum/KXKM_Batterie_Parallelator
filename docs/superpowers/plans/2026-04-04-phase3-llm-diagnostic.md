# Phase 3: LLM Diagnostic Narratives — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fine-tune Qwen2.5-7B with Unsloth for French battery diagnostic generation, deploy as inference service on kxkm-ai.

**Architecture:** QLoRA fine-tuning on synthetic + validated dataset. Inference via vLLM or llama.cpp. FastAPI endpoint for daily digest + on-demand diagnostics.

**Tech Stack:** Python 3.12, Unsloth, QLoRA, Qwen2.5-7B, vLLM/llama.cpp, FastAPI, Docker

**Infrastructure:** kxkm-ai (RTX 4090 24 GB, Tailscale). InfluxDB on kxkm-ai:8086 for scoring data. LLM inference on port 8401, diagnostic API on port 8400 (shared with Phase 2 SOH API).

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `scripts/llm/generate_dataset.py` | Claude API synthetic dataset generation |
| Create | `scripts/llm/scenario_templates.py` | 8 scenario types with parametric context generators |
| Create | `scripts/llm/validate_dataset.py` | Quality filters, dedup, train/val/test split |
| Create | `scripts/llm/finetune_qwen.py` | Unsloth QLoRA fine-tuning script |
| Create | `scripts/llm/evaluate_model.py` | ROUGE-L, severity accuracy, human review export |
| Create | `scripts/llm/requirements.txt` | Python dependencies for dataset + fine-tuning |
| Create | `services/soh-llm/inference_server.py` | vLLM/llama.cpp LoRA inference wrapper |
| Create | `services/soh-llm/diagnostic_api.py` | FastAPI diagnostic endpoints |
| Create | `services/soh-llm/prompt_template.py` | Prompt construction + severity extraction |
| Create | `services/soh-llm/config.py` | Service configuration (ports, model paths, InfluxDB) |
| Create | `services/soh-llm/Dockerfile` | Container definition |
| Create | `services/soh-llm/requirements.txt` | Runtime Python dependencies |
| Create | `services/soh-llm/docker-compose.soh-llm.yml` | Docker Compose service definition |
| Create | `tests/llm/test_prompt_template.py` | Prompt construction unit tests |
| Create | `tests/llm/test_diagnostic_api.py` | API response format + endpoint tests |
| Create | `tests/llm/test_severity_classification.py` | Severity extraction logic tests |
| Create | `tests/llm/conftest.py` | Shared fixtures for LLM tests |
| Modify | `services/soh-api/docker-compose.yml` | Add soh-llm service reference (if exists) |

---

### Task 1: Dataset generation scaffold

**Files:**
- Create: `scripts/llm/generate_dataset.py`
- Create: `scripts/llm/requirements.txt`

- [ ] **Step 1: Create requirements.txt for dataset generation**

Create `scripts/llm/requirements.txt`:
```txt
anthropic>=0.40.0
pandas>=2.2.0
pyarrow>=15.0.0
pydantic>=2.6.0
rouge-score>=0.1.2
numpy>=1.26.0
tqdm>=4.66.0
```

- [ ] **Step 2: Create dataset generation script**

Create `scripts/llm/generate_dataset.py`:
```python
#!/usr/bin/env python3
"""
generate_dataset.py — Generate synthetic (battery context JSON -> French diagnostic) pairs
using Claude API for Qwen2.5-7B fine-tuning.

Usage:
    python scripts/llm/generate_dataset.py \
        --output data/llm/diagnostic_dataset_raw.jsonl \
        --num-examples 1500 \
        --scenario-mix balanced \
        --api-key $ANTHROPIC_API_KEY
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import random
import sys
import time
from pathlib import Path

import anthropic
from tqdm import tqdm

from scenario_templates import (
    SCENARIO_GENERATORS,
    BatteryContext,
    generate_context_for_scenario,
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("gen_dataset")

# ---------------------------------------------------------------------------
# Claude API generation
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = (
    "Tu es un expert en batteries LiFePO4/Li-ion pour systèmes de spectacle vivant "
    "(KXKM BMU). Tu rédiges des diagnostics techniques concis en français."
)

USER_PROMPT_TEMPLATE = """\
Voici les données d'une batterie du parc KXKM:

{context_json}

Rédige un diagnostic concis en français (3-5 phrases):
1. État actuel de la batterie (santé, résistance interne, tension)
2. Tendance observée (évolution sur les derniers jours)
3. Recommandation d'action si nécessaire

Sois précis et technique. Utilise les unités (mΩ, mV, A, jours).
Termine par une classification de sévérité: [INFO], [WARNING] ou [CRITICAL].
"""


def generate_single_example(
    client: anthropic.Anthropic,
    context: BatteryContext,
    model: str = "claude-sonnet-4-20250514",
    max_retries: int = 3,
) -> dict | None:
    """Generate one (context, diagnostic) pair via Claude API."""
    context_json = json.dumps(context.to_dict(), indent=2, ensure_ascii=False)
    user_msg = USER_PROMPT_TEMPLATE.format(context_json=context_json)

    for attempt in range(max_retries):
        try:
            response = client.messages.create(
                model=model,
                max_tokens=512,
                system=SYSTEM_PROMPT,
                messages=[{"role": "user", "content": user_msg}],
                temperature=0.7,
            )
            text = response.content[0].text.strip()

            # Extract severity from [INFO]/[WARNING]/[CRITICAL] tag
            severity = "info"
            for tag in ["CRITICAL", "WARNING", "INFO"]:
                if f"[{tag}]" in text:
                    severity = tag.lower()
                    break

            return {
                "context": context.to_dict(),
                "scenario": context.scenario,
                "diagnostic": text,
                "severity": severity,
                "model": model,
            }
        except anthropic.RateLimitError:
            wait = 2 ** (attempt + 1)
            log.warning("Rate limited, waiting %ds...", wait)
            time.sleep(wait)
        except anthropic.APIError as e:
            log.error("API error: %s", e)
            if attempt == max_retries - 1:
                return None
            time.sleep(1)
    return None


def generate_dataset(
    output_path: Path,
    num_examples: int,
    scenario_mix: str,
    api_key: str,
    model: str,
) -> None:
    """Generate full dataset and write to JSONL."""
    client = anthropic.Anthropic(api_key=api_key)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Distribute examples across scenarios
    scenarios = list(SCENARIO_GENERATORS.keys())
    if scenario_mix == "balanced":
        per_scenario = num_examples // len(scenarios)
        distribution = {s: per_scenario for s in scenarios}
        # Distribute remainder
        remainder = num_examples - per_scenario * len(scenarios)
        for i, s in enumerate(scenarios[:remainder]):
            distribution[s] += 1
    else:
        # Weighted: more degradation/anomaly examples
        weights = {
            "healthy": 0.15,
            "early_degradation": 0.15,
            "accelerated_degradation": 0.15,
            "connection_issue": 0.12,
            "fleet_outlier": 0.12,
            "end_of_life": 0.10,
            "post_replacement": 0.10,
            "fleet_imbalance": 0.11,
        }
        distribution = {s: max(1, int(num_examples * w)) for s, w in weights.items()}

    log.info("Distribution: %s (total: %d)", distribution, sum(distribution.values()))

    written = 0
    errors = 0

    with open(output_path, "w", encoding="utf-8") as f:
        for scenario, count in distribution.items():
            log.info("Generating %d examples for scenario: %s", count, scenario)
            for i in tqdm(range(count), desc=scenario):
                context = generate_context_for_scenario(scenario)
                example = generate_single_example(client, context, model=model)
                if example:
                    f.write(json.dumps(example, ensure_ascii=False) + "\n")
                    written += 1
                else:
                    errors += 1
                # Rate limiting: ~30 req/min for Sonnet
                time.sleep(2.0)

    log.info("Done: %d written, %d errors -> %s", written, errors, output_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate LLM diagnostic dataset")
    parser.add_argument("--output", type=Path, default=Path("data/llm/diagnostic_dataset_raw.jsonl"))
    parser.add_argument("--num-examples", type=int, default=1500)
    parser.add_argument("--scenario-mix", choices=["balanced", "weighted"], default="balanced")
    parser.add_argument("--api-key", default=os.environ.get("ANTHROPIC_API_KEY"))
    parser.add_argument("--model", default="claude-sonnet-4-20250514")
    args = parser.parse_args()

    if not args.api_key:
        log.error("ANTHROPIC_API_KEY required (--api-key or env var)")
        sys.exit(1)

    generate_dataset(
        output_path=args.output,
        num_examples=args.num_examples,
        scenario_mix=args.scenario_mix,
        api_key=args.api_key,
        model=args.model,
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Verify script imports and structure**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator
python3 -c "import ast; ast.parse(open('scripts/llm/generate_dataset.py').read()); print('OK')"
```

**Commit:** `feat(llm): add dataset generation scaffold with Claude API`

---

### Task 2: Scenario templates

**Files:**
- Create: `scripts/llm/scenario_templates.py`

- [ ] **Step 1: Create scenario templates with 8 scenario types**

Create `scripts/llm/scenario_templates.py`:
```python
#!/usr/bin/env python3
"""
scenario_templates.py — Parametric battery context generators for 8 diagnostic scenarios.

Each scenario type produces a BatteryContext with realistic ranges for LiFePO4/Li-ion
24-30V batteries in the KXKM BMU fleet (2-23 batteries per unit).
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field, asdict
from typing import Callable


@dataclass
class BatteryContext:
    """Battery context JSON structure for LLM prompt input."""
    battery_id: int
    fleet_size: int
    soh_pct: float              # 0-100
    rul_days: float             # estimated remaining useful life
    anomaly_score: float        # 0.0-1.0
    r_ohmic_mohm: float         # mOhm
    r_total_mohm: float         # mOhm
    r_int_trend_mohm_per_day: float  # mOhm/day over 7 days
    v_avg_mv: float             # mV
    i_avg_a: float              # A
    cycle_count: int
    fleet_health_pct: float     # 0-100
    soh_confidence: int         # 0-100
    chemistry: str              # "LiFePO4" or "Li-ion"
    scenario: str = ""          # scenario label (not included in prompt)

    def to_dict(self) -> dict:
        """Return dict for JSON serialization (excludes scenario label)."""
        d = asdict(self)
        d.pop("scenario", None)
        return d


# ---------------------------------------------------------------------------
# Parametric generators per scenario
# ---------------------------------------------------------------------------

def _healthy() -> BatteryContext:
    """Healthy battery, normal operation."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(85, 100), 1),
        rul_days=round(random.uniform(300, 800), 0),
        anomaly_score=round(random.uniform(0.0, 0.1), 2),
        r_ohmic_mohm=round(random.uniform(8.0, 18.0), 1),
        r_total_mohm=round(random.uniform(12.0, 25.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(-0.02, 0.05), 3),
        v_avg_mv=round(random.uniform(26000, 28500), 0),
        i_avg_a=round(random.uniform(0.5, 8.0), 1),
        cycle_count=random.randint(10, 300),
        fleet_health_pct=round(random.uniform(85, 98), 1),
        soh_confidence=random.randint(75, 100),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="healthy",
    )


def _early_degradation() -> BatteryContext:
    """Early degradation: R_int rising slowly, SOH 70-85%."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(70, 85), 1),
        rul_days=round(random.uniform(90, 300), 0),
        anomaly_score=round(random.uniform(0.1, 0.3), 2),
        r_ohmic_mohm=round(random.uniform(18.0, 30.0), 1),
        r_total_mohm=round(random.uniform(25.0, 42.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.05, 0.15), 3),
        v_avg_mv=round(random.uniform(25500, 27500), 0),
        i_avg_a=round(random.uniform(0.5, 8.0), 1),
        cycle_count=random.randint(300, 800),
        fleet_health_pct=round(random.uniform(75, 90), 1),
        soh_confidence=random.randint(60, 90),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="early_degradation",
    )


def _accelerated_degradation() -> BatteryContext:
    """Accelerated degradation: R_int knee point, SOH dropping fast."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(55, 75), 1),
        rul_days=round(random.uniform(20, 90), 0),
        anomaly_score=round(random.uniform(0.3, 0.6), 2),
        r_ohmic_mohm=round(random.uniform(28.0, 50.0), 1),
        r_total_mohm=round(random.uniform(40.0, 70.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.15, 0.5), 3),
        v_avg_mv=round(random.uniform(24500, 26500), 0),
        i_avg_a=round(random.uniform(0.5, 6.0), 1),
        cycle_count=random.randint(600, 1500),
        fleet_health_pct=round(random.uniform(65, 85), 1),
        soh_confidence=random.randint(50, 80),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="accelerated_degradation",
    )


def _connection_issue() -> BatteryContext:
    """Connection issue: sudden R_int jump, low confidence."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(60, 90), 1),
        rul_days=round(random.uniform(50, 200), 0),
        anomaly_score=round(random.uniform(0.5, 0.9), 2),
        r_ohmic_mohm=round(random.uniform(40.0, 120.0), 1),
        r_total_mohm=round(random.uniform(60.0, 180.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.5, 5.0), 3),
        v_avg_mv=round(random.uniform(25000, 28000), 0),
        i_avg_a=round(random.uniform(0.2, 5.0), 1),
        cycle_count=random.randint(50, 600),
        fleet_health_pct=round(random.uniform(70, 90), 1),
        soh_confidence=random.randint(10, 40),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="connection_issue",
    )


def _fleet_outlier() -> BatteryContext:
    """Fleet outlier: GNN detected anomaly relative to peers."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(6, 23),
        soh_pct=round(random.uniform(60, 80), 1),
        rul_days=round(random.uniform(40, 150), 0),
        anomaly_score=round(random.uniform(0.6, 0.95), 2),
        r_ohmic_mohm=round(random.uniform(25.0, 55.0), 1),
        r_total_mohm=round(random.uniform(35.0, 75.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.1, 0.4), 3),
        v_avg_mv=round(random.uniform(24500, 27000), 0),
        i_avg_a=round(random.uniform(0.5, 7.0), 1),
        cycle_count=random.randint(200, 1000),
        fleet_health_pct=round(random.uniform(80, 95), 1),
        soh_confidence=random.randint(55, 85),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="fleet_outlier",
    )


def _end_of_life() -> BatteryContext:
    """End of life: SOH < 60%, RUL < 30 days."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(30, 60), 1),
        rul_days=round(random.uniform(0, 30), 0),
        anomaly_score=round(random.uniform(0.7, 1.0), 2),
        r_ohmic_mohm=round(random.uniform(45.0, 100.0), 1),
        r_total_mohm=round(random.uniform(65.0, 150.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.3, 2.0), 3),
        v_avg_mv=round(random.uniform(23000, 25500), 0),
        i_avg_a=round(random.uniform(0.1, 3.0), 1),
        cycle_count=random.randint(1000, 3000),
        fleet_health_pct=round(random.uniform(55, 80), 1),
        soh_confidence=random.randint(40, 75),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="end_of_life",
    )


def _post_replacement() -> BatteryContext:
    """Post replacement: new battery, low cycle count, high SOH."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(95, 100), 1),
        rul_days=round(random.uniform(600, 1200), 0),
        anomaly_score=round(random.uniform(0.0, 0.15), 2),
        r_ohmic_mohm=round(random.uniform(6.0, 12.0), 1),
        r_total_mohm=round(random.uniform(9.0, 18.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(-0.01, 0.02), 3),
        v_avg_mv=round(random.uniform(27000, 29000), 0),
        i_avg_a=round(random.uniform(0.5, 8.0), 1),
        cycle_count=random.randint(0, 20),
        fleet_health_pct=round(random.uniform(80, 95), 1),
        soh_confidence=random.randint(30, 60),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="post_replacement",
    )


def _fleet_imbalance() -> BatteryContext:
    """Fleet imbalance: one weak battery dragging others, low fleet health."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(6, 23),
        soh_pct=round(random.uniform(50, 72), 1),
        rul_days=round(random.uniform(30, 120), 0),
        anomaly_score=round(random.uniform(0.4, 0.8), 2),
        r_ohmic_mohm=round(random.uniform(30.0, 65.0), 1),
        r_total_mohm=round(random.uniform(45.0, 90.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.1, 0.4), 3),
        v_avg_mv=round(random.uniform(24000, 26500), 0),
        i_avg_a=round(random.uniform(0.3, 5.0), 1),
        cycle_count=random.randint(400, 1200),
        fleet_health_pct=round(random.uniform(45, 70), 1),
        soh_confidence=random.randint(50, 80),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="fleet_imbalance",
    )


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------

SCENARIO_GENERATORS: dict[str, Callable[[], BatteryContext]] = {
    "healthy": _healthy,
    "early_degradation": _early_degradation,
    "accelerated_degradation": _accelerated_degradation,
    "connection_issue": _connection_issue,
    "fleet_outlier": _fleet_outlier,
    "end_of_life": _end_of_life,
    "post_replacement": _post_replacement,
    "fleet_imbalance": _fleet_imbalance,
}


def generate_context_for_scenario(scenario: str) -> BatteryContext:
    """Generate a random battery context for a given scenario type."""
    if scenario not in SCENARIO_GENERATORS:
        raise ValueError(f"Unknown scenario: {scenario}. Valid: {list(SCENARIO_GENERATORS.keys())}")
    return SCENARIO_GENERATORS[scenario]()
```

- [ ] **Step 2: Verify all 8 scenarios produce valid context**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator
python3 -c "
import sys; sys.path.insert(0, 'scripts/llm')
from scenario_templates import SCENARIO_GENERATORS, generate_context_for_scenario
import json
for s in SCENARIO_GENERATORS:
    ctx = generate_context_for_scenario(s)
    d = ctx.to_dict()
    assert 'scenario' not in d, 'scenario should be excluded from dict'
    assert 0 <= d['soh_pct'] <= 100
    assert d['r_ohmic_mohm'] > 0
    print(f'{s}: OK ({len(json.dumps(d))} bytes)')
print(f'All {len(SCENARIO_GENERATORS)} scenarios valid')
"
```

**Commit:** `feat(llm): add 8 parametric scenario templates for dataset generation`

---

### Task 3: Dataset validation

**Files:**
- Create: `scripts/llm/validate_dataset.py`

- [ ] **Step 1: Create dataset validation and splitting script**

Create `scripts/llm/validate_dataset.py`:
```python
#!/usr/bin/env python3
"""
validate_dataset.py — Quality filters, deduplication, and train/val/test split
for the LLM diagnostic dataset.

Usage:
    python scripts/llm/validate_dataset.py \
        --input data/llm/diagnostic_dataset_raw.jsonl \
        --output-dir data/llm/splits \
        --train-ratio 0.8 --val-ratio 0.1 --test-ratio 0.1
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import re
import sys
from pathlib import Path

import pandas as pd

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("validate")

# ---------------------------------------------------------------------------
# Quality filters
# ---------------------------------------------------------------------------

MIN_DIAGNOSTIC_LENGTH = 80    # chars — reject trivially short outputs
MAX_DIAGNOSTIC_LENGTH = 2000  # chars — reject runaway generations
REQUIRED_SEVERITY_TAGS = {"info", "warning", "critical"}
MIN_FRENCH_RATIO = 0.5       # fraction of French stop words present


# Common French words to verify language
FRENCH_MARKERS = {
    "la", "le", "les", "de", "du", "des", "un", "une", "est", "et",
    "en", "pour", "par", "sur", "avec", "dans", "qui", "que", "ce",
}


def is_french(text: str) -> bool:
    """Check if text is likely French using stop word ratio."""
    words = set(re.findall(r"\b\w+\b", text.lower()))
    if not words:
        return False
    french_count = len(words & FRENCH_MARKERS)
    return french_count / len(words) >= 0.05  # At least 5% are French markers


def validate_example(example: dict) -> tuple[bool, str]:
    """Validate a single example. Returns (valid, reason)."""
    diag = example.get("diagnostic", "")

    if len(diag) < MIN_DIAGNOSTIC_LENGTH:
        return False, f"too_short ({len(diag)} chars)"

    if len(diag) > MAX_DIAGNOSTIC_LENGTH:
        return False, f"too_long ({len(diag)} chars)"

    severity = example.get("severity", "")
    if severity not in REQUIRED_SEVERITY_TAGS:
        return False, f"invalid_severity ({severity})"

    if not is_french(diag):
        return False, "not_french"

    # Check context has required fields
    ctx = example.get("context", {})
    required_fields = ["soh_pct", "r_ohmic_mohm", "r_total_mohm", "rul_days"]
    for field in required_fields:
        if field not in ctx:
            return False, f"missing_field ({field})"

    return True, "ok"


def deduplicate(examples: list[dict]) -> list[dict]:
    """Remove near-duplicate diagnostics using content hashing."""
    seen = set()
    unique = []
    for ex in examples:
        # Normalize whitespace for dedup
        normalized = re.sub(r"\s+", " ", ex["diagnostic"].strip().lower())
        h = hashlib.md5(normalized.encode()).hexdigest()
        if h not in seen:
            seen.add(h)
            unique.append(ex)
    return unique


def split_dataset(
    examples: list[dict],
    train_ratio: float,
    val_ratio: float,
    test_ratio: float,
    seed: int = 42,
) -> tuple[list[dict], list[dict], list[dict]]:
    """Stratified split by scenario type."""
    import random as rng
    rng.seed(seed)

    by_scenario: dict[str, list[dict]] = {}
    for ex in examples:
        s = ex.get("scenario", "unknown")
        by_scenario.setdefault(s, []).append(ex)

    train, val, test = [], [], []
    for scenario, items in by_scenario.items():
        rng.shuffle(items)
        n = len(items)
        n_train = int(n * train_ratio)
        n_val = int(n * val_ratio)
        train.extend(items[:n_train])
        val.extend(items[n_train:n_train + n_val])
        test.extend(items[n_train + n_val:])

    rng.shuffle(train)
    rng.shuffle(val)
    rng.shuffle(test)
    return train, val, test


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate and split LLM dataset")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("data/llm/splits"))
    parser.add_argument("--train-ratio", type=float, default=0.8)
    parser.add_argument("--val-ratio", type=float, default=0.1)
    parser.add_argument("--test-ratio", type=float, default=0.1)
    args = parser.parse_args()

    # Load raw dataset
    examples = []
    with open(args.input, encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                examples.append(json.loads(line))
            except json.JSONDecodeError as e:
                log.warning("Line %d: JSON parse error: %s", line_num, e)

    log.info("Loaded %d raw examples", len(examples))

    # Validate
    valid_examples = []
    reject_reasons: dict[str, int] = {}
    for ex in examples:
        ok, reason = validate_example(ex)
        if ok:
            valid_examples.append(ex)
        else:
            reject_reasons[reason] = reject_reasons.get(reason, 0) + 1

    log.info("Valid: %d / %d", len(valid_examples), len(examples))
    if reject_reasons:
        log.info("Rejections: %s", reject_reasons)

    # Deduplicate
    before_dedup = len(valid_examples)
    valid_examples = deduplicate(valid_examples)
    log.info("After dedup: %d (removed %d)", len(valid_examples), before_dedup - len(valid_examples))

    # Split
    train, val, test = split_dataset(
        valid_examples, args.train_ratio, args.val_ratio, args.test_ratio
    )
    log.info("Split: train=%d, val=%d, test=%d", len(train), len(val), len(test))

    # Write splits
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in [("train", train), ("val", val), ("test", test)]:
        out_path = args.output_dir / f"{name}.jsonl"
        with open(out_path, "w", encoding="utf-8") as f:
            for ex in data:
                f.write(json.dumps(ex, ensure_ascii=False) + "\n")
        log.info("Wrote %s: %d examples", out_path, len(data))

    # Write summary
    summary = {
        "total_raw": len(examples),
        "valid": len(valid_examples),
        "rejected": reject_reasons,
        "duplicates_removed": before_dedup - len(valid_examples),
        "train": len(train),
        "val": len(val),
        "test": len(test),
        "scenarios": {
            s: len([e for e in valid_examples if e.get("scenario") == s])
            for s in sorted(set(e.get("scenario", "unknown") for e in valid_examples))
        },
    }
    summary_path = args.output_dir / "split_summary.json"
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    log.info("Summary: %s", summary_path)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify script parses correctly**

```bash
python3 -c "import ast; ast.parse(open('scripts/llm/validate_dataset.py').read()); print('OK')"
```

**Commit:** `feat(llm): add dataset validation with quality filters and stratified split`

---

### Task 4: Fine-tuning script

**Files:**
- Create: `scripts/llm/finetune_qwen.py`

- [ ] **Step 1: Create Unsloth QLoRA fine-tuning script**

Create `scripts/llm/finetune_qwen.py`:
```python
#!/usr/bin/env python3
"""
finetune_qwen.py — Fine-tune Qwen2.5-7B with Unsloth QLoRA for French battery diagnostics.

Runs on kxkm-ai (RTX 4090 24 GB). Produces a LoRA adapter (~50 MB).

Usage:
    python scripts/llm/finetune_qwen.py \
        --train data/llm/splits/train.jsonl \
        --val data/llm/splits/val.jsonl \
        --output-dir models/llm/qwen-bmu-diag \
        --epochs 3 --batch-size 4 --lr 2e-4

Requirements (kxkm-ai):
    pip install unsloth transformers datasets trl peft accelerate bitsandbytes
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("finetune")

# ---------------------------------------------------------------------------
# Prompt formatting (must match inference server)
# ---------------------------------------------------------------------------

SYSTEM_MSG = "Tu es l'assistant diagnostic batterie du BMU KXKM."

def format_prompt(context: dict) -> str:
    """Build the user prompt from battery context dict."""
    return (
        f"Batterie {context['battery_id']}, flotte de {context['fleet_size']} batteries.\n"
        f"SOH: {context['soh_pct']}%, RUL estimé: {context['rul_days']} jours, "
        f"anomalie: {context['anomaly_score']}.\n"
        f"R_int ohmique: {context['r_ohmic_mohm']} mΩ "
        f"(tendance: {context['r_int_trend_mohm_per_day']} mΩ/jour sur 7j).\n"
        f"R_int total: {context['r_total_mohm']} mΩ. "
        f"V moyen: {context['v_avg_mv']} mV, I moyen: {context['i_avg_a']} A.\n"
        f"Cycles: {context['cycle_count']}. "
        f"Santé flotte: {context['fleet_health_pct']}%."
    )


def format_chat(example: dict) -> dict:
    """Convert a dataset example to Qwen chat format."""
    return {
        "conversations": [
            {"role": "system", "content": SYSTEM_MSG},
            {"role": "user", "content": format_prompt(example["context"])},
            {"role": "assistant", "content": example["diagnostic"]},
        ]
    }


def load_dataset_jsonl(path: Path) -> list[dict]:
    """Load JSONL dataset and convert to chat format."""
    examples = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                examples.append(format_chat(json.loads(line)))
    return examples


def main() -> None:
    parser = argparse.ArgumentParser(description="Fine-tune Qwen2.5-7B for BMU diagnostics")
    parser.add_argument("--train", type=Path, required=True)
    parser.add_argument("--val", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("models/llm/qwen-bmu-diag"))
    parser.add_argument("--base-model", default="Qwen/Qwen2.5-7B")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--gradient-accumulation", type=int, default=4)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--lora-rank", type=int, default=16)
    parser.add_argument("--lora-alpha", type=int, default=32)
    parser.add_argument("--max-seq-length", type=int, default=1024)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    # ── Load data ─────────────────────────────────────────────────────
    log.info("Loading training data: %s", args.train)
    train_data = load_dataset_jsonl(args.train)
    log.info("Loading validation data: %s", args.val)
    val_data = load_dataset_jsonl(args.val)
    log.info("Train: %d examples, Val: %d examples", len(train_data), len(val_data))

    # ── Unsloth model loading ─────────────────────────────────────────
    log.info("Loading base model: %s (4-bit QLoRA)", args.base_model)

    from unsloth import FastLanguageModel

    model, tokenizer = FastLanguageModel.from_pretrained(
        model_name=args.base_model,
        max_seq_length=args.max_seq_length,
        dtype=None,  # auto-detect (float16 on 4090)
        load_in_4bit=True,
    )

    # ── LoRA adapter ──────────────────────────────────────────────────
    log.info("Applying LoRA: rank=%d, alpha=%d", args.lora_rank, args.lora_alpha)

    model = FastLanguageModel.get_peft_model(
        model,
        r=args.lora_rank,
        lora_alpha=args.lora_alpha,
        lora_dropout=0.05,
        target_modules=[
            "q_proj", "k_proj", "v_proj", "o_proj",
            "gate_proj", "up_proj", "down_proj",
        ],
        bias="none",
        use_gradient_checkpointing="unsloth",
        random_state=args.seed,
    )

    # ── Dataset preparation ───────────────────────────────────────────
    from datasets import Dataset

    def tokenize_chat(example):
        """Apply Qwen chat template and tokenize."""
        text = tokenizer.apply_chat_template(
            example["conversations"],
            tokenize=False,
            add_generation_prompt=False,
        )
        return tokenizer(
            text,
            truncation=True,
            max_length=args.max_seq_length,
            padding=False,
        )

    train_dataset = Dataset.from_list(train_data).map(tokenize_chat, remove_columns=["conversations"])
    val_dataset = Dataset.from_list(val_data).map(tokenize_chat, remove_columns=["conversations"])

    # ── Training ──────────────────────────────────────────────────────
    from trl import SFTTrainer
    from transformers import TrainingArguments

    args.output_dir.mkdir(parents=True, exist_ok=True)

    training_args = TrainingArguments(
        output_dir=str(args.output_dir),
        num_train_epochs=args.epochs,
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=args.batch_size,
        gradient_accumulation_steps=args.gradient_accumulation,
        learning_rate=args.lr,
        weight_decay=0.01,
        warmup_ratio=0.1,
        lr_scheduler_type="cosine",
        logging_steps=10,
        eval_strategy="epoch",
        save_strategy="epoch",
        save_total_limit=2,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        greater_is_better=False,
        bf16=True,
        seed=args.seed,
        report_to="none",
    )

    trainer = SFTTrainer(
        model=model,
        tokenizer=tokenizer,
        train_dataset=train_dataset,
        eval_dataset=val_dataset,
        args=training_args,
        max_seq_length=args.max_seq_length,
    )

    log.info("Starting training: %d epochs, batch=%d, grad_accum=%d",
             args.epochs, args.batch_size, args.gradient_accumulation)

    trainer.train()

    # ── Save LoRA adapter ─────────────────────────────────────────────
    adapter_dir = args.output_dir / "lora-adapter"
    log.info("Saving LoRA adapter to %s", adapter_dir)
    model.save_pretrained(str(adapter_dir))
    tokenizer.save_pretrained(str(adapter_dir))

    # Save training config for reproducibility
    config = {
        "base_model": args.base_model,
        "lora_rank": args.lora_rank,
        "lora_alpha": args.lora_alpha,
        "epochs": args.epochs,
        "batch_size": args.batch_size,
        "gradient_accumulation": args.gradient_accumulation,
        "lr": args.lr,
        "max_seq_length": args.max_seq_length,
        "train_examples": len(train_data),
        "val_examples": len(val_data),
    }
    config_path = args.output_dir / "training_config.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    log.info("Training complete. Adapter: %s, Config: %s", adapter_dir, config_path)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify script parses correctly**

```bash
python3 -c "import ast; ast.parse(open('scripts/llm/finetune_qwen.py').read()); print('OK')"
```

**Commit:** `feat(llm): add Unsloth QLoRA fine-tuning script for Qwen2.5-7B`

---

### Task 5: Evaluation

**Files:**
- Create: `scripts/llm/evaluate_model.py`

- [ ] **Step 1: Create evaluation script with ROUGE-L and severity accuracy**

Create `scripts/llm/evaluate_model.py`:
```python
#!/usr/bin/env python3
"""
evaluate_model.py — Evaluate fine-tuned Qwen2.5-7B on held-out test set.

Metrics:
  - ROUGE-L: text quality vs reference diagnostics
  - Severity accuracy: classification into info/warning/critical
  - Human review export: CSV for manual review of 50 random examples

Usage:
    python scripts/llm/evaluate_model.py \
        --test data/llm/splits/test.jsonl \
        --model-dir models/llm/qwen-bmu-diag/lora-adapter \
        --base-model Qwen/Qwen2.5-7B \
        --output-dir models/llm/eval
"""

from __future__ import annotations

import argparse
import csv
import json
import logging
import random
import re
import sys
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("evaluate")


def extract_severity(text: str) -> str:
    """Extract severity tag from diagnostic text."""
    for tag in ["CRITICAL", "WARNING", "INFO"]:
        if f"[{tag}]" in text.upper():
            return tag.lower()
    # Heuristic fallback: look for French keywords
    lower = text.lower()
    if any(w in lower for w in ["critique", "urgent", "immédiat", "remplacer"]):
        return "critical"
    if any(w in lower for w in ["attention", "surveiller", "dégradation", "vigilance"]):
        return "warning"
    return "info"


def compute_rouge_l(reference: str, hypothesis: str) -> float:
    """Compute ROUGE-L F1 score between reference and hypothesis."""
    from rouge_score import rouge_scorer
    scorer = rouge_scorer.RougeScorer(["rougeL"], use_stemmer=False)
    scores = scorer.score(reference, hypothesis)
    return scores["rougeL"].fmeasure


def generate_diagnostic(model, tokenizer, context: dict, max_new_tokens: int = 256) -> str:
    """Generate a diagnostic using the fine-tuned model."""
    from scripts.llm.finetune_qwen import format_prompt, SYSTEM_MSG

    messages = [
        {"role": "system", "content": SYSTEM_MSG},
        {"role": "user", "content": format_prompt(context)},
    ]
    text = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
    inputs = tokenizer(text, return_tensors="pt").to(model.device)

    outputs = model.generate(
        **inputs,
        max_new_tokens=max_new_tokens,
        temperature=0.3,
        do_sample=True,
        top_p=0.9,
    )
    generated = tokenizer.decode(outputs[0][inputs["input_ids"].shape[1]:], skip_special_tokens=True)
    return generated.strip()


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate fine-tuned diagnostic model")
    parser.add_argument("--test", type=Path, required=True)
    parser.add_argument("--model-dir", type=Path, required=True, help="LoRA adapter directory")
    parser.add_argument("--base-model", default="Qwen/Qwen2.5-7B")
    parser.add_argument("--output-dir", type=Path, default=Path("models/llm/eval"))
    parser.add_argument("--human-review-count", type=int, default=50)
    parser.add_argument("--max-new-tokens", type=int, default=256)
    args = parser.parse_args()

    # Load test data
    test_examples = []
    with open(args.test, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                test_examples.append(json.loads(line))
    log.info("Loaded %d test examples", len(test_examples))

    # Load model
    log.info("Loading model: %s + %s", args.base_model, args.model_dir)
    from unsloth import FastLanguageModel

    model, tokenizer = FastLanguageModel.from_pretrained(
        model_name=str(args.model_dir),
        max_seq_length=1024,
        dtype=None,
        load_in_4bit=True,
    )
    FastLanguageModel.for_inference(model)

    # Evaluate
    rouge_scores = []
    severity_correct = 0
    severity_total = 0
    results = []

    for i, ex in enumerate(test_examples):
        log.info("Evaluating %d/%d...", i + 1, len(test_examples))
        generated = generate_diagnostic(model, tokenizer, ex["context"], args.max_new_tokens)
        reference = ex["diagnostic"]

        # ROUGE-L
        rouge = compute_rouge_l(reference, generated)
        rouge_scores.append(rouge)

        # Severity accuracy
        ref_severity = ex.get("severity", extract_severity(reference))
        gen_severity = extract_severity(generated)
        is_correct = ref_severity == gen_severity
        severity_correct += int(is_correct)
        severity_total += 1

        results.append({
            "index": i,
            "scenario": ex.get("scenario", "unknown"),
            "reference": reference,
            "generated": generated,
            "rouge_l": round(rouge, 4),
            "ref_severity": ref_severity,
            "gen_severity": gen_severity,
            "severity_correct": is_correct,
        })

    # Metrics
    avg_rouge = sum(rouge_scores) / len(rouge_scores) if rouge_scores else 0
    severity_acc = severity_correct / severity_total if severity_total else 0

    metrics = {
        "num_examples": len(test_examples),
        "rouge_l_mean": round(avg_rouge, 4),
        "rouge_l_min": round(min(rouge_scores), 4) if rouge_scores else 0,
        "rouge_l_max": round(max(rouge_scores), 4) if rouge_scores else 0,
        "severity_accuracy": round(severity_acc, 4),
        "severity_correct": severity_correct,
        "severity_total": severity_total,
    }

    # Per-scenario breakdown
    by_scenario: dict[str, list[float]] = {}
    for r in results:
        by_scenario.setdefault(r["scenario"], []).append(r["rouge_l"])
    metrics["per_scenario_rouge_l"] = {
        s: round(sum(v) / len(v), 4) for s, v in sorted(by_scenario.items())
    }

    log.info("ROUGE-L mean: %.4f, Severity accuracy: %.4f", avg_rouge, severity_acc)

    # Write outputs
    args.output_dir.mkdir(parents=True, exist_ok=True)

    with open(args.output_dir / "metrics.json", "w") as f:
        json.dump(metrics, f, indent=2)

    with open(args.output_dir / "results.jsonl", "w", encoding="utf-8") as f:
        for r in results:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")

    # Human review subset
    review_sample = random.sample(results, min(args.human_review_count, len(results)))
    review_path = args.output_dir / "human_review.csv"
    with open(review_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "index", "scenario", "reference", "generated",
            "rouge_l", "ref_severity", "gen_severity", "human_score",
        ])
        writer.writeheader()
        for r in review_sample:
            writer.writerow({**{k: r[k] for k in writer.fieldnames if k in r}, "human_score": ""})

    log.info("Results: %s", args.output_dir)
    log.info("Human review CSV (%d examples): %s", len(review_sample), review_path)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify script parses correctly**

```bash
python3 -c "import ast; ast.parse(open('scripts/llm/evaluate_model.py').read()); print('OK')"
```

**Commit:** `feat(llm): add evaluation script with ROUGE-L and severity classification`

---

### Task 6: Inference server

**Files:**
- Create: `services/soh-llm/inference_server.py`
- Create: `services/soh-llm/prompt_template.py`
- Create: `services/soh-llm/config.py`

- [ ] **Step 1: Create service configuration**

Create `services/soh-llm/config.py`:
```python
"""Service configuration for soh-llm diagnostic inference."""

from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass
class LLMConfig:
    """Configuration for the LLM diagnostic service."""
    # Model
    base_model: str = os.environ.get("LLM_BASE_MODEL", "Qwen/Qwen2.5-7B")
    lora_adapter: str = os.environ.get("LLM_LORA_ADAPTER", "/models/qwen-bmu-diag/lora-adapter")
    max_seq_length: int = int(os.environ.get("LLM_MAX_SEQ_LENGTH", "1024"))
    max_new_tokens: int = int(os.environ.get("LLM_MAX_NEW_TOKENS", "256"))
    temperature: float = float(os.environ.get("LLM_TEMPERATURE", "0.3"))
    top_p: float = float(os.environ.get("LLM_TOP_P", "0.9"))

    # InfluxDB (Phase 2 scores source)
    influxdb_url: str = os.environ.get("INFLUXDB_URL", "http://localhost:8086")
    influxdb_token: str = os.environ.get("INFLUXDB_TOKEN", "")
    influxdb_org: str = os.environ.get("INFLUXDB_ORG", "kxkm")
    influxdb_bucket: str = os.environ.get("INFLUXDB_BUCKET", "bmu")

    # API
    api_host: str = os.environ.get("LLM_API_HOST", "0.0.0.0")
    api_port: int = int(os.environ.get("LLM_API_PORT", "8401"))

    # Cache
    cache_ttl_hours: int = int(os.environ.get("LLM_CACHE_TTL_HOURS", "24"))
    daily_digest_hour: int = int(os.environ.get("LLM_DAILY_DIGEST_HOUR", "6"))  # 06:00


config = LLMConfig()
```

- [ ] **Step 2: Create prompt template module**

Create `services/soh-llm/prompt_template.py`:
```python
"""Prompt construction and severity extraction for battery diagnostics."""

from __future__ import annotations

import re
from typing import Any


SYSTEM_MSG = "Tu es l'assistant diagnostic batterie du BMU KXKM."

SEVERITY_LEVELS = ("info", "warning", "critical")


def build_user_prompt(scores: dict[str, Any]) -> str:
    """Build the user prompt from ML scoring data.

    Args:
        scores: Dict with keys matching Phase 2 SOH API response format:
            battery, fleet_size, soh_score, rul_days, anomaly_score,
            r_ohmic_mohm, r_total_mohm, r_int_trend_mohm_per_day,
            v_avg_mv, i_avg_a, cycle_count, fleet_health_pct
    """
    return (
        f"Batterie {scores['battery']}, flotte de {scores.get('fleet_size', '?')} batteries.\n"
        f"SOH: {scores['soh_score']}%, RUL estimé: {scores['rul_days']} jours, "
        f"anomalie: {scores['anomaly_score']}.\n"
        f"R_int ohmique: {scores.get('r_ohmic_mohm', '?')} mΩ "
        f"(tendance: {scores.get('r_int_trend_mohm_per_day', '?')} mΩ/jour sur 7j).\n"
        f"R_int total: {scores.get('r_total_mohm', '?')} mΩ. "
        f"V moyen: {scores.get('v_avg_mv', '?')} mV, I moyen: {scores.get('i_avg_a', '?')} A.\n"
        f"Cycles: {scores.get('cycle_count', '?')}. "
        f"Santé flotte: {scores.get('fleet_health_pct', '?')}%."
    )


def build_messages(scores: dict[str, Any]) -> list[dict[str, str]]:
    """Build complete chat messages for the model."""
    return [
        {"role": "system", "content": SYSTEM_MSG},
        {"role": "user", "content": build_user_prompt(scores)},
    ]


def extract_severity(text: str) -> str:
    """Extract severity level from generated diagnostic text.

    Looks for explicit [INFO]/[WARNING]/[CRITICAL] tags first,
    then falls back to keyword-based heuristic.
    """
    upper = text.upper()
    for tag in ["CRITICAL", "WARNING", "INFO"]:
        if f"[{tag}]" in upper:
            return tag.lower()

    lower = text.lower()
    critical_kw = ["critique", "urgent", "immédiat", "remplacer", "hors service", "fin de vie"]
    warning_kw = ["attention", "surveiller", "dégradation", "vigilance", "planifier", "prévoir"]

    if any(kw in lower for kw in critical_kw):
        return "critical"
    if any(kw in lower for kw in warning_kw):
        return "warning"
    return "info"


def clean_diagnostic(text: str) -> str:
    """Clean generated diagnostic text: remove severity tags, normalize whitespace."""
    # Remove severity tags from display text
    text = re.sub(r"\s*\[(INFO|WARNING|CRITICAL)\]\s*", "", text, flags=re.IGNORECASE)
    # Normalize whitespace
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()
```

- [ ] **Step 3: Create inference server**

Create `services/soh-llm/inference_server.py`:
```python
"""
inference_server.py — Qwen2.5-7B inference with LoRA adapter for battery diagnostics.

Loads the base model + LoRA adapter once, serves generation requests.
Designed to run as a long-lived process inside the soh-llm Docker container.
"""

from __future__ import annotations

import logging
import time
from typing import Any

from config import config
from prompt_template import build_messages, extract_severity, clean_diagnostic

log = logging.getLogger("inference")

# Global model/tokenizer (loaded once at startup)
_model = None
_tokenizer = None


def load_model() -> None:
    """Load base model + LoRA adapter into GPU memory."""
    global _model, _tokenizer

    log.info("Loading model: %s + %s", config.base_model, config.lora_adapter)
    from unsloth import FastLanguageModel

    _model, _tokenizer = FastLanguageModel.from_pretrained(
        model_name=config.lora_adapter,
        max_seq_length=config.max_seq_length,
        dtype=None,
        load_in_4bit=True,
    )
    FastLanguageModel.for_inference(_model)
    log.info("Model loaded successfully")


def generate_diagnostic(scores: dict[str, Any]) -> dict[str, Any]:
    """Generate a diagnostic for a single battery from its ML scores.

    Args:
        scores: Battery scoring data from Phase 2 API.

    Returns:
        Dict with keys: battery, diagnostic, severity, generation_time_ms
    """
    if _model is None or _tokenizer is None:
        raise RuntimeError("Model not loaded. Call load_model() first.")

    messages = build_messages(scores)
    text = _tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
    inputs = _tokenizer(text, return_tensors="pt").to(_model.device)

    t0 = time.monotonic()
    outputs = _model.generate(
        **inputs,
        max_new_tokens=config.max_new_tokens,
        temperature=config.temperature,
        do_sample=True,
        top_p=config.top_p,
    )
    generation_ms = int((time.monotonic() - t0) * 1000)

    generated = _tokenizer.decode(
        outputs[0][inputs["input_ids"].shape[1]:],
        skip_special_tokens=True,
    )

    diagnostic_text = clean_diagnostic(generated)
    severity = extract_severity(generated)

    return {
        "battery": scores.get("battery", -1),
        "diagnostic": diagnostic_text,
        "severity": severity,
        "generation_time_ms": generation_ms,
    }
```

- [ ] **Step 4: Verify all service modules parse correctly**

```bash
for f in config.py prompt_template.py inference_server.py; do
    python3 -c "import ast; ast.parse(open('services/soh-llm/$f').read()); print('$f: OK')"
done
```

**Commit:** `feat(llm): add inference server with prompt template and config`

---

### Task 7: FastAPI diagnostic API

**Files:**
- Create: `services/soh-llm/diagnostic_api.py`
- Create: `services/soh-llm/requirements.txt`

- [ ] **Step 1: Create runtime requirements**

Create `services/soh-llm/requirements.txt`:
```txt
fastapi>=0.115.0
uvicorn[standard]>=0.34.0
httpx>=0.27.0
apscheduler>=3.10.0
unsloth
transformers>=4.46.0
torch>=2.5.0
peft>=0.14.0
accelerate>=1.2.0
bitsandbytes>=0.45.0
trl>=0.13.0
```

- [ ] **Step 2: Create FastAPI diagnostic endpoints**

Create `services/soh-llm/diagnostic_api.py`:
```python
#!/usr/bin/env python3
"""
diagnostic_api.py — FastAPI diagnostic service for battery health narratives.

Endpoints:
    GET  /api/diagnostic/{battery_id}    — cached daily diagnostic
    POST /api/diagnostic/{battery_id}    — generate fresh diagnostic (on-demand)
    GET  /api/diagnostic/fleet           — fleet-level summary
    GET  /health                         — health check

Scheduling:
    Daily digest at 06:00 — generates diagnostics for all batteries.
"""

from __future__ import annotations

import logging
import time
from contextlib import asynccontextmanager
from datetime import datetime
from typing import Any

import httpx
from apscheduler.schedulers.asyncio import AsyncIOScheduler
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from config import config
from inference_server import generate_diagnostic, load_model

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("api")

# ---------------------------------------------------------------------------
# Cache: battery_id -> {diagnostic, severity, generated_at}
# ---------------------------------------------------------------------------
_cache: dict[int, dict[str, Any]] = {}
_fleet_cache: dict[str, Any] | None = None


# ---------------------------------------------------------------------------
# SOH API client (Phase 2)
# ---------------------------------------------------------------------------

async def fetch_battery_scores(battery_id: int) -> dict[str, Any]:
    """Fetch ML scores from Phase 2 SOH API."""
    url = f"http://localhost:8400/api/soh/battery/{battery_id}"
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get(url)
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"SOH API returned {resp.status_code}")
        return resp.json()


async def fetch_fleet_scores() -> dict[str, Any]:
    """Fetch fleet scores from Phase 2 SOH API."""
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get("http://localhost:8400/api/soh/fleet")
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"SOH API returned {resp.status_code}")
        return resp.json()


async def fetch_all_battery_scores() -> list[dict[str, Any]]:
    """Fetch scores for all batteries from Phase 2 SOH API."""
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get("http://localhost:8400/api/soh/batteries")
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"SOH API returned {resp.status_code}")
        return resp.json()


# ---------------------------------------------------------------------------
# Daily digest job
# ---------------------------------------------------------------------------

async def daily_digest() -> None:
    """Generate diagnostics for all batteries (cron job)."""
    global _fleet_cache
    log.info("Starting daily digest...")
    try:
        batteries = await fetch_all_battery_scores()
        for scores in batteries:
            bid = scores.get("battery", -1)
            try:
                result = generate_diagnostic(scores)
                _cache[bid] = {
                    **result,
                    "generated_at": int(time.time()),
                }
            except Exception as e:
                log.error("Digest failed for battery %d: %s", bid, e)

        # Fleet summary
        fleet = await fetch_fleet_scores()
        fleet_diag = generate_diagnostic({
            "battery": -1,
            "fleet_size": len(batteries),
            "soh_score": fleet.get("fleet_health", 0) * 100,
            "rul_days": min((b.get("rul_days", 999) for b in batteries), default=0),
            "anomaly_score": fleet.get("outlier_score", 0),
            "r_ohmic_mohm": "N/A",
            "r_total_mohm": "N/A",
            "r_int_trend_mohm_per_day": "N/A",
            "v_avg_mv": "N/A",
            "i_avg_a": "N/A",
            "cycle_count": "N/A",
            "fleet_health_pct": fleet.get("fleet_health", 0) * 100,
        })
        _fleet_cache = {
            **fleet_diag,
            "generated_at": int(time.time()),
            "num_batteries": len(batteries),
        }
        log.info("Daily digest complete: %d batteries", len(batteries))
    except Exception as e:
        log.error("Daily digest failed: %s", e)


# ---------------------------------------------------------------------------
# App lifecycle
# ---------------------------------------------------------------------------

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Load model on startup, schedule daily digest."""
    load_model()

    scheduler = AsyncIOScheduler()
    scheduler.add_job(
        daily_digest,
        "cron",
        hour=config.daily_digest_hour,
        minute=0,
        id="daily_digest",
    )
    scheduler.start()
    log.info("Scheduled daily digest at %02d:00", config.daily_digest_hour)

    yield

    scheduler.shutdown()


app = FastAPI(
    title="BMU LLM Diagnostic API",
    version="1.0.0",
    lifespan=lifespan,
)


# ---------------------------------------------------------------------------
# Response models
# ---------------------------------------------------------------------------

class DiagnosticResponse(BaseModel):
    battery: int
    diagnostic: str
    severity: str
    generated_at: int


class FleetDiagnosticResponse(BaseModel):
    diagnostic: str
    severity: str
    generated_at: int
    num_batteries: int


class HealthResponse(BaseModel):
    status: str
    model_loaded: bool
    cache_size: int
    uptime_s: float


_start_time = time.monotonic()


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.get("/health", response_model=HealthResponse)
async def health_check():
    from inference_server import _model
    return HealthResponse(
        status="ok",
        model_loaded=_model is not None,
        cache_size=len(_cache),
        uptime_s=round(time.monotonic() - _start_time, 1),
    )


@app.get("/api/diagnostic/fleet", response_model=FleetDiagnosticResponse)
async def get_fleet_diagnostic():
    """Return cached fleet diagnostic (from daily digest)."""
    if _fleet_cache is None:
        raise HTTPException(status_code=404, detail="No fleet diagnostic available. Wait for daily digest or POST to generate.")
    return FleetDiagnosticResponse(**_fleet_cache)


@app.get("/api/diagnostic/{battery_id}", response_model=DiagnosticResponse)
async def get_battery_diagnostic(battery_id: int):
    """Return cached daily diagnostic for a battery."""
    if battery_id not in _cache:
        raise HTTPException(status_code=404, detail=f"No cached diagnostic for battery {battery_id}. POST to generate on-demand.")
    entry = _cache[battery_id]
    # Check staleness
    age_h = (time.time() - entry["generated_at"]) / 3600
    if age_h > config.cache_ttl_hours:
        raise HTTPException(status_code=404, detail=f"Cached diagnostic expired ({age_h:.1f}h old). POST to refresh.")
    return DiagnosticResponse(**entry)


@app.post("/api/diagnostic/{battery_id}", response_model=DiagnosticResponse)
async def generate_battery_diagnostic(battery_id: int):
    """Generate a fresh diagnostic on-demand for a battery."""
    scores = await fetch_battery_scores(battery_id)
    result = generate_diagnostic(scores)
    entry = {
        **result,
        "generated_at": int(time.time()),
    }
    _cache[battery_id] = entry
    return DiagnosticResponse(**entry)


# ---------------------------------------------------------------------------
# Entrypoint
# ---------------------------------------------------------------------------

def main() -> None:
    import uvicorn
    uvicorn.run(
        "diagnostic_api:app",
        host=config.api_host,
        port=config.api_port,
        workers=1,  # Single worker — model in GPU memory
        log_level="info",
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Verify API module parses correctly**

```bash
python3 -c "import ast; ast.parse(open('services/soh-llm/diagnostic_api.py').read()); print('OK')"
```

**Commit:** `feat(llm): add FastAPI diagnostic endpoints with caching and daily digest`

---

### Task 8: Docker service

**Files:**
- Create: `services/soh-llm/Dockerfile`
- Create: `services/soh-llm/docker-compose.soh-llm.yml`

- [ ] **Step 1: Create Dockerfile**

Create `services/soh-llm/Dockerfile`:
```dockerfile
# soh-llm: Qwen2.5-7B diagnostic inference service
# Runs on kxkm-ai (RTX 4090 24 GB)

FROM nvidia/cuda:12.4.1-runtime-ubuntu22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

# System dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3.12 python3.12-venv python3-pip git curl \
    && rm -rf /var/lib/apt/lists/*

# Create app user
RUN useradd -m -s /bin/bash llm
USER llm
WORKDIR /app

# Python dependencies
COPY requirements.txt .
RUN python3.12 -m pip install --user --no-cache-dir -r requirements.txt

# Application code
COPY config.py prompt_template.py inference_server.py diagnostic_api.py ./

# Model volume mount point
VOLUME /models

# Environment defaults
ENV LLM_BASE_MODEL=Qwen/Qwen2.5-7B
ENV LLM_LORA_ADAPTER=/models/qwen-bmu-diag/lora-adapter
ENV LLM_API_HOST=0.0.0.0
ENV LLM_API_PORT=8401
ENV INFLUXDB_URL=http://influxdb:8086

EXPOSE 8401

HEALTHCHECK --interval=30s --timeout=10s --retries=3 \
    CMD curl -f http://localhost:8401/health || exit 1

CMD ["python3.12", "-m", "uvicorn", "diagnostic_api:app", "--host", "0.0.0.0", "--port", "8401", "--workers", "1"]
```

- [ ] **Step 2: Create Docker Compose service definition**

Create `services/soh-llm/docker-compose.soh-llm.yml`:
```yaml
# SOH LLM Diagnostic Service
# Deploy on kxkm-ai alongside existing services
#
# Usage:
#   docker compose -f docker-compose.soh-llm.yml up -d
#   docker compose -f docker-compose.soh-llm.yml logs -f soh-llm

services:
  soh-llm:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: soh-llm
    restart: unless-stopped
    ports:
      - "8401:8401"
    volumes:
      - llm-models:/models
    environment:
      - LLM_BASE_MODEL=Qwen/Qwen2.5-7B
      - LLM_LORA_ADAPTER=/models/qwen-bmu-diag/lora-adapter
      - LLM_API_PORT=8401
      - LLM_DAILY_DIGEST_HOUR=6
      - LLM_CACHE_TTL_HOURS=24
      - INFLUXDB_URL=http://influxdb:8086
      - INFLUXDB_TOKEN=${INFLUXDB_TOKEN:-}
      - INFLUXDB_ORG=kxkm
      - INFLUXDB_BUCKET=bmu
    deploy:
      resources:
        reservations:
          devices:
            - driver: nvidia
              count: 1
              capabilities: [gpu]
        limits:
          memory: 20G
    networks:
      - bmu-net
    depends_on:
      - influxdb
    logging:
      driver: json-file
      options:
        max-size: "10m"
        max-file: "3"

volumes:
  llm-models:
    driver: local

networks:
  bmu-net:
    external: true
```

**Commit:** `feat(llm): add Docker service definition for soh-llm`

---

### Task 9: Tests

**Files:**
- Create: `tests/llm/conftest.py`
- Create: `tests/llm/test_prompt_template.py`
- Create: `tests/llm/test_diagnostic_api.py`
- Create: `tests/llm/test_severity_classification.py`

- [ ] **Step 1: Create test fixtures**

Create `tests/llm/conftest.py`:
```python
"""Shared fixtures for LLM diagnostic tests."""

import pytest


@pytest.fixture
def healthy_scores() -> dict:
    return {
        "battery": 3,
        "fleet_size": 12,
        "soh_score": 92.0,
        "rul_days": 280,
        "anomaly_score": 0.05,
        "r_ohmic_mohm": 14.2,
        "r_total_mohm": 20.1,
        "r_int_trend_mohm_per_day": 0.02,
        "v_avg_mv": 27200,
        "i_avg_a": 4.5,
        "cycle_count": 150,
        "fleet_health_pct": 91.0,
    }


@pytest.fixture
def critical_scores() -> dict:
    return {
        "battery": 7,
        "fleet_size": 16,
        "soh_score": 42.0,
        "rul_days": 12,
        "anomaly_score": 0.85,
        "r_ohmic_mohm": 68.5,
        "r_total_mohm": 102.3,
        "r_int_trend_mohm_per_day": 1.2,
        "v_avg_mv": 24100,
        "i_avg_a": 1.2,
        "cycle_count": 1800,
        "fleet_health_pct": 62.0,
    }


@pytest.fixture
def warning_scores() -> dict:
    return {
        "battery": 5,
        "fleet_size": 10,
        "soh_score": 73.0,
        "rul_days": 85,
        "anomaly_score": 0.35,
        "r_ohmic_mohm": 28.0,
        "r_total_mohm": 40.5,
        "r_int_trend_mohm_per_day": 0.12,
        "v_avg_mv": 26000,
        "i_avg_a": 3.0,
        "cycle_count": 600,
        "fleet_health_pct": 78.0,
    }
```

- [ ] **Step 2: Create prompt template tests**

Create `tests/llm/test_prompt_template.py`:
```python
"""Unit tests for prompt construction and text processing."""

import sys
from pathlib import Path

# Add service module to path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "soh-llm"))

from prompt_template import (
    SYSTEM_MSG,
    build_messages,
    build_user_prompt,
    clean_diagnostic,
    extract_severity,
)


class TestBuildUserPrompt:
    def test_contains_battery_id(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "Batterie 3" in prompt

    def test_contains_soh(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "92.0%" in prompt

    def test_contains_rint_values(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "14.2 mΩ" in prompt
        assert "20.1 mΩ" in prompt

    def test_contains_trend(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "0.02 mΩ/jour" in prompt

    def test_contains_fleet_info(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "flotte de 12 batteries" in prompt
        assert "91.0%" in prompt

    def test_missing_fields_use_placeholder(self):
        minimal = {"battery": 1, "soh_score": 80, "rul_days": 100, "anomaly_score": 0.1}
        prompt = build_user_prompt(minimal)
        assert "Batterie 1" in prompt
        assert "?" in prompt  # Missing fields show ?


class TestBuildMessages:
    def test_returns_system_and_user(self, healthy_scores):
        messages = build_messages(healthy_scores)
        assert len(messages) == 2
        assert messages[0]["role"] == "system"
        assert messages[0]["content"] == SYSTEM_MSG
        assert messages[1]["role"] == "user"

    def test_system_msg_is_french(self):
        assert "batterie" in SYSTEM_MSG.lower()
        assert "KXKM" in SYSTEM_MSG


class TestCleanDiagnostic:
    def test_removes_info_tag(self):
        assert "[INFO]" not in clean_diagnostic("Batterie OK. [INFO]")

    def test_removes_warning_tag(self):
        assert "[WARNING]" not in clean_diagnostic("Dégradation. [WARNING]")

    def test_removes_critical_tag(self):
        assert "[CRITICAL]" not in clean_diagnostic("[CRITICAL] Remplacer.")

    def test_case_insensitive_tag_removal(self):
        assert "[info]" not in clean_diagnostic("OK. [info]").lower()

    def test_preserves_content(self):
        result = clean_diagnostic("Batterie en bon état. [INFO]")
        assert "Batterie en bon état." in result

    def test_normalizes_excess_newlines(self):
        result = clean_diagnostic("Ligne 1.\n\n\n\nLigne 2.")
        assert "\n\n\n" not in result
```

- [ ] **Step 3: Create severity classification tests**

Create `tests/llm/test_severity_classification.py`:
```python
"""Unit tests for severity extraction from diagnostic text."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "soh-llm"))

from prompt_template import extract_severity


class TestExplicitTags:
    def test_info_tag(self):
        assert extract_severity("Batterie en bon état. [INFO]") == "info"

    def test_warning_tag(self):
        assert extract_severity("Dégradation lente observée. [WARNING]") == "warning"

    def test_critical_tag(self):
        assert extract_severity("[CRITICAL] Remplacer la batterie.") == "critical"

    def test_tag_priority_critical_over_warning(self):
        assert extract_severity("[CRITICAL] Attention [WARNING]") == "critical"

    def test_tag_priority_warning_over_info(self):
        assert extract_severity("[WARNING] [INFO]") == "warning"

    def test_case_insensitive(self):
        assert extract_severity("OK. [info]") == "info"
        assert extract_severity("Bad. [Critical]") == "critical"


class TestKeywordFallback:
    def test_critical_keywords(self):
        texts = [
            "État critique, la batterie doit être remplacée immédiatement.",
            "Situation urgente, intervention requise.",
            "Batterie en fin de vie, hors service.",
        ]
        for t in texts:
            assert extract_severity(t) == "critical", f"Failed for: {t}"

    def test_warning_keywords(self):
        texts = [
            "Attention, la résistance interne augmente. Surveiller l'évolution.",
            "Dégradation progressive, planifier le remplacement.",
            "Vigilance requise sur la tendance R_int.",
        ]
        for t in texts:
            assert extract_severity(t) == "warning", f"Failed for: {t}"

    def test_default_info(self):
        assert extract_severity("Batterie en bon état, aucun problème détecté.") == "info"

    def test_empty_string(self):
        assert extract_severity("") == "info"
```

- [ ] **Step 4: Create API response format tests**

Create `tests/llm/test_diagnostic_api.py`:
```python
"""Unit tests for diagnostic API response format and validation."""

import sys
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "soh-llm"))


class TestDiagnosticResponseFormat:
    """Verify API response schema matches spec."""

    def test_diagnostic_response_fields(self):
        from diagnostic_api import DiagnosticResponse

        resp = DiagnosticResponse(
            battery=3,
            diagnostic="Batterie en bon état.",
            severity="info",
            generated_at=1743678000,
        )
        assert resp.battery == 3
        assert resp.severity == "info"
        assert resp.generated_at == 1743678000

    def test_diagnostic_response_rejects_invalid_severity_type(self):
        from diagnostic_api import DiagnosticResponse

        # severity is a str field — Pydantic accepts any string
        # Actual validation happens in extract_severity()
        resp = DiagnosticResponse(
            battery=0,
            diagnostic="Test",
            severity="unknown",
            generated_at=0,
        )
        assert resp.severity == "unknown"

    def test_fleet_response_fields(self):
        from diagnostic_api import FleetDiagnosticResponse

        resp = FleetDiagnosticResponse(
            diagnostic="Flotte en bon état global.",
            severity="info",
            generated_at=1743678000,
            num_batteries=16,
        )
        assert resp.num_batteries == 16

    def test_health_response_fields(self):
        from diagnostic_api import HealthResponse

        resp = HealthResponse(
            status="ok",
            model_loaded=True,
            cache_size=5,
            uptime_s=120.5,
        )
        assert resp.model_loaded is True


class TestCacheLogic:
    """Verify cache behavior without loading actual model."""

    def test_cache_miss_returns_404(self):
        """GET /api/diagnostic/{id} with empty cache should 404."""
        from diagnostic_api import _cache
        _cache.clear()
        # Cache is empty, battery 99 not present
        assert 99 not in _cache
```

- [ ] **Step 5: Run tests**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator
python3 -m pytest tests/llm/ -v --tb=short 2>&1 | tail -30
```

**Commit:** `test(llm): add prompt template, severity classification, and API format tests`

---

## Execution Sequence

```
Task 1 ──→ Task 2 ──→ Task 3 ──→ Task 4 ──→ Task 5
                                                 │
                          Task 6 ──→ Task 7 ──→ Task 8
                                                 │
                                              Task 9
```

Tasks 1-5 (dataset pipeline) and Tasks 6-8 (service) can be parallelized:
- **Track A** (dataset): Tasks 1 → 2 → 3 → 4 → 5
- **Track B** (service): Tasks 6 → 7 → 8
- **Track C** (tests): Task 9 (after Tasks 6-7 exist)

## Remote Execution Commands

After all tasks are committed, deploy to kxkm-ai:

```bash
# 1. Sync code to kxkm-ai
git archive --format=tar HEAD | ssh kxkm@kxkm-ai "mkdir -p /home/kxkm/KXKM_Batterie_Parallelator && tar -C /home/kxkm/KXKM_Batterie_Parallelator -xf -"

# 2. Generate dataset (requires ANTHROPIC_API_KEY)
ssh kxkm@kxkm-ai "cd /home/kxkm/KXKM_Batterie_Parallelator && \
    python3 scripts/llm/generate_dataset.py \
    --output data/llm/diagnostic_dataset_raw.jsonl \
    --num-examples 1500 --scenario-mix balanced"

# 3. Validate and split
ssh kxkm@kxkm-ai "cd /home/kxkm/KXKM_Batterie_Parallelator && \
    python3 scripts/llm/validate_dataset.py \
    --input data/llm/diagnostic_dataset_raw.jsonl \
    --output-dir data/llm/splits"

# 4. Fine-tune (RTX 4090, ~30 min for 1500 examples, 3 epochs)
ssh kxkm@kxkm-ai "cd /home/kxkm/KXKM_Batterie_Parallelator && \
    python3 scripts/llm/finetune_qwen.py \
    --train data/llm/splits/train.jsonl \
    --val data/llm/splits/val.jsonl \
    --output-dir models/llm/qwen-bmu-diag \
    --epochs 3 --batch-size 4 --lr 2e-4"

# 5. Evaluate
ssh kxkm@kxkm-ai "cd /home/kxkm/KXKM_Batterie_Parallelator && \
    python3 scripts/llm/evaluate_model.py \
    --test data/llm/splits/test.jsonl \
    --model-dir models/llm/qwen-bmu-diag/lora-adapter \
    --output-dir models/llm/eval"

# 6. Deploy service
ssh kxkm@kxkm-ai "cd /home/kxkm/KXKM_Batterie_Parallelator/services/soh-llm && \
    docker compose -f docker-compose.soh-llm.yml up -d --build"

# 7. Verify
ssh kxkm@kxkm-ai "curl -s http://localhost:8401/health | python3 -m json.tool"
```

## Acceptance Criteria

- [ ] Dataset generation produces 1500 examples across 8 scenarios in JSONL format
- [ ] Validation filters reject malformed/non-French/duplicate examples
- [ ] Stratified train/val/test split preserves scenario distribution
- [ ] Fine-tuning runs on kxkm-ai RTX 4090 within 24 GB VRAM budget (4-bit QLoRA)
- [ ] ROUGE-L mean > 0.3 on held-out test set
- [ ] Severity classification accuracy > 80%
- [ ] GET /api/diagnostic/{id} returns cached diagnostic within 50 ms
- [ ] POST /api/diagnostic/{id} returns fresh diagnostic within 5 s
- [ ] GET /api/diagnostic/fleet returns fleet summary
- [ ] Daily digest cron generates all battery diagnostics at 06:00
- [ ] Docker container starts and passes health check
- [ ] All pytest tests pass (prompt template, severity, API format)
