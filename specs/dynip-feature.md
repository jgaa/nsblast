## DynIP Update API Spec v2

This document defines the DynIP update endpoint behavior and supersedes the older permission-based update model.

It is aligned with:
- `specs/dynip-provision.md` (capability-based hot path)
- `specs/permanent-vars-feature.md` (DynIP configuration stored as permanent variables)

---

## 1. Goals

1. Keep update authorization O(1) and independent of tenant role evaluation.
2. Support high-frequency updates from routers and DynDNS-style clients.
3. Preserve compatibility with legacy DynDNS clients while making JSON POST the primary interface.
4. Enforce runtime behavior from permanent variables, not command-line flags.

---

## 2. Endpoints

Primary update endpoint:
- `POST /dynip/update` (JSON)

Compatibility endpoint:
- `GET /nic/update` (DynDNS-style, optional)

`POST /nic/update` may be kept as an alias for backward compatibility, but `POST /dynip/update` is normative.

---

## 3. Authentication and Authorization

## 3.1 Authentication modes

Supported:
- `Authorization: Bearer <token>`
- `Authorization: Basic base64(<fqdn>:<password>)`

## 3.2 Authorization model (normative)

Updates are capability-based.

The server MUST:
1. Resolve credential to a dyn-host capability (`tokenId`/password lookup).
2. Retrieve dyn-host metadata via constant-time key lookup (e.g. `dyn_host_by_token`).
3. Authorize only against capability scope (exact host), not tenant-role permissions.

The update hot path MUST NOT evaluate tenant permissions such as `DYNIP_PROVISION`, `DYNIP_CREATE`, `CREATE_RR`, or `UPDATE_RR`.
Those permissions apply only to provisioning APIs.

If credential is unknown or invalid:
- JSON mode: `401 Unauthorized` (or `404` if you intentionally hide existence)
- Legacy mode: `badauth`

---

## 4. Request Formats

## 4.1 JSON update request (primary)

`POST /dynip/update`

Headers:
- `Authorization: Bearer ...` or `Authorization: Basic ...`
- `Content-Type: application/json`

Body:

```json
{
  "fqdn": "host.root.dynip.nsblast.com",
  "ip": "auto"
}
```

Rules:
- `fqdn` is required.
- `ip` is optional.
- If `ip` omitted or `"auto"`, server uses peer IP.
- If `ip` is explicit, it must be a valid IPv4/IPv6.
- Capability must allow updates for that exact `fqdn`.

## 4.2 Legacy DynDNS request (compatibility)

`GET /nic/update?hostname=<fqdn>[&myip=<ip>]`

Compatibility params (`system`, `wildcard`, `mx`, `backmx`) may be accepted and ignored.

Rules:
- `hostname` maps to a single dyn-host FQDN.
- `myip` omitted => peer IP.

---

## 5. Effective IP and Record Update Rules

Given:
- `peer_ip` from the TCP connection
- `req_ip` from request (`ip` or `myip`, if present)

Effective IP:
1. If `req_ip` absent or `"auto"` => `effective_ip = peer_ip`
2. Else `effective_ip = req_ip`

Validation:
- `effective_ip` must be syntactically valid IPv4 or IPv6.
- Private/reserved IP acceptance follows variable policy.

Record selection:
- IPv4 => update `A`
- IPv6 => update `AAAA`

Behavior:
- Same value => `nochg`
- Different value => update and return `good`

The server SHOULD update dyn-host metadata (`lastUpdate`, `lastIp`, counters) after successful updates.

---

## 6. Response Model

## 6.1 Legacy response tokens (`text/plain`)

Success:
- `good <ip>`
- `nochg <ip>`

Auth/input:
- `badauth`
- `notfqdn`
- `nohost`
- `badip`
- `numhost`

Service:
- `911`

For maximum compatibility, legacy mode may return `200` with token body even on logical errors.

## 6.2 JSON response

Success/no-change: `200 OK`

Error statuses:
- `400` malformed payload / invalid IP
- `401` bad credentials
- `404` unknown host/capability (optional concealment strategy)
- `429` rate limited
- `500`/`503` server errors

Example success body:

```json
{
  "status": "good",
  "changed": true,
  "fqdn": "host.root.dynip.nsblast.com",
  "effective_ip": "203.0.113.10",
  "peer_ip": "203.0.113.10",
  "record_type": "A",
  "ts": "2026-02-27T12:00:00Z"
}
```

---

## 7. Configuration via Permanent Variables

DynIP update behavior MUST be controlled by permanent variables (`VarsSnapshot`) and runtime reload logic, not command-line options.

Core variables from `specs/permanent-vars-feature.md`:
- `dynip_enabled`
- `dynip_realm`
- `dynip_max_hosts_per_root`
- `dynip_default_ttl`
- `dynip_min_ttl`
- `dynip_max_ttl`
- `dynip_allow_txt`

Operational update-policy variables (currently represented in runtime config and should be formalized as PVs):
- `dynip_enable_get`
- `dynip_enable_post_json`
- `dynip_allow_partial_multi_host`
- `dynip_max_hosts_per_request`
- `dynip_require_user_agent`
- `dynip_allow_private_ips`
- `dynip_rate_limit` (or split into typed fields)

Normative behavior:
- If `dynip_enabled = false`, both provisioning and update endpoints MUST reject requests.
- `dynip_realm` constrains allowable DynIP hostnames.
- TTL used for create/update MUST satisfy `dynip_min_ttl <= ttl <= dynip_max_ttl`, defaulting to `dynip_default_ttl`.

---

## 8. Security and Performance Requirements

1. Token/password verification SHOULD be constant-time.
2. Tokens SHOULD be cryptographically random and stored hashed.
3. Update path MUST remain O(1) lookup + optional DNS write.
4. No tenant permission graph traversal in hot path.
5. Enforce per-token/per-IP rate limiting.
6. Return `Cache-Control: no-store` for compatibility endpoint responses.

---

## 9. Compatibility Notes

1. Legacy routers often depend on DynDNS token strings and may ignore HTTP status codes.
2. Keep `GET /nic/update` optional and explicitly controllable.
3. Prefer JSON response contracts for new clients.

---

## 10. Required Changes TODO

- [ ] Remove/update outdated text that ties update authorization to tenant ownership + `DYNIP` permission checks in update path.
- [ ] Make `POST /dynip/update` the documented primary update endpoint; keep `GET /nic/update` as compatibility mode.
- [ ] Align request schema terminology with provisioning spec (`fqdn`, capability credential) and remove legacy-only assumptions.
- [ ] Define and document credential-to-host lookup contract (`dyn_host_by_token` or equivalent) as normative for update path.
- [ ] Standardize error semantics between JSON and legacy paths (`401`/`404` policy, `badauth`, `nohost`, `badip`).
- [ ] Clarify behavior for unknown/disabled endpoints via policy (`dynip_enable_get`, `dynip_enable_post_json`).
- [ ] Replace references to command-line DynIP config flags with permanent variable based configuration.
- [ ] Add missing permanent variables for update-policy controls (currently runtime config fields) to `VarsSnapshot` or document why they stay outside PV.
- [ ] Ensure TTL handling references `dynip_default_ttl`, `dynip_min_ttl`, and `dynip_max_ttl` from permanent variables.
- [ ] Confirm that when `dynip_enabled=false`, both `/dynip/*` provisioning and update endpoints fail closed.
- [ ] Add tests covering capability-based auth hot path (no tenant permission check), legacy compatibility tokens, and PV-driven behavior.
