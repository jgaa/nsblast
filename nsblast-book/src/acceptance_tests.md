# Acceptance Tests

nsBLAST currently has two end-to-end acceptance harnesses under `tests/acceptance/`.
They cover different risk surfaces and are intended to be run separately.

## Full Cluster Acceptance

The full acceptance test is defined in [`specs/full-acceptance-test.md`](../../specs/full-acceptance-test.md)
and executed by `tests/acceptance/run_full_acceptance.sh`.

It starts a three-node cluster:

- `nsb-1` as primary
- `nsb-2` as follower
- `nsb-3` as follower

The harness bootstraps the cluster, configures permanent vars, provisions a large
mixed dataset, verifies replication convergence, exercises tenant-scoped
authorization, and runs repeated DynIP update waves.

This is the main regression test for:

- cluster bootstrap and follower join
- RocksDB-backed provisioning at scale
- replication correctness and lag
- RBAC enforcement on real API operations
- DynIP behavior under sustained write load

Artifacts are written under `tests/acceptance/artifacts/full-acceptance-*/` and
include a structured report, sampled metrics, and per-node Docker logs.

Run it with:

```sh
tests/acceptance/run_full_acceptance.sh
```

Use this harness when validating replication, large provisioning changes, or
server-side behavior that may only break under load.

## DynIP CLI Acceptance

The DynIP CLI acceptance test is defined in
[`specs/dynip-client-acceptance-test.md`](../../specs/dynip-client-acceptance-test.md)
and executed by `tests/acceptance/run_dynip_client_acceptance.sh`.

It starts one standalone server container, bootstraps it, enables DynIP through
permanent vars, provisions a tenant plus DynIP roots and hosts, then runs the
real `nsblast-dynip` binary against the primary update endpoint
`/api/v1/dynip/update`.

This harness validates:

- CLI config handling
- Bearer-token auth for DynIP updates
- success and no-change update flows
- invalid token and scope mismatch handling
- DynIP-disabled and server-unavailable behavior
- end-to-end provisioning needed by the CLI

Artifacts are written under
`tests/acceptance/artifacts/dynip-client-acceptance-*/` and include a report,
request samples, a transcript, and server logs.

Run it with:

```sh
tests/acceptance/run_dynip_client_acceptance.sh
```

Use this harness when changing the DynIP client, the DynIP provisioning APIs, or
the standalone server path used by the CLI.

## Which One To Run

Run the full cluster acceptance test when the change touches replication,
concurrency, authorization, or bulk provisioning.

Run the DynIP CLI acceptance test when the change touches the Rust CLI, DynIP
request shaping, DynIP auth, or the standalone update path.

For replication or storage changes, run both.
