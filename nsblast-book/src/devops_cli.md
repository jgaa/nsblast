# DevOps CLI

`nsblastctl` is a Rust command-line tool for operators and automation.

It targets the existing nsblast REST API and provides a script-friendly surface
for:

- tenant management
- zone management
- resource record management
- JSON export/import
- CSV-driven bulk RR changes
- basic auth login/logout helpers
- simple health/version checks

## Location

The CLI lives in the Rust workspace under:

- `cli/nsblastctl`

Build it with:

```sh
cd cli
cargo build -p nsblastctl
```

The binary is:

```sh
target/debug/nsblastctl
```

## Configuration

The CLI supports a per-user config file:

```text
~/.config/nsblast/nsblastctl.yaml
```

The config stores non-secret defaults such as:

- `server`
- `username`
- `timeout`
- `output`
- selected `profile`

Password handling follows this order:

1. CLI flags
2. keyring lookup
3. config plaintext password, if explicitly allowed
4. environment variables
5. interactive prompt

By default the config file must not be broader than `0600`.

## Examples

Check connectivity and auth:

```sh
nsblastctl --server http://127.0.0.1:8080 --username admin auth whoami
nsblastctl --server http://127.0.0.1:8080 --username admin health
```

List zones:

```sh
nsblastctl zone list
nsblastctl zone list --tenant 6b5f3ed6-3f55-4fa2-8c3b-9d6b8f8f0001
```

Create a zone:

```sh
nsblastctl zone create example.com --tenant 6b5f3ed6-3f55-4fa2-8c3b-9d6b8f8f0001
```

Add a record:

```sh
nsblastctl rr add example.com --name www --type A --ttl 300 --value 192.0.2.20
```

Bulk add from CSV:

```sh
nsblastctl rr bulk-add --csv ./records.csv --upsert
```

Export/import:

```sh
nsblastctl export zone example.com --file ./example.com.zone.json
nsblastctl import zone --file ./example.com.zone.json --conflict overwrite
```

## Bulk behavior

The current server does not expose native bulk endpoints for zones, tenants, or
resource records.

Because of that, `nsblastctl` falls back to grouped single-item operations:

- RR bulk add groups rows by fqdn and sends one write per fqdn
- RR bulk delete resolves rows to current RR state and applies one change at a time
- zone and tenant import orchestrate multiple existing API calls in dependency-safe order

This keeps the CLI compatible with the current server without requiring server
changes.

## Current limitations

- RR updates and deletes use a synthetic CLI-side RR identifier because the
  server does not currently return a stable RR id.
- Bulk operations are functional, but not as efficient as native server-side
  batch endpoints would be.
- Import/export uses the documented `nsblast.zone.v1` and `nsblast.tenant.v1`
  schemas implemented in the CLI.

See also:

- `specs/devops-cli.md`
- `bulk-suggestions.md`
