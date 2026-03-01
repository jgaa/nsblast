# nsblast DB Upgrade Proposal

## 1. Purpose

Introduce explicit, incremental data migrations for persisted RocksDB data.

Goals:
- Track a durable **data schema version** in RocksDB.
- On startup, detect old schema versions and upgrade incrementally.
- Make upgrades deterministic, testable, and safe to resume.
- Support future changes like permission backfills (for example new enum values).

Non-goals:
- Changing DNS RR binary storage format semantics in this proposal.
- One-shot ad-hoc scripts without version tracking.

## 2. Current State

Today startup differentiates only between:
- Fresh DB bootstrap (directory missing).
- Existing DB open.

There is no persisted DB schema/data version and no startup migration pipeline for existing data. This means changes to stored tenant/user/permission structures rely on implicit compatibility or manual operations.

## 3. Proposed Model

### 3.1 Version constants

Add a dedicated version constant for persisted DB data, separate from DNS RR buffer format version:
- `CURRENT_DATA_SCHEMA_VERSION` (new)
- Keep existing `CURRENT_STORAGE_VERSION` (RR format) unchanged.

Rationale: RR binary layout version and account/metadata schema evolution are independent concerns.

### 3.2 Persisted version key

Store current data schema version as a single metadata key in RocksDB.

Recommended placement:
- Column family: `ACCOUNT`
- Key class: add `RealKey::Class::META` (new)
- Key payload: `data_schema_version`
- Value encoding: 4-byte unsigned integer, big-endian (consistent with existing helpers `setValueAt/getValueAt`).

If key is missing, interpret as version `0`.

### 3.3 Startup migration flow

During startup, run migration check after DB/auth initialization and before network services start.

Target sequence (high level):
1. Open DB.
2. Initialize auth manager.
3. Run `DataMigrator::migrate()`.
4. Start replication/http/dns workers.

Rationale: no external traffic while mutations are in progress.

## 4. Migration Engine

Introduce a migration component (e.g. `DataMigrator`).

Responsibilities:
- Read current persisted schema version.
- Compare against `CURRENT_DATA_SCHEMA_VERSION`.
- Apply migrations one version at a time.
- Persist new version after each successful step.
- Abort startup on migration failure.

Pseudo-flow:

```text
version = readVersion() // default 0 if missing
while version < CURRENT_DATA_SCHEMA_VERSION:
    target = version + 1
    applyMigration(target)
    writeVersion(target)
    commit
    version = target
```

### 4.1 Transaction boundaries

Preferred default: one transaction per migration step.

Benefits:
- Clear checkpoint after each version.
- Restart resumes from last committed version.
- Easier rollback semantics (transaction abort only affects one step).

### 4.2 Idempotency and restart safety

Each migration step should be idempotent or naturally convergent:
- Re-running the same step should not corrupt data.
- If process exits before commit, step is retried safely.

### 4.3 Failure policy

If any step fails:
- Log source/target version and migration name.
- Fail startup.
- Do not serve traffic on partially migrated state.

## 5. Migration Authoring Rules

For each new schema version:
- Add exactly one migration function, e.g. `migrateToV3(...)`.
- Keep scope narrow and explicit.
- Add tests for both:
  - Upgrade from previous version.
  - No-op when already on target version.
- Do not mutate unrelated entities.
- Preserve backward readability only as needed for migration input; runtime should operate on post-migration state.

Suggested naming:
- `Migration_001_InitVersion`
- `Migration_002_AddPermissionDynip`
- etc.

## 6. Example: Permission Backfill Migration

When adding a new permission enum (for example `DYNIP`):
- Iterate tenants in `ACCOUNT` data.
- Ensure `tenant.allowedpermissions` contains the new permission.
- Optionally update built-in/full-access roles according to explicit policy.
- Re-serialize and write only changed tenant records.

Policy must be explicit in migration description to avoid surprising privilege changes.

## 7. Compatibility and Defaults

### 7.1 Fresh installations

Two valid approaches:
- A) Write schema version at bootstrap and still run migrator (no-op).
- B) Let migrator initialize from `0` to current.

Recommendation: **B** for a single consistent path.

### 7.2 Existing installations

If no version key exists, treat as version `0` and migrate forward.

### 7.3 Downgrade behavior

Downgrades are out of scope for automatic support.
If binary version is older than DB schema, startup should fail with clear error.

## 8. Observability

Add startup logs:
- Current DB schema version.
- Target schema version.
- Each applied migration step and duration.
- Final success message.

Metric:
- Gauge/counter current version at runtime.
- Show data version as a data-point in the `/version` API endpoint.

Replication:
- Replication messages gets a numeric field for the primary servers current data version
- Followers stop replication when they detect a version different than than their own and print a warning once. They continue to serve DNS requests while they await restart/upgrade.

## 9. Testing Strategy

Add tests covering:
- Missing version key => migrates from 0.
- Incremental multi-step upgrade (0 -> N).
- Crash/restart simulation between steps (resume behavior).
- Idempotency of each migration.
- Permission backfill correctness for representative tenant datasets.

Prefer integration-style tests using temporary RocksDB directories.

## 10. Rollout Plan

1. Introduce schema version key support and migrator framework with no-op migration path.
2. Set `CURRENT_DATA_SCHEMA_VERSION = 1` with initial migration writing version key.
3. Add first real migration (e.g. permission backfill) as next version.
4. Add CI tests for migration paths.
5. Document operator guidance for failed migrations (logs + restore from backup).

## 11. Behaviour

- Dry-run mode (`--check-migrations`) that validates applicability without writes?
- The built-in admin user is repaired to hold every built-in system-tenant role once,
  using canonical uppercase role references.
- Legacy `Administrator` role references are removed when the system tenant already
  has the real built-in roles.

## 12. Summary

This proposal adds a durable, incremental DB upgrade mechanism with explicit schema versioning in RocksDB. It enables safe evolution of tenant/user/permission data across releases and removes reliance on implicit compatibility.
