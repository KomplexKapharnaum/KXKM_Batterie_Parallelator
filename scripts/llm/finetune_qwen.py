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
