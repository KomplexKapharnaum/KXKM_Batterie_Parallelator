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
