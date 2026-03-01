# Bulk API Suggestions

These are rough API additions that would make `nsblastctl` materially faster and
simpler for bulk operations.

## RR operations

- `POST /api/v1/rr:bulk-upsert`
  - Accept an array of `{fqdn, entry}` objects.
  - Return per-item status with created/updated/error details.
  - This removes the current need to group CSV rows client-side and loop one
    request per fqdn.

- `POST /api/v1/rr:bulk-delete`
  - Accept either synthetic tuples like `{fqdn, type, value}` or a future
    server-native RR id.
  - Return per-item results.
  - This avoids current read-modify-write fallback on the client.

- Add stable RR identifiers to verbose RR listing and `GET /rr/{fqdn}` results.
  - A real server-side RR id would let the CLI delete/update one RR without
    reconstructing the entire fqdn entry.

## Zone operations

- `PUT /api/v1/zone/{zone}`
  - Replace zone metadata and apex state directly.
  - Current client import has to emulate overwrite by mixing zone creation with
    `rr` writes.

- `POST /api/v1/zone:bulk-create`
  - Accept an array of zones with tenant assignments.
  - Useful for tenant bootstrap and large imports.

- `POST /api/v1/zone:bulk-delete`
  - Accept a list of zones and return per-zone status.

## Tenant operations

- `POST /api/v1/tenant:bulk-upsert`
  - Accept tenant objects with optional access metadata.
  - Useful for migration/bootstrap workflows.

- `POST /api/v1/tenant/{tenant}/import`
  - Server-managed tenant import endpoint that applies users, roles, zones, and
    records in dependency-safe order.
  - That would remove a lot of orchestration from the CLI.

## Import/export support

- `GET /api/v1/zone/{zone}/export`
  - Return a stable export schema with tenant, SOA, apex NS, and all records.

- `GET /api/v1/tenant/{tenant}/export`
  - Return tenant metadata, access metadata, zones, and records in one document.

- `POST /api/v1/import/zone`
  - Accept the exported zone schema plus `conflict=fail|overwrite|skip`.

- `POST /api/v1/import/tenant`
  - Accept the exported tenant schema plus `conflict=fail|overwrite|skip`.

## Batch execution behavior

- Support partial-success batch responses with:
  - `processed`
  - `created`
  - `updated`
  - `deleted`
  - `skipped`
  - `failed`
  - per-item `errors`

- Support `dry_run=true` on bulk endpoints.
  - The client currently has to simulate dry-run locally because the API does
    not expose a write validation mode.

- Support optional `wait=<seconds>` and aggregate replication status in batch
  responses.
  - This would help operators use bulk writes safely in replicated setups.
