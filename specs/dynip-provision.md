# 1. Overview

The DynIP feature enables tenants to provision dynamic DNS hostnames under a configured namespace (e.g., `dynip.nsblast.com`) and update their IP addresses via a high-frequency API endpoint.

The system is designed to:

1. Separate **provisioning (low frequency, tenant-authenticated)** from
   **updates (high frequency, capability-authenticated)**.
2. Make update authorization a **constant-time lookup**.
3. Avoid tenant permission evaluation in the update hot path.
4. Remain compatible with common DynDNS-style clients (`/nic/update`).

---

# 2. Terminology

| Term                | Description                                                        |
| ------------------- | ------------------------------------------------------------------ |
| DynIP Realm         | Configured DNS suffix for dynamic hosts (e.g. `dynip.nsblast.com`) |
| Provisioned Root    | `<root>.<realm>` owned by a tenant                                 |
| Dyn Host            | `<host>.<root>.<realm>` dynamic hostname                           |
| Update Token        | Secret capability allowing updates for exactly one Dyn Host        |
| Tenant              | nsblast account owner                                              |
| Permanent Variables | Server-wide persistent key-value storage                           |

---

# 3. Configuration

## 3.1 Required Permanent Variables

The following variables MUST be stored using the permanent variable storage:

| Variable                   | Description                           |
| -------------------------- | ------------------------------------- |
| `dynip.realm`              | DNS suffix (e.g. `dynip.nsblast.com`) |
| `dynip.enabled`            | Boolean                               |
| `dynip.max_hosts_per_root` | Default host limit (e.g. 5)           |
| `dynip.allow_txt`          | Boolean (default false)               |
| `dynip.default_ttl`        | Default TTL                           |
| `dynip.min_ttl`            | Minimum TTL                           |
| `dynip.max_ttl`            | Maximum TTL                           |

### Realm Immutability

Once DynIP provisioning exists, `dynip.realm` MUST NOT be changed without explicit migration.
Changing it invalidates all FQDNs and update credentials.

---

# 4. Data Model

To optimize update performance, the system uses two lookup paths.

## 4.1 Provisioning Tables

### provisioned_root_by_tenant

Key:

```
{tenantId}/{rootLower}
```

Value:

```
{
  createdAt,
  hostLimit,
  metadata
}
```

---

### dyn_host_by_root

Key:

```
{tenantId}/{rootLower}/{hostLower}
```

Value:

```
{
  fqdnLower,
  tokenId,
  passwordHash (optional),
  allowedRRTypes,
  ttl,
  lastUpdate,
  lastIp
}
```

---

## 4.2 Update Hot-Path Table

### dyn_host_by_token

Key:

```
{tokenId}
```

Value:

```
{
  fqdnLower,
  zoneId,
  tenantId,
  allowedRRTypes,
  ttlPolicy,
  passwordHash (if Basic auth mode),
  rateLimitState (optional cached)
}
```

The update endpoint MUST require only:

```
lookup(tokenId)
```

No tenant role evaluation is permitted in this path.

---

# 5. Permission Model

Permissions apply only to provisioning APIs.

| Permission      | Description                      |
| --------------- | -------------------------------- |
| DYNIP_PROVISION | Create/delete provisioned roots  |
| DYNIP_CREATE    | Create dyn-host under owned root |
| DYNIP_DELETE    | Delete dyn-host                  |
| DYNIP_LIST      | List dyn-host entries            |

Update endpoint authorization is capability-based (token/password), not tenant-role-based.

---

# 6. Provisioning API

Provisioning APIs require standard tenant authentication.

---

## 6.1 Create Provisioned Root

```
POST /dynip/{root}
```

Creates:

```
{root}.{dynip.realm}
```

Rules:

* `root` must be DNS-label safe
* Lowercased and normalized
* Must not already exist globally
* Tenant must have `DYNIP_PROVISION`

Response:

```
201 Created
{
  fqdn: "root.dynip.realm",
  hostLimit: 5
}
```

---

## 6.2 List Roots

```
GET /dynip/
```

Returns all provisioned roots owned by caller.

---

## 6.3 Delete Root

```
DELETE /dynip/{root}
```

Deletes:

* Root
* All dyn-host entries
* Associated DNS records

