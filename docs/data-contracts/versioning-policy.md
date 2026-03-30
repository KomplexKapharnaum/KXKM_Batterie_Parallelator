# Data Contract Versioning Policy

Statut: draft v0.1  
Scope: telemetry + inference schemas used by BMU edge and cloud analytics

## 1. SemVer rules

- Version format: `MAJOR.MINOR.PATCH`
- `MAJOR`: breaking schema change (field removed/renamed, type changed, semantic change)
- `MINOR`: backward-compatible addition (optional field, enum extension if explicitly tolerated)
- `PATCH`: non-structural fixes (description, examples, typo)

## 2. Compatibility policy

- Producers must always emit an explicit `schema_version` field.
- Consumers must reject unknown `MAJOR` versions.
- Consumers may accept higher `MINOR` versions only if unknown fields are optional and safely ignored.
- `PATCH` updates must not change runtime behavior.

## 3. Change control

For each schema update, include:
- Changelog entry with rationale and migration notes.
- Updated JSON schema files under `docs/data-contracts/`.
- Validation evidence from CI or local checks.
- Impact note on firmware, cloud ingestion, and dashboards.

## 4. Deprecation window

- Deprecated fields are announced in one `MINOR` release before removal.
- Removal is only allowed in the next `MAJOR` release.
- During deprecation window, both old and new fields may coexist.

## 5. Ownership

- Firmware lead validates edge compatibility.
- ML lead validates feature/inference contract compatibility.
- QA lead validates acceptance tests and evidence before release.