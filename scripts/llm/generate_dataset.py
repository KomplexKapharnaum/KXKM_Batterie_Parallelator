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