---

# 7. Dyn Host Management API

---

## 7.1 Create Dyn Host

```
POST /dynip/{root}/{host}
```

Creates:

```
{host}.{root}.{realm}
```

Generates:

* `tokenId` (random 128–256 bit value)
* Optional password (if Basic auth supported)

Response:

```
201 Created
{
  fqdn: "host.root.realm",
  token: "xxxxxxxx",
  updateUrl: "/dynip/update"
}
```

---

## 7.2 Delete Dyn Host

```
DELETE /dynip/{root}/{host}
```

Deletes:

* Host metadata
* Token entry
* DNS RRsets

---

## 7.3 List Dyn Hosts

```
GET /dynip/{root}/hosts
```

Returns host list with metadata.

---

# 8. Update Endpoint (Hot Path)

## 8.1 Endpoint

```
POST /dynip/update
```

OR compatibility:

```
GET /nic/update
```

---

## 8.2 Authentication

Supported methods:

### Bearer Token

```
Authorization: Bearer <token>
```

### Basic Auth

```
Authorization: Basic base64(fqdn:password)
```

Token lookup MUST be constant time.

---

## 8.3 Request Body (JSON mode)

```
{
  "fqdn": "host.root.realm",
  "ip": "auto" | "1.2.3.4"
}
```

If `ip` omitted or `"auto"`, server uses caller IP.

---

## 8.4 Update Flow

1. Extract token
2. Lookup `dyn_host_by_token`
3. If not found → 404
4. If auth fails → 401
5. Apply rate limiting
6. Determine IP
7. If IP unchanged → return NOCHANGE
8. Update DNS RRset
9. Persist `lastUpdate` and `lastIp`
10. Return OK

---

## 8.5 Response Codes

| HTTP | Meaning             |
| ---- | ------------------- |
| 200  | OK / NOCHANGE       |
| 401  | Invalid credentials |
| 404  | Unknown host/token  |
| 429  | Rate limited        |
| 400  | Invalid input       |

Compatibility mode may return DynDNS-style strings:

```
good 1.2.3.4
nochg 1.2.3.4
badauth
```

---

# 9. Rate Limiting

Update endpoint SHOULD implement:

* Per-token rate limiting
* Per-IP rate limiting
* Burst + sustained bucket model

Rate limiting MUST be memory-backed for performance.

---

# 10. DNS Record Behavior

Default:

* A and AAAA supported
* TTL bounded by `min_ttl` / `max_ttl`
* TXT only allowed if `dynip.allow_txt = true`

ACME Support Option:

Allow `_acme-challenge.host.root.realm` TXT updates scoped to host.

---

# 11. Security Considerations

* All tokens MUST be cryptographically random.
* Tokens MUST be stored hashed (recommended).
* Comparison MUST be constant-time.
* Hostnames MUST be normalized and punycode-converted before storage.
* Root deletion MUST invalidate all tokens.

---

# 12. Performance Characteristics

Update path complexity:

* One KV lookup
* One optional RRset write
* No tenant permission evaluation
* No root walking
* No recursive ownership checks

This allows the endpoint to tolerate heavy hammering from misconfigured clients.

---

# 13. Audit & Observability

Each dyn-host SHOULD track:

* `lastUpdate`
* `lastIp`
* `updateCount`
* Optional failure counters

Metrics should expose:

* Update rate
* Auth failures
* Rate limit events

---

# 14. Failure & Abuse Handling

The system SHOULD allow:

* Temporary token disable
* Token rotation
* Automatic disable after repeated abuse
* Blacklisting IP ranges

---

# 15. Future Extensions

* Token rotation endpoint
* HMAC-based update authentication
* Per-host TTL override
* Webhook on IP change
* IPv6-only host mode
* Signed update tokens (stateless validation option)

---

# 16. Design Summary

This design:

* Separates provisioning from update
* Uses capability-based authorization for hot path
* Avoids tenant permission checks during update
* Enables O(1) update validation
* Supports high-frequency dynamic DNS workloads

---

If you would like, I can next produce:

* A storage key layout optimized specifically for RocksDB
* A C++ interface sketch aligned with nsblast
* Or a minimal OpenAPI spec version of this document
