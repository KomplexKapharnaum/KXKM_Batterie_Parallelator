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
    # Import from sibling module
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from finetune_qwen import format_prompt, SYSTEM_MSG

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
