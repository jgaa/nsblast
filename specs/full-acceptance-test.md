# Full Acceptance Test Specification

## 1. Purpose

A long-running acceptance test that validates end-to-end behavior of a 3-node nsblast cluster with:

- bootstrap and cluster formation
- tenant/zone/RR provisioning at meaningful scale
- nsblast replication correctness and latency
- DynIP provisioning and continuous updates
- role-scoped user restrictions for selected tenants

The test is designed to run in about 1 hour on a normal 6 GB cloud VM.

## 2. Runtime and Tooling

### 2.1 Language and environment

- The test suite MUST be implemented in Python 3 with `venv`.
- The test repo root MUST include:
  - `tests/acceptance/requirements.txt`
  - `tests/acceptance/run_full_acceptance.sh`
  - `tests/acceptance/full_acceptance.py`
- `run_full_acceptance.sh` MUST:
  - create/use `.venv` (or `tests/acceptance/.venv`)
  - install dependencies with `pip install -r requirements.txt`
  - execute the Python test entrypoint

### 2.2 Containers

- Docker MUST be used to run nsblast instances.
- Default assumption: local image is already built via `./build-docker-image.sh`.
- The test MUST support optional override:
  - `--image-tag <tag>` (resolved as default nsblast repo + tag)
  - `--image <full-image-name>` (fully qualified image reference)
- Image selection precedence:
  1. `--image`
  2. `--image-tag`
  3. local default image produced by `./build-docker-image.sh`

### 2.3 Python dependencies (minimum)

- `pytest`
- `requests`
- `docker` (Docker SDK for Python) or CLI wrappers (`subprocess`)
- `dnspython` (optional but recommended for DNS-level verification)

## 3. Cluster Topology

- 3 nsblast containers on one Docker network:
  - `nsb-1` primary
  - `nsb-2` follower
  - `nsb-3` follower
- The primary must expose API ports to the test runner.
- All nodes must provide DNS service and Metrics to the test runner
- Persistent volumes per node MUST be isolated.
- Startup sequence:
  1. create network and volumes
  1.a Create shared secret file for the cluster
  1.b Create signed certs for the primary and followers (Certificate Generator commands)
  2. start primary
  3. bootstrap primary
  4. start followers, bootstrap them as followers and join replication
  5. wait for healthy state on all nodes

## 4. Configuration Requirements

- Test MUST configure permanent vars needed for DynIP and replication-sensitive behavior.
- DynIP MUST be enabled through permanent vars (not CLI flags), including:
  - realm
  - TTL min/default/max
  - max hosts per root
  - TXT allowance (as needed by test)
- Cluster role behavior MUST be configured through permanent vars and/or runtime APIs consistent with current implementation.

## 5. Data Generation Model

### 5.1 Scale

- Total zones: 2,000 to 4,000 (default 3,000)
- Total tenants: 200 to 500 (default 300)
- Zone distribution across tenants MUST be non-uniform.

### 5.2 Tenant profiles

Generate mixed tenant profiles:

- Small profile: few RRs (for example 1-5 A/AAAA/CNAME/MX/TXT combined)
- Standard web profile: `www`, apex, email (`MX`, `SPF`, `DKIM`-like TXT placeholders)
- Backend profile: service/internal naming (`api`, `db`, `cache`, `mq`)
- Mixed profile with private IP RRs (for example `staging1.dev.example.com` with RFC1918 targets)

At least 10% of generated zones MUST include private IP-based records.

### 5.3 Determinism

- Data generator MUST accept `--seed`.
- On failure, seed and generation summary MUST be printed for reproduction.

## 6. Authorization Scenarios

For a subset of tenants (default: 10 tenants), create additional users with constrained roles:

- user limited to a specific subdomain under one zone
- user limited to a single RR

The test MUST verify:

- allowed changes succeed
- out-of-scope changes fail with authorization error
- replication reflects only successful authorized changes

## 7. DynIP Scenarios

### 7.1 Provisioning

- Enable DynIP for 10 tenants.
- For each of those tenants, provision random host count in `[1,5]`.
- Persist mapping of tenant -> roots/hosts/tokens for update simulation.

### 7.2 Update simulation

- Simulate update waves every 30 seconds.
- Each wave updates all provisioned DynIP hosts (or a configured sample rate for runtime control).
- Effective IP inputs SHOULD include changing public IPv4 values; optional subset may include IPv6.

### 7.3 DynIP verification

For each update wave:

- primary acknowledges update success
- follower nodes converge to same record content
- replication lag per update MUST be measured

## 8. Replication Verification and SLOs

### 8.1 Core requirement

- Replication verification MUST fail if convergence exceeds 2 seconds by default.

### 8.2 Measurement

For each write operation class (zone/RR updates, role-constrained changes, DynIP updates):

1. capture write commit time on primary
2. poll/read followers until matching state is observed
3. record per-operation convergence latency

### 8.3 Default pass/fail thresholds

- hard fail if any required convergence exceeds `2.0s` (`--max-repl-lag-sec`, default `2.0`)
- hard fail on mismatched final state across nodes
- hard fail on replication stream interruptions not auto-recovered within configurable grace

## 9. Duration and Workload Shaping

Target wall-clock runtime on 6 GB VM: ~60 minutes.

Implementation MUST include knobs to keep this target:

- `--zones` (default 3000)
- `--tenants` (default 300)
- `--dynip-tenants` (default 10)
- `--wave-interval-sec` (default 30)
- `--duration-min` (default 60)
- `--parallelism` for provisioning operations

Recommended phase budget:

- cluster bootstrap and readiness: <= 10 min
- bulk tenant/zone/RR provisioning + baseline replication checks: <= 20 min
- auth restriction scenarios: <= 10 min
- DynIP wave simulation with replication checks: ~20 min

## 10. Execution Flow

1. Parse args and resolve image source.
2. Create Docker network/volumes; start 3-node cluster.
3. Bootstrap primary and join followers.
4. Configure permanent vars for DynIP and test defaults.
5. Generate tenants/zones/RRs and apply dataset.
6. Verify full replication baseline on followers.
7. Apply role-restricted user scenarios and verify auth + replication.
8. Provision DynIP for 10 tenants with 1..5 hosts each.
9. Run 30-second update waves for configured duration window.
10. Aggregate metrics/results and emit summary.
11. Tear down containers unless `--keep-on-fail` is set.

## 11. Reporting and Artifacts

The run MUST output:

- test parameters (including seed and image resolution)
- counts: tenants, zones, RRs, DynIP hosts, update waves, total updates
- replication latency stats per scenario:
  - min/p50/p95/p99/max
- pass/fail summary with first failing assertion and context

On failure, persist artifacts under `tests/acceptance/artifacts/<timestamp>/`:

- structured JSON report
- sampled request/response logs
- container logs (`docker logs`) for all nodes
- seed and generated workload summary

## 12. CLI Contract

Minimum CLI options for `full_acceptance.py`:

- `--image-tag`
- `--image`
- `--seed`
- `--zones`
- `--tenants`
- `--dynip-tenants`
- `--duration-min`
- `--wave-interval-sec`
- `--max-repl-lag-sec` (default `2.0`)
- `--keep-on-fail`

## 13. Non-Goals

- Cross-region/network-chaos testing.
- Kubernetes orchestration.
- Benchmark-grade throughput certification.

## 14. Acceptance Criteria

The feature is accepted when:

1. A single command creates venv, runs the full test, and produces structured artifacts.
2. 3-node Docker cluster is bootstrapped automatically.
3. Dataset of thousands of zones and hundreds of tenants is provisioned with mixed RR complexity.
4. Private-IP RR cases are present and replicated correctly.
5. Role-restricted users are validated for both allow/deny paths.
6. DynIP is provisioned for 10 tenants with randomized 1..5 hosts.
7. DynIP updates run every 30 seconds and replication is verified per wave.
8. Default run fails if replication convergence exceeds 2 seconds.
9. Default runtime is approximately 1 hour on a 6 GB VM.
