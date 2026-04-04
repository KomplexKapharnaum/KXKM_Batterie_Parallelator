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
