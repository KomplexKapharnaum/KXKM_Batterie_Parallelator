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
