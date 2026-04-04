# Multi-stage build for SOH scoring pipeline
FROM python:3.12-slim AS base

WORKDIR /app

# Install system dependencies for PyTorch + PyG
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

COPY pyproject.toml .
COPY src/ src/

RUN pip install --no-cache-dir -e .

# --- API service ---
FROM base AS soh-api
EXPOSE 8400
CMD ["soh-api"]

# --- Scheduler (ETL + scoring) ---
FROM base AS soh-scheduler
CMD ["soh-scheduler"]

# --- Training (includes dev deps) ---
FROM base AS soh-train
RUN pip install --no-cache-dir -e ".[dev]"
CMD ["bash"]
