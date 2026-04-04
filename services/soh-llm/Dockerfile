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
