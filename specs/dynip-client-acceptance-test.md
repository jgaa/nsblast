# DynIP Client Acceptance Test Specification

## 1. Purpose

This document defines an end-to-end acceptance test for the `nsblast-dynip` CLI.

The test MUST validate the real client binary against a real standalone `nsblast`
server container, without mocking the DynIP update path.

Primary goals:
- prove the CLI works against the primary DynIP update endpoint: `POST /api/v1/dynip/update`
- prove provisioning and update setup can be established end to end
- verify valid client behavior and representative invalid/failure behavior
- keep server compatibility behavior unchanged for legacy DynIP clients and routers

This test is for the CLI integration surface, not for multi-node replication.

---

## 2. Scope

The test covers:
- starting one standalone `nsblast` container
- bootstrapping the server
- creating a tenant with DynIP provisioning permissions
- enabling DynIP through permanent vars
- creating a DynIP root and DynIP host
- obtaining the capability token for that host
- writing a CLI config file using Bearer-token auth semantics
- running the CLI binary in one-shot mode
- validating success, no-change, auth failure, config failure, and selected protocol failure cases

The test MUST use the real CLI binary and real HTTP requests to the server.

The test MUST NOT modify server behavior or remove backward compatibility for
legacy DynIP clients such as routers using `/nic/update`.

---

## 3. Runtime and Tooling

### 3.1 Languages and locations

- The acceptance harness SHOULD be implemented in Python 3.
- The CLI under test is the Rust binary `nsblast-dynip`.
- Test assets SHOULD live under `tests/acceptance/` or `cli/dynip-client/tests/acceptance/`.

Recommended files:
- `tests/acceptance/run_dynip_client_acceptance.sh`
- `tests/acceptance/dynip_client_acceptance.py`

### 3.2 Prerequisites

- Docker available locally
- a built `nsblast` server image
- Rust toolchain available for building the CLI

The harness MUST support either:
1. using an already-built CLI binary, or
2. building the CLI binary as part of the test

### 3.3 Server image selection

The harness MUST support:
- `--image <full-image-ref>`
- `--image-tag <tag>`

Image resolution precedence:
1. `--image`
2. `--image-tag`
3. default local image

### 3.4 CLI binary selection

The harness MUST support:
- `--cli-bin <path>` to use an existing built binary

If `--cli-bin` is absent, the harness SHOULD build the CLI from the `cli/`
workspace before running the scenarios.

---

## 4. Topology

Single-node topology:
- one Docker network
- one `nsblast` server container
- one test runner process on the host

The container MUST expose to the host:
- HTTP API port
- DNS port if DNS-level verification is used

Persistent storage for the server container MUST be isolated per run.

---

## 5. Server Setup Flow

For each test run, the harness MUST:

1. create an isolated Docker network
2. generate any required bootstrap/cert/auth material
3. start one `nsblast` container in standalone mode
4. wait for API readiness
5. bootstrap the server
6. authenticate as system admin
7. enable DynIP through permanent vars
8. create a dedicated tenant for the DynIP CLI test
9. create a tenant admin user with permissions sufficient for:
   - `USE_API`
   - `DYNIP_PROVISION`
   - `DYNIP_CREATE`
   - `DYNIP_DELETE`
   - `DYNIP_LIST`
10. create the DynIP realm zone if required by the current server behavior
11. provision a DynIP root and host
12. capture the returned DynIP capability token

The harness MUST fail fast if any bootstrap or provisioning step fails.

---

## 6. DynIP and Tenant Setup Requirements

### 6.1 Permanent vars

The harness MUST configure at least:
- `dynip_enabled = true`
- `dynip_realm = <test realm>`
- `dynip_min_ttl`
- `dynip_default_ttl`
- `dynip_max_ttl`
- `dynip_max_hosts_per_root`

Recommended defaults:
- realm: `dynip.acceptance.test`
- min TTL: `60`
- default TTL: `300`
- max TTL: `1800`
- max hosts per root: `8`

### 6.2 Test objects

The acceptance test SHOULD create:
- tenant: `dynip-cli-tenant`
- root: `office`
- host: `router`
- resulting fqdn: `router.office.<realm>`

The created host token MUST be used as the CLI `token` config value.

---

## 7. CLI Config Under Test

The test harness MUST write a real config file for the CLI with mode `0600`.

Default valid config:

```yaml
url: "http://127.0.0.1:<api-port>"
token: "<dynip capability token>"
fqdn: "router.office.dynip.acceptance.test"
timeout_seconds: 10
```

The test MUST execute the real CLI binary with `--config <path>`.

---

## 8. Core Success Scenarios

### 8.1 Successful update

Given:
- valid `url`
- valid `token`
- valid `fqdn`
- server ready

When:
- the CLI runs once

Then:
- it MUST call `POST /api/v1/dynip/update`
- it MUST authenticate with `Authorization: Bearer <token>`
- it MUST send JSON containing `fqdn`
- for current server builds, the harness MUST pass one explicit `--ip` value so the request body also contains `ip`
- it MUST exit with code `2` on `status="good"` and `changed=true`
- the server response MUST contain the effective IP and `fqdn`
- the server state for the host MUST reflect the new IP

### 8.2 No-change update

Given:
- the same host already points at the same effective IP

When:
- the CLI runs again

Then:
- it MUST exit with code `0`
- the server response MUST indicate `status="nochg"` and `changed=false`

### 8.3 Response compatibility alias

If the server returns `hostname` instead of `fqdn`, the CLI MAY continue to
accept that response for compatibility.

This MAY be validated with a small HTTP fixture test, but it is not the primary
end-to-end scenario because the current server returns `fqdn`.

---

## 9. Invalid and Failure Scenarios

The acceptance suite MUST cover the following failure classes end to end where
possible.

### 9.1 Invalid token

Config:
- valid `url`
- invalid `token`
- valid `fqdn`

Expected:
- CLI exits `3`
- server returns `401` or `403`

### 9.2 Scope mismatch token

Setup:
- provision two DynIP hosts and tokens

Config:
- token for host A
- fqdn for host B

Expected:
- CLI exits non-success
- preferred server result is `404`, `401`, or `403` according to current policy
- test MUST assert only the mapped client exit code unless the server policy is
  already stabilized more tightly

Default expected CLI code:
- `5` for `404`
- `3` for `401/403`

### 9.3 Missing token in config

Config:
- omit `token`

Expected:
- CLI fails startup before sending request
- exit code `5`

### 9.4 Missing fqdn in config

Config:
- omit `fqdn`

Expected:
- CLI fails startup before sending request
- exit code `5`

### 9.5 Config file permissions too broad

Config:
- valid file contents
- mode `0644`

Expected:
- CLI rejects config
- exit code `5`

### 9.6 DynIP disabled

Setup:
- after provisioning, disable DynIP via permanent vars

Expected:
- CLI exits non-success
- server returns `403`
- mapped client exit code `3`

### 9.7 Unknown host / deleted host

Setup:
- provision host and token
- delete the DynIP host or root before running the CLI

Expected:
- server returns `404` or auth failure according to implementation policy
- CLI exits non-success
- expected code:
  - `5` for `404`
  - `3` for `401/403`

### 9.8 Server unavailable

Setup:
- stop the container before invoking the CLI

Expected:
- CLI exits `4`

### 9.9 Server transient failure class

If practical, the harness SHOULD validate at least one transient failure class:
- `429`
- `500`
- `503`

Because the standalone server does not naturally expose all of those on demand,
this MAY be covered by a small local HTTP fixture instead of the real container.

This is secondary to the real end-to-end server path.

---

## 10. Optional Extended Scenarios

The following are recommended but not required for v1:

- run CLI in `--daemon` mode briefly and validate repeated success/no-change loop
- validate `--on-changed` hook execution on changed update only
- validate custom CA file flow for HTTPS container setup
- validate deprecated config alias `password:` still works as token input

If the deprecated alias is tested, it MUST be clearly marked as a compatibility
case, not the primary documented contract.

---

## 11. Execution Flow

Recommended order:

1. resolve server image and CLI binary
2. create temp work directory and artifacts directory
3. create Docker network and server runtime dirs
4. start standalone server container
5. wait for API readiness
6. bootstrap/authenticate admin
7. configure DynIP permanent vars
8. create tenant, user, and realm zone
9. provision DynIP root and host
10. write valid CLI config
11. run success scenario
12. run no-change scenario
13. run invalid token scenario
14. run scope mismatch scenario
15. run missing token/fqdn/permission scenarios
16. run disabled/unknown-host/server-down scenarios
17. collect logs and artifacts
18. tear down unless `--keep-on-fail`

---

## 12. Assertions and Exit Codes

Primary assertions:
- exit code matches spec in [dynip-client.md](/home/jgaa/src/nsblast/specs/dynip-client.md)
- server HTTP status matches expected class
- response JSON shape is valid for success cases
- server-side record content changes only for valid operations

Expected client exit code mapping:
- `0`: success, no change
- `2`: success, changed
- `3`: authentication/authorization failure (`401`/`403`)
- `4`: network/transient failure
- `5`: config/protocol/other failure

---

## 13. Artifacts

On every run, the harness SHOULD persist:
- test parameters
- resolved image and CLI binary path
- generated server endpoints
- tenant/root/host/fqdn identifiers
- CLI config used for each scenario, with token redacted
- per-scenario:
  - command line
  - exit code
  - stdout/stderr
  - server response summary if captured

On failure, the harness MUST persist:
- server container logs
- structured JSON report
- server bootstrap/provisioning transcript
- any temporary config files with secrets redacted

Recommended artifact path:
- `tests/acceptance/artifacts/dynip-client-acceptance-<timestamp>/`

---

## 14. CLI Contract for the Harness

Minimum harness options:
- `--image`
- `--image-tag`
- `--cli-bin`
- `--keep-on-fail`
- `--artifact-root`
- `--server-log-level`
- `--seed` if randomization is added

Optional:
- `--https`
- `--tls-ca-file`
- `--admin-password`
- `--realm`

---

## 15. Non-Goals

- multi-node replication testing
- router interoperability certification
- performance benchmarking
- exhaustive TLS matrix testing
- server backward-compatibility behavior changes

---

## 16. Acceptance Criteria

This feature is accepted when:

1. one command can start a standalone server container and execute the real CLI end to end
2. the harness bootstraps the server and provisions DynIP state automatically
3. the CLI succeeds with valid token-based config against `/api/v1/dynip/update`
4. the CLI returns `2` on first successful change and `0` on no-change rerun
5. invalid token and disabled DynIP cases are verified
6. startup/config validation failures are verified
7. server-unavailable behavior is verified
8. artifacts and logs are preserved on failure
9. the spec does not require any server-side removal of legacy DynIP compatibility behavior
