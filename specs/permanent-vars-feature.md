# Feature Specification

# Permanent Variables (PV) System

**Component:** Core
**Storage:** RocksDB
**Serialization:** Protobuf
**Bootstrap:** CLI only
**Status:** Draft

---

# 1. Purpose

The Permanent Variables (PV) system provides:

* Server-wide persistent configuration variables
* Typed, validated configuration values
* Lock-free runtime access
* Admin-controlled modification via REST and CLI
* CLI-only bootstrap before DB initialization

All optional or advanced features MUST default to disabled.

---

# 2. Design Overview

## 2.1 Storage Model

The PV system stores a single Protobuf snapshot message in RocksDB.

Key:

```
vars/snapshot
```

Value:

```
VarsSnapshot (protobuf)
```

This snapshot represents the complete effective configuration.

No per-variable keys are stored.

---

## 2.2 Runtime Access Model

The server maintains:

```cpp
std::atomic<std::shared_ptr<const VarsSnapshot>> g_vars;
```

### Read Path (hot path)

* Load shared_ptr atomically
* Access typed fields directly
* No mutex
* No blocking

### Write Path

1. Load snapshot
2. Clone snapshot
3. Modify requested field
4. Validate entire snapshot
5. Persist to RocksDB
6. Atomically swap pointer

This provides:

* Lock-free reads
* Consistent configuration view
* RCU-style update semantics
* Zero partial state exposure

---

# 3. Protobuf Schema

File: `nsblast.proto`

```proto
message VarsSnapshot {
  uint32 schema_version = 1; This is a non-mutable variable that is set by the server

  // =========================
  // Core system
  // =========================

  bool pv_bootstrapped = 2; This is a non-mutable variable that is set by the server.
  string cluster_role = 3; // Migrate `--cluster-role` from global to bootstrap cli arg and update the C++ code to use the arg. This is a non-mutable variable that cannot be changed once it's set. Valid roles are "primary", "follower", and "none". None simply means stand-alone server (usually fo testing). The server cannot start up and serve gRPC, DNS or HTTP without having a valid role.

  // =========================
  // DynIP Feature
  // (Remove the dynip cli flags and migrate them)
  // =========================

  bool dynip_enabled = 100;
  string dynip_realm = 101;                // e.g. "dynip.nsblast.com"
  uint32 dynip_max_hosts_per_root = 102;   // default 5
  uint32 dynip_default_ttl = 103;          // seconds
  uint32 dynip_min_ttl = 104;
  uint32 dynip_max_ttl = 105;
  bool dynip_allow_txt = 106;

  // Future variables go here
}
```

Note: The cli's `vars set` command is allowed to change any variable if `--force` is applied. It may not make sense today, but it may be required to deal with a disaster at some later time.

---

# 4. Default Values

Defaults are compiled into the server and used when building an initial snapshot.

All advanced features MUST default to disabled.

### DynIP Defaults

| Variable                 | Default |
| ------------------------ | ------- |
| dynip_enabled            | false   |
| dynip_realm              | ""      |
| dynip_max_hosts_per_root | 5       |
| dynip_default_ttl        | 300     |
| dynip_min_ttl            | 60      |
| dynip_max_ttl            | 3600    |
| dynip_allow_txt          | false   |

If `dynip_enabled = false`, the DynIP subsystem MUST reject all DynIP provisioning and update requests.

---

# 5. Bootstrap Process

## 5.1 Conditions

Bootstrap is required when:

* `vars/snapshot` key does not exist in RocksDB

No REST bootstrap endpoint is provided.

The default variables are written to the key if they don't exist. Normlly this happen during bootstrap of the db, but if an existing server is upgraded, the variables must be set to default values and saved. The data is always written as a transaction to the replication table on the primary server so it gets replicated when followers connect.

If required variables are unset, the app must log what is missing on error level and exit.

---

## 5.2 Bootstrap Command

```
nsblast bootstrap --set name=value --set name=value ...
```

Rules:

* Only allowed if snapshot does not exist
* Must set all mandatory variables
* Builds snapshot from defaults
* Applies provided overrides
* Validates snapshot
* Writes snapshot
* Sets `pv_bootstrapped = true`

After successful bootstrap:

* Server may start normally
* REST variable modification becomes available

---

# 6. CLI Interface

CLI operates directly on RocksDB without starting network listeners.

## 6.1 List Variables

```
nsblast vars list
```

Shows:

* name
* effective value
* default value
* requires_restart flag
* non_mutable flag
* description

Optional:

```
nsblast vars list --json
```

---

## 6.2 Get Variable

```
nsblast vars get dynip_enabled
```

---

## 6.3 Set Variable

```
nsblast vars set dynip_enabled=true
```

Note: non_mutable variables cannot be set after they have a value. Non-string non_mutables may be optional variable in the proto definition to enforce this. Override this constraint at the cli level with `--force`.

Flow:

* Load snapshot
* Clone
* Apply value
* Validate
* Persist
* Exit 0 on success

---

## 6.4 Unset Variable (Reset to Default)

```
nsblast vars unset dynip_enabled
```

Note: non_mutable variables cannot be set after they have a value. Non-string non_mutables may be optional variable in the proto definition to enforce this. Override this constraint at the cli level with `--force`.


Meaning:

* Reset field to compiled default
* Persist new snapshot

---

## 6.5 Exit Codes

| Code | Meaning            |
| ---- | ------------------ |
| 0    | Success            |
| 2    | Validation error   |
| 3    | Variable not found |
| 4    | Not bootstrapped   |
| 5    | Storage error      |

---

# 7. REST API

Base path:

```
/admin/vars
```

Requires admin permission.

---

## 7.1 List Variables

```
GET /admin/vars
```

Response:

```json
{
  "items": [
    {
      "name": "dynip_enabled",
      "value": false,
      "default": false,
      "non_mutable": false,
      "requires_restart": false,
      "description": "Enables the dynip feature."
    }
  ]
}
```

---

## 7.2 Get One

```
GET /admin/vars/{name}
```

---

## 7.3 Set Variable

Note: non_mutable variables cannot be changed after they have a value. Non-string non_mutables may be optional variable in the proto definition to enforce this. There is no override at the API level.

```
PUT /admin/vars/{name}
```

Body:

```json
{ "value": true }
```

---

## 7.4 Reset to Default

```
DELETE /admin/vars/{name}
```

Note: non_mutable variables cannot be changed after they have a value. Non-string non_mutables may be optional variable in the proto definition to enforce this. There is no override at the API level.


---

# 8. Permissions

Server-level permissions:

| Permission | Description      |
| ---------- | ---------------- |
| VARS_LIST  | List variables   |
| VARS_READ  | Read variable    |
| VARS_SET   | Modify variable  |
| VARS_UNSET | Reset to default |

Only admin role should have these.

Bootstrap requires local CLI execution and does not use REST permissions.

---

# 9. Validation Rules

Validation occurs on every modification and bootstrap.

Example rules:

### DynIP

* If `dynip_enabled = true`:

  * `dynip_realm` must be non-empty
  * `dynip_realm` must be valid FQDN
* `dynip_min_ttl <= dynip_default_ttl <= dynip_max_ttl`
* TTL values must be within allowed DNS bounds

If validation fails:

* No change persisted
* CLI returns exit code 2
* REST returns HTTP 400

---

# 10. Restart Semantics

Some variables may require restart (future flag).

If a variable marked `requires_restart` is modified:

* Persist value
* Mark in response that restart required
* Do not apply live

DynIP variables SHOULD be hot-reloadable.

We store and reflect `restart required` to a corresponding metrics state.

---

# 11. Schema Evolution

`snapshot.schema_version` MUST be incremented if breaking changes occur.

On load:

* If version older → perform migration in memory
* Persist upgraded snapshot

Protobuf unknown-field rules allow forward compatibility.

# 11.a Replication

The variables protobuf message is replicated to followers in the same way zone updates are.

---

# 12. Failure Handling

If `vars/snapshot` is corrupt:

* Server must refuse to start
* CLI `vars export` may attempt recovery
* Admin intervention required

---

# 13. Performance Characteristics

Read:

* One atomic pointer load
* Direct field access
* No locking
* No heap allocation

Write:

* Clone protobuf
* Single RocksDB write
* Atomic pointer swap

Expected write frequency: very low
Expected read frequency: high

---

# 14. Rationale for Snapshot Design

This design was chosen because:

* Reads are far more frequent than writes
* Eliminates mutex usage
* Ensures consistent cross-field configuration
* Simplifies reasoning about system state
* Integrates cleanly with DynIP hot paths

---

# 15. Future Extensions

* Audit trail (append-only protobuf message)
* Change notifications via internal event bus
* Export/import CLI
* Per-cluster variable replication (future)
* Feature flag grouping

---

# 16. Testability Strategy

As runtime configuration moves from `Config` to permanent variables, tests MUST use PV paths instead of mutating `Config`.

## 16.1 Rules for New and Migrated Variables

* If a runtime behavior is PV-backed, tests MUST set it via `Vars` APIs.
* Tests MUST NOT configure PV-backed behavior via `Config`.
* `Config` remains for process/bootstrap wiring and non-PV concerns only.

## 16.2 Test Fixture API

Shared fixtures (for example `MockServer`) SHOULD provide helpers to reduce churn:

* `setVar(name, value, force=false, allowForce=false)`
* `setVars({"name=value", ...}, force=false, allowForce=false)`
* `setClusterRole("primary" | "follower" | "none")`

This keeps tests aligned with production code paths and avoids repeated direct calls into low-level setup.

## 16.3 Component Seams

Components that only need read access to runtime variables SHOULD depend on a read-only interface:

* `VarsViewIf` with `snapshot()` access

This allows unit tests to inject fake snapshots without requiring full `Server` + RocksDB setup.

Naming convention for pure interfaces is `*If` (not `I*`).

## 16.4 Migration Procedure for Tests

When a variable is migrated from `Config` to PV:

1. Update production code to read from `VarsSnapshot`.
2. Add/update fixture helper if needed.
3. Replace `Config`-based test setup with fixture var helper calls.
4. Keep one focused test that verifies required-var startup/runtime failure behavior when unset.
