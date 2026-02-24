# DynIP Client Spec v1

## 1. Scope

This document defines the required behavior for the `nsblast-dynip` client.

Primary goals:
- Update DynIP records against the nsblast `/nic/update` endpoint.
- Use JSON payloads for both request and response.
- Support one-shot operation and daemon mode.
- Provide deterministic exit codes in one-shot mode.
- Prevent concurrent runs of the same config.

## 2. Runtime and Interface

- Implementation language: Rust.
- Binary name: `nsblast-dynip`.
- Transport: HTTPS by default.
- Authentication: HTTP Basic Auth.
- Protocol mode: JSON POST only (`POST /nic/update`).

## 3. Configuration

### 3.1 Config file location

- CLI option: `--config <path>`
- Default path: `~/.config/nsblast-dynip/config.yaml`

### 3.2 Required config keys

- `url`: Base nsblast URL (for example `https://dns.example.com`).
- `username`: Basic auth username.
- `password`: Basic auth password.
- `fqdn`: Hostname to update.

`fqdn` is required. The client must fail startup if it is missing.

### 3.3 Optional config keys

- `repeat_minutes`: Integer, default `15`. Used in daemon mode.
- `lock_file`: Override lock path.
- `tls_ca_file`: Optional custom CA bundle path.
- `timeout_seconds`: HTTP timeout per request, default `10`.
- `client_ref`: Optional correlation string sent in request.

### 3.4 Config precedence

- CLI flags override config file values.
- Config file values override built-in defaults.

### 3.5 File permission requirements

- Config file must not be accessible by group or others.
- Required file mode: `0600`.
- Recommended config directory mode: `0700`.
- If permissions are broader than allowed, the client must fail with exit code `5`.

## 4. CLI

- `--config <path>`: Path to YAML config.
- `--daemon`: Run continuously.
- `--repeat-minutes <n>`: Interval in minutes in daemon mode. Default `15`.
- `--on-changed <path>`: Execute script when effective IP changed.
- `--fqdn <hostname>`: Override configured hostname.

## 5. JSON Request and Response Behavior

### 5.1 Request format

- Endpoint path: `/nic/update`
- Method: `POST`
- Headers:
  - `Authorization: Basic ...`
  - `Content-Type: application/json`
  - `Accept: application/json`

Request body (default behavior):

```json
{
  "hostname": "office.example.com"
}
```

Optional request fields:
- `client_ref`
- `ip`
- `ipv4`
- `ipv6`

For v1 client behavior, send only `hostname` (and `client_ref` if configured) unless an explicit future option enables client-supplied IP values.

### 5.2 Success response handling

Expected success HTTP status: `200`.
Expected JSON fields include: `status`, `changed`, `effective_ip`, `hostname`.

Success mapping:
- `changed=true` or `status="good"` => one-shot exit code `2`
- `changed=false` or `status="nochg"` => one-shot exit code `0`

### 5.3 Error response handling

HTTP status mapping:
- `401` or `403` => `3`
- `429`, `500`, or `503` => `4`
- `400`, `404`, and other non-success HTTP responses => `5`
- Network or transport failures (DNS/TCP/TLS/timeout/no route) => `4`

JSON parse failures, missing required success fields on `200`, or unknown payload shape => `5`.

If HTTP status and JSON body indicate different outcomes, HTTP status takes precedence.

## 6. Operation Modes

### 6.1 One-shot mode (default)

- Perform one update on startup.
- Exit with code per Section 5.
- Logging targets:
  - stdout
  - systemd journal when available

### 6.2 Daemon mode

- Enabled by `--daemon`.
- Run update loop every `repeat_minutes`.
- Must add startup jitter in range `0..30` seconds.
- On failure, retry with exponential backoff capped at `repeat_minutes`.
- Do not exit on transient update failures.
- Exit only on fatal initialization/config errors or termination signal.
- Logging target:
  - systemd journal when available
  - fallback to stdout if systemd journal is unavailable

## 7. Concurrency and Locking

- The client must prevent parallel instances for the same config scope.
- Default lock path: `/tmp/nsblast-dynip.lock`.
- Lock acquisition behavior:
  - Non-blocking acquire at startup.
  - If lock is already held, log a violation and exit.
- Exit code on lock violation: `5`.
- Lock must be released on normal exit and signal-based shutdown.

## 8. `--on-changed` Script Contract

When update result is changed (`changed=true` or `status="good"`):
- Execute script path from `--on-changed`.
- Pass environment variables:
  - `NSBLAST_DYNIP_FQDN`
  - `NSBLAST_DYNIP_PREV_IP` (if known)
  - `NSBLAST_DYNIP_NEW_IP`
- Script timeout: `30` seconds.
- Script exit code does not change DynIP client exit code.
- Script failure must be logged.

## 9. Security Requirements

- TLS certificate verification is enabled by default.
- `--insecure` mode is not allowed in v1.
- Never log `password` or `Authorization` header.
- Logs may include URL host, FQDN, HTTP status, and response `status` value.

## 10. Exit Codes

Defined for one-shot mode and startup failures in daemon mode:
- `0`: Success, no change.
- `2`: Success, changed.
- `3`: Authentication/authorization failure (`401`/`403`).
- `4`: Network/connection/transient server failure (`429`/`500`/`503`).
- `5`: Other error (validation, config, lock, parse, unexpected server response).

Daemon runtime update failures do not force process exit.

## 11. README Requirement

Provide a README with:
- example config file,
- example one-shot invocation,
- example JSON request/response,
- example systemd unit and timer for periodic execution,
- note about config permission requirements.

## 12. Acceptance Criteria

Minimum tests for v1:
- One-shot: HTTP `200` + `changed=true` returns `2`.
- One-shot: HTTP `200` + `changed=false` returns `0`.
- One-shot: `401/403` returns `3`.
- One-shot: DNS/TLS/timeout failures return `4`.
- One-shot: `429/500/503` return `4`.
- One-shot: malformed/unexpected JSON response returns `5`.
- Startup fails when `fqdn` missing.
- Startup fails when config file mode is too open.
- Second concurrent process exits with lock violation.
- `--on-changed` runs only on changed update result.
- Daemon loop applies interval, jitter, and bounded backoff.
