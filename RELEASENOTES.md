# nsblast Alpha Release

This is an alpha release and is not considered stable.

## Container Images

Container image publishing has moved from Docker Hub to GitHub Container Registry (GHCR).

Official image repository:
`ghcr.io/jgaa/nsblast`

## Summary Of Recent Changes

- Added explicit command-based bootstrap flow. New databases are now initialized
  with `nsblast bootstrap`, and cluster role is persisted as a permanent
  variable instead of being a normal runtime flag.
- Added permanent variables support, including vars CLI and REST management, and
  moved the first runtime policy set into vars, especially `cluster_role` and
  DynIP-related settings.
- Added DynIP support end to end: provisioning APIs, capability-token auth,
  Bearer-token update flow, tenant permissions, and the `nsblast-dynip` Rust
  client for Linux.
- Added DynIP management in the UI, including root/host listing, provisioning,
  deletion, availability checks, and permission-gated access.
- Added real acceptance coverage:
  - full cluster acceptance with bootstrap, replication, RBAC, and sustained
    DynIP load
  - DynIP CLI end-to-end acceptance against a standalone server container
- Added the new `nsblastctl` DevOps CLI for tenant, zone, RR, import/export,
  and bulk-oriented operator workflows, with single-operation fallback where the
  server does not yet provide bulk APIs.
- Fixed a replication locking issue and validated the result with both focused
  replication tests and full acceptance reruns.
- Added disaster-recovery support for rebuilding a primary from a replica
  database.
- Improved operations and observability with log viewing in the UI/API and
  broader manual updates in `nsblast-book`.
- Addressed recent security and dependency issues in test and UI tooling,
  including `requests`, `dnspython`, and `esbuild`, and tightened the related
  GitHub Actions workflows.
