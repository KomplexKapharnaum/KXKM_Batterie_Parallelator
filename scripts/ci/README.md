# scripts/ci

CI support scripts and conventions for KXKM Batterie Parallelator.

## Goals

- Produce verifiable evidence for build/test gates.
- Keep safety checks explicit and reproducible.
- Avoid marking tasks complete without command-level proof.

## Reference commands

Firmware host tests:
```bash
pio test -e sim-host
```

Firmware build (target S3):
```bash
pio run -e kxkm-s3-16MB
```

Memory budget guardrail:
```bash
scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85
```

## Evidence checklist

For each gate, capture:
- command executed
- pass/fail status
- timestamp
- artifact/log path

## Notes

- Prefer small incremental validation runs over late full-batch checks.
- Keep plan status synchronized with actual evidence.