# DevOps/Admin CLI Spec v1

## 0. Implementation Plan: Config + Secret Storage

Goal: add persistent client config and secure password handling while server auth is Basic Auth only.

Phase 1:
- Add config file support for `~/.config/nsblast/nsblastctl.yaml`.
- Persist `server` and `username` defaults.
- Keep password out of config by default.

Phase 2:
- Add `auth` commands for login/password management.
- Integrate Linux Secret Service (KDE Wallet / GNOME Keyring) for secret storage.
- Define fallback behavior when keyring is unavailable.

Phase 3:
- Implement precedence rules and non-interactive automation flows.
- Add tests for config permissions, credential precedence, and key uniqueness.

## 1. Scope

This document defines the behavior for a Rust command-line tool for nsblast operators.

Primary goals:
- Simple day-to-day management of tenants, zones, and RR sets.
- Safe bulk changes from CSV files.
- Portable backup/restore of tenant or zone data via JSON.
- Script-friendly and predictable non-interactive behavior.

## 2. Runtime and Interface

- Implementation language: Rust.
- Binary name: `nsblastctl`.
- API transport: HTTPS/HTTP to nsblast API endpoint.
- Auth: HTTP Basic Auth (current server capability).
- Output modes: `table` (default), `json`, `yaml`.

### 2.1 Client config file

- Path: `~/.config/nsblast/nsblastctl.yaml`
- Purpose:
  - Persist server URL and username defaults.
  - Persist password source metadata.
  - Avoid plaintext password in config unless explicitly allowed.

Example:

```yaml
server: "https://dns.example.net"
username: "ops-admin"
password_source: "keyring" # keyring | config | env | prompt
password_key_id: "io.nsblast.nsblastctl/basic-auth/v1/2f4f...c9a"
timeout: 30
output: "table"
profile: "default"
```

Required behavior:
- Create parent directory with mode `0700` if missing.
- Create config file with mode `0600`.
- Refuse to load config with broader permissions unless `--allow-insecure-config` is set.

## 3. Global CLI Rules

### 3.1 Global options

- `--server <url>`: nsblast base URL.
- `--username <username>`: Basic auth username.
- `--password <password>`: Basic auth password (discouraged in shell history).
- `--password-stdin`: Read password from stdin.
- `--save-password`: Store password in local secret storage if available.
- `--no-keyring`: Disable keyring lookup/storage for this command.
- `--allow-insecure-config`: Allow plaintext password in config file.
- `--profile <name>`: Named profile from config.
- `--output <table|json|yaml>`: Output formatter.
- `--timeout <seconds>`: Request timeout. Default `30`.
- `--verbose`: Increase log detail.
- `--quiet`: Suppress non-error informational output.
- `--yes`: Confirm destructive actions without prompt.
- `--dry-run`: Validate and preview changes only; no writes.

### 3.2 Exit codes

- `0`: Success.
- `2`: Input/validation error.
- `3`: Authentication/authorization error.
- `4`: Conflict/precondition error.
- `5`: Transport/server/internal error.

### 3.3 Safety defaults

- All destructive operations require `--yes` unless interactive confirm is explicitly enabled later.
- Import operations default to `--conflict fail` unless user chooses otherwise.
- Bulk commands print a summary: `processed`, `created`, `updated`, `deleted`, `skipped`, `failed`.
- Password must never appear in logs, error output, or debug traces.

### 3.4 Credential source precedence

Highest to lowest:
- CLI options (`--username`, `--password`, `--password-stdin`)
- Keyring lookup from `password_key_id` or derived key id
- Config plaintext password (`password_source: config`)
- Environment fallback (`NSBLASTCTL_USERNAME`, `NSBLASTCTL_PASSWORD`)
- Interactive prompt (TTY only)

If no password source is available in non-interactive mode, exit with code `2`.

### 3.5 Environment variables

- `NSBLASTCTL_SERVER`: Default server URL when `--server` is not provided.
- `NSBLASTCTL_USERNAME`: Default username when `--username` is not provided.
- `NSBLASTCTL_PASSWORD`: Password fallback if no CLI, keyring, or config password is available.

## 4. Command Model

- `nsblastctl zone ...`
- `nsblastctl rr ...`
- `nsblastctl tenant ...`
- `nsblastctl import ...`
- `nsblastctl export ...`
- `nsblastctl auth ...`
- `nsblastctl health ...`

## 5. Zone Commands

- `zone list [--tenant <tenant>] [--filter <expr>]`
- `zone get <zone>`
- `zone create <zone> --tenant <tenant> [--primary-ns <fqdn>] [--admin <mail>]`
- `zone delete <zone> --yes`
- `zone export <zone> --file <path.json>`
- `zone import --file <path.json> [--upsert] [--conflict <fail|overwrite|skip>]`

Behavior:
- `zone create` creates SOA/NS bootstrap records if server policy requires it.
- `zone import` validates zone ownership and schema before write.

## 6. RR Commands

- `rr list <zone> [--name <fqdn>] [--type <A|AAAA|CNAME|MX|TXT|NS|SRV|CAA|PTR|...>]`
- `rr add <zone> --name <name> --type <type> --ttl <ttl> --value <value> [--priority <n>] [--weight <n>] [--port <n>]`
- `rr update <zone> --id <rr-id> [--name ...] [--ttl ...] [--value ...] [...]`
- `rr delete <zone> --id <rr-id> --yes`
- `rr bulk-add --csv <path.csv> [--upsert] [--dry-run]`
- `rr bulk-delete --csv <path.csv> [--dry-run] --yes`

Behavior:
- `rr bulk-add` may target multiple zones through CSV `zone` column.
- `rr update` supports partial field updates.
- `rr delete` is exact-match by ID for unambiguous deletes.

## 7. Tenant Commands

- `tenant list`
- `tenant get <tenant>`
- `tenant create <tenant> [--display-name <name>]`
- `tenant delete <tenant> --yes`
- `tenant export <tenant> --file <path.json>`
- `tenant import --file <path.json> [--upsert] [--conflict <fail|overwrite|skip>]`

Behavior:
- Tenant export/import includes all zones and RR sets under that tenant.
- Optional inclusion of users/roles can be controlled later via `--include-access`.

## 8. Import/Export Commands

- `export zone <zone> --file <path.json>`
- `export tenant <tenant> --file <path.json>`
- `import zone --file <path.json> [--upsert] [--conflict <fail|overwrite|skip>]`
- `import tenant --file <path.json> [--upsert] [--conflict <fail|overwrite|skip>]`

Behavior:
- Export is deterministic (stable field ordering where feasible).
- Import validates first, then applies in dependency-safe order.

## 8.1 Auth Commands

- `auth login [--server <url>] [--username <name>] [--password-stdin] [--save-password]`
- `auth set-password [--server <url>] [--username <name>] [--password-stdin] [--save-password]`
- `auth logout [--server <url>] [--username <name>]`
- `auth whoami [--server <url>]`

Behavior:
- `auth login` verifies credentials with the server and stores non-secret defaults in config.
- With `--save-password`, password is written to keyring when available.
- `auth logout` deletes matching keyring entry and clears credential references in config.
- If keyring is unavailable and `--save-password` is requested:
  - Default behavior is fail with an actionable error.
  - With `--allow-insecure-config`, allow storing plaintext in config.

## 8.2 Secret Storage (KDE/GNOME)

Linux desktop target:
- Use Secret Service (works with GNOME Keyring and KDE Wallet integrations).
- Suggested Rust crate/backend: `keyring` with Secret Service.

Global uniqueness of key path/id:
- Service name: `io.nsblast.nsblastctl`
- Secret key namespace: `io.nsblast.nsblastctl/basic-auth/v1/<id>`
- `<id>` must be: `hex(sha256(lowercase(canonical_server_url) + \"\\n\" + username))`

Secret metadata attributes:
- `app=io.nsblast.nsblastctl`
- `kind=basic-auth`
- `server=<canonical server url>`
- `username=<username>`
- `schema=v1`

Rules:
- If keyring write succeeds, config must not contain plaintext password.
- Config stores only `password_source: keyring` and `password_key_id`.
- On keyring read failure, emit a clear error and do not silently downgrade to plaintext storage.

## 9. CSV ("cvs") Format

Note: The requested "cvs" format is treated as standard CSV (comma-separated values).

### 9.1 Encoding and parsing rules

- UTF-8 file encoding.
- Header row is required.
- Delimiter: comma `,`.
- Quote character: `"`.
- Spaces around unquoted fields are trimmed.
- Empty lines are ignored.
- Lines with first non-space char `#` are comments and ignored.

### 9.2 RR bulk-add CSV schema

Required columns:
- `zone`: Zone name (FQDN).
- `name`: RR owner name (`@`, relative label, or FQDN).
- `type`: RR type.
- `ttl`: Positive integer seconds.
- `value`: RR value/rdata payload.

Optional columns:
- `priority`: Required by MX; optional otherwise.
- `weight`: SRV weight.
- `port`: SRV port.
- `comment`: Free text metadata.
- `tenant`: Optional override; when omitted, server resolves from zone.

### 9.3 RR bulk-delete CSV schema

Supported modes:
- By ID: `id` (preferred).
- By tuple match: `zone,name,type,value`.

If both are present, `id` wins.

### 9.4 CSV example

```csv
zone,name,type,ttl,value,priority,weight,port,comment
example.com,@,A,300,192.0.2.10,,,,Apex A
example.com,www,A,300,192.0.2.20,,,,Web node
example.com,@,MX,3600,mail.example.com,10,,,Mail route
example.com,_sip._tcp,SRV,3600,sip.example.com,,5,5060,SIP service
```

### 9.5 CSV validation behavior

- `--strict`: stop on first error.
- Default: collect all row errors and fail at end.
- Error output must include row number, column name, and reason.

## 10. JSON Formats

## 10.1 Common JSON rules

- UTF-8 JSON.
- Unknown fields ignored by default; `--strict-schema` rejects unknown fields.
- `schema_version` is required for forward compatibility.

### 10.2 Zone export/import JSON schema

```json
{
  "schema_version": "nsblast.zone.v1",
  "zone": {
    "name": "example.com",
    "tenant": "acme",
    "soa": {
      "mname": "ns1.example.com",
      "rname": "hostmaster.example.com",
      "serial": 2026022501,
      "refresh": 3600,
      "retry": 600,
      "expire": 1209600,
      "minimum": 300
    },
    "metadata": {
      "created_at": "2026-02-25T12:00:00Z",
      "updated_at": "2026-02-25T12:00:00Z"
    }
  },
  "records": [
    {
      "name": "@",
      "type": "A",
      "ttl": 300,
      "value": "192.0.2.10"
    },
    {
      "name": "www",
      "type": "A",
      "ttl": 300,
      "value": "192.0.2.20"
    }
  ]
}
```

Required fields:
- `schema_version`
- `zone.name`
- `zone.tenant`
- `records[]` entries with `name,type,ttl,value`

### 10.3 Tenant export/import JSON schema

```json
{
  "schema_version": "nsblast.tenant.v1",
  "tenant": {
    "name": "acme",
    "display_name": "Acme Corp",
    "metadata": {
      "created_at": "2026-02-25T12:00:00Z",
      "updated_at": "2026-02-25T12:00:00Z"
    }
  },
  "access": {
    "users": [
      { "name": "alice", "roles": ["admin"] }
    ],
    "roles": [
      { "name": "admin", "permissions": ["zones:read", "zones:write"] }
    ]
  },
  "zones": [
    {
      "name": "example.com",
      "soa": {
        "mname": "ns1.example.com",
        "rname": "hostmaster.example.com",
        "serial": 2026022501,
        "refresh": 3600,
        "retry": 600,
        "expire": 1209600,
        "minimum": 300
      },
      "records": [
        { "name": "@", "type": "A", "ttl": 300, "value": "192.0.2.10" }
      ]
    }
  ]
}
```

Required fields:
- `schema_version`
- `tenant.name`
- `zones[]`

### 10.4 Conflict modes for JSON import

- `fail`: abort if target entity already exists.
- `overwrite`: replace existing entity state.
- `skip`: ignore existing entities; import only missing entities.

## 11. Import Execution Order

To avoid referential failures, imports apply in this order:
- Tenant metadata
- Access metadata (users/roles), if included
- Zones
- RR sets

For zone JSON import, apply:
- Zone metadata/soa
- RR sets

## 12. UX Examples

```bash
# Create a zone
nsblastctl zone create example.com --tenant acme

# Add one RR
nsblastctl rr add example.com --name www --type A --ttl 300 --value 192.0.2.20

# Bulk add from CSV
nsblastctl rr bulk-add --csv ./records.csv --upsert --dry-run
nsblastctl rr bulk-add --csv ./records.csv --upsert

# Export/import a zone
nsblastctl export zone example.com --file ./example.com.zone.json
nsblastctl import zone --file ./example.com.zone.json --conflict fail

# Export/import a full tenant
nsblastctl tenant export acme --file ./acme.tenant.json
nsblastctl tenant import --file ./acme.tenant.json --upsert --conflict overwrite
```

## 13. Rust Implementation Guidance (non-normative)

- CLI parser: `clap`.
- Async runtime: `tokio`.
- HTTP client: `reqwest`.
- Serialization: `serde`, `serde_json`, `csv`.
- Suggested crate layout:
  - `src/main.rs`
  - `src/cmd/{zone,rr,tenant,import,export,health}.rs`
  - `src/model/*.rs`
  - `src/client/*.rs`

## 14. Acceptance Criteria

- Can create/list/delete zones and RRs via CLI.
- Can bulk add/delete RRs from CSV with clear validation errors.
- Can export/import zone JSON losslessly for supported fields.
- Can export/import tenant JSON including zones and RR sets.
- Supports dry-run and deterministic exit codes.
- Commands are automation-friendly with `--output json`.
