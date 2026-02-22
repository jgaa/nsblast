## DynIP Update API Spec v1

### 1. Goals

1. **Compatibility mode (DynDNS-style):** support legacy clients/routers that call `GET /nic/update` with query parameters and expect **plain-text** response tokens (`good`, `nochg`, `badauth`, etc.).
2. **Modern mode (REST/JSON):** support `POST /nic/update` with `Content-Type: application/json` returning **JSON** with explicit fields.
3. **Policy control:** allow the service operator to **disable GET** and require POST (or allow both).

---

## 2. Endpoint

### 2.1 Update endpoint

`/nic/update`

Supported methods:

* `GET` (legacy compatibility; optional, configurable)
* `POST` (recommended; supports JSON body)

---

## 3. Authentication & Authorization

### 3.1 Authentication

* **HTTP Basic Authentication**: `Authorization: Basic base64(username:password)`

### 3.2 Authorization logic (normative)

For a given requested FQDN `hostname`:

1. **Ownership + rule check**
   The tenant must own the requested FQDN, and the user must have a rule that allows access to it (use your normal lookup/validation logic).

2. **DynIP permission path**
   If the user has **DYNIP permission** for the requested FQDN:

   * If the request **does not specify an IP**, use the **peer IP** from the TCP connection (IPv4/IPv6 depending on the connection).
   * If the request **specifies an IP**, it is allowed **only if** it equals the peer IP (exact match). Otherwise reject.
     Rationale: prevents spoofing and aligns with “untrusted environments”.

3. **Fallback to ordinary RR update rules**
   If the user does **not** have DYNIP permission for the FQDN, fall back to ordinary CREATE_RR/UPDATE_RR rules:

   * If request specifies an IP: use it.
   * If request does not specify an IP: use peer IP.

---

## 4. Determining the effective IP (normative)

Let:

* `peer_ip` = remote IP of the TCP connection (v4 or v6)
* `req_ip` = IP specified by client (if any)

Compute `effective_ip`:

1. If `req_ip` is absent → `effective_ip = peer_ip`
2. If `req_ip` is present:

   * If user has DYNIP permission for hostname: require `req_ip == peer_ip`, else error `badip`
   * Else (fallback mode): accept `req_ip` if valid IPv4/IPv6 per validation rules

### 4.1 Validation rules

* `hostname` must be a fully qualified domain name the tenant owns.
* `effective_ip` must parse as valid IPv4 or IPv6.
* Optional (recommended): reject private/reserved ranges unless explicitly allowed by config.

---

## 5. Request formats

### 5.1 Legacy GET (DynDNS compatible)

#### Request

`GET /nic/update?hostname=<fqdn>[&myip=<ip>][&system=dyndns][&wildcard=NOCHG][&mx=NOCHG][&backmx=NOCHG]`

Supported query parameters:

* `hostname` (**required**)

  * Either a single FQDN or a comma-separated list (see 5.3).
* `myip` (optional)

  * If omitted, server uses `peer_ip`.
* Other parameters (`system`, `wildcard`, `mx`, `backmx`) are accepted and ignored for compatibility.

#### Expected headers

* Required:

  * `Authorization: Basic …` (unless you later add token mode)
  * `Host` (HTTP/1.1)
* Recommended:

  * `User-Agent` (log + abuse detection)
* Response headers:

  * `Cache-Control: no-store`
  * `Content-Type: text/plain; charset=utf-8`

### 5.2 Modern JSON POST

#### Request

`POST /nic/update`

Headers:

* `Content-Type: application/json`
* `Authorization: Basic …`

Body (JSON):

```json
{
  "hostname": "office.tlv.dynip.nsblast.com",
  "ip": "203.0.113.10",
  "ipv4": "203.0.113.10",
  "ipv6": "2001:db8::10",
  "client_ref": "optional string for client correlation"
}
```

Rules:

* Exactly one of the following must be provided:

  * `ip` (single address, v4 or v6)
  * `ipv4` and/or `ipv6`
* If none provided → server uses `peer_ip` (and updates only that family)
* If both `ipv4` and `ipv6` provided → server may update both records (see 6.4)

Response:

* `Content-Type: application/json; charset=utf-8`
* `Cache-Control: no-store`

### 5.3 Multiple hostnames

Support (optional but very useful for routers):

* `hostname=a.example.com,b.example.com`
* For JSON: `"hostname": ["a.example.com", "b.example.com"]`

If any hostname fails authz/validation:

* Legacy mode: return a single failure code (see 7.2) and do not partially update unless config allows partial success.
* JSON mode: return per-host results (recommended).

---

## 6. Update behavior

### 6.1 Record type selection

* If `effective_ip` is IPv4 → update/create `A` record
* If `effective_ip` is IPv6 → update/create `AAAA` record

### 6.2 Create vs update

* If record exists and value differs → update, `changed=true`
* If record exists and value same → no change, `changed=false`
* If record does not exist:

  * If policy allows auto-create for DynIP hostnames → create, `changed=true`
  * Otherwise return `nohost` (legacy) / `HOST_NOT_FOUND` (JSON)

### 6.3 TTL (recommended)

* Use a short default TTL (e.g., 60–300s) configurable per DynIP zone/record.
* When updating, preserve existing TTL unless policy says otherwise.

### 6.4 Dual-stack updates (JSON)

If JSON provides both `ipv4` and `ipv6`, update both families independently and return two result entries.

---

## 7. Responses

### 7.1 Legacy (text/plain) response tokens

Return **one line** of text, optionally with the IP appended.

Success:

* `good <ip>` — updated successfully
* `nochg <ip>` — already set to that IP

Auth/permission:

* `badauth` — bad credentials
* `notfqdn` — hostname malformed
* `nohost` — hostname not found / not provisioned for tenant
* `!donator` — (optional) if you ever gate features; otherwise don’t use

Input errors:

* `badip` — invalid IP, or (DYNIP permission) provided IP != peer IP
* `numhost` — too many hosts in one request (if you set a limit)

Server errors:

* `911` — transient server error (client should retry later)

**HTTP status codes for legacy mode**

* For maximal router compatibility: always `200 OK` and encode errors in the text token.
* Optionally (config): use HTTP 4xx/5xx while still returning a token body.

### 7.2 Modern JSON response

HTTP status:

* `200 OK` for success and no-change
* `400 Bad Request` for malformed payload
* `401 Unauthorized` for missing/invalid basic auth
* `403 Forbidden` for authz failures
* `404 Not Found` when hostname not provisioned (if you don’t auto-create)
* `429 Too Many Requests` for rate limiting
* `500/503` for server issues

Body:

```json
{
  "status": "good",
  "changed": true,
  "effective_ip": "203.0.113.10",
  "peer_ip": "203.0.113.10",
  "hostname": "office.tlv.dynip.nsblast.com",
  "record_type": "A",
  "message": "updated",
  "ts": "2026-02-22T12:34:56Z",
  "client_ref": "echoed-if-provided"
}
```

For multi-host JSON:

```json
{
  "results": [
    { "hostname": "a.example.com", "status": "nochg", "changed": false, "effective_ip": "203.0.113.10", "record_type": "A" },
    { "hostname": "b.example.com", "status": "good",  "changed": true,  "effective_ip": "203.0.113.10", "record_type": "A" }
  ],
  "peer_ip": "203.0.113.10",
  "ts": "2026-02-22T12:34:56Z"
}
```

---

## 8. Config options (service operator)

Config in Config structure in nsblast.h

* `dynip_enable_get`: `true|false`
  If `false`, `GET /nic/update` returns:

  * Legacy: `!disabled` (or `911` if you must stay strict)
  * HTTP: `405 Method Not Allowed` (recommended)

* `dynip_enable_post_json`: `true|false`

* `dynip_allow_partial_multi_host`: `true|false` (default `false`)

* `dynip_max_hosts_per_request`: integer (default 5)

* `dynip_require_user_agent`: `true|false` (default `false`; if true, missing UA returns error)

* `dynip_allow_private_ips`: `true|false` (default `false`)

* `dynip_default_ttl_seconds`: integer (e.g., 60–300) (default 300)

* `dynip_rate_limit`: e.g. `{ "per_fqdn_per_min": 30, "per_user_per_min": 60 }` (default 3, 16)

---

## 9. Examples

### 9.1 Router-style update (server-detected IP)

```http
GET /nic/update?hostname=office.tlv.dynip.nsblast.com HTTP/1.1
Host: dynip.nsblast.com
Authorization: Basic <...>
User-Agent: router-fw/1.0
```

Response:

```
good 203.0.113.10
```

### 9.2 CLI POST (JSON)

```http
POST /nic/update HTTP/1.1
Host: dynip.nsblast.com
Authorization: Basic <...>
Content-Type: application/json

{"hostname":"office.tlv.dynip.nsblast.com"}
```

Response:

```json
{"status":"nochg","changed":false,"effective_ip":"203.0.113.10","peer_ip":"203.0.113.10","hostname":"office.tlv.dynip.nsblast.com","record_type":"A","ts":"2026-02-22T12:34:56Z"}
```

### 9.3 DYNIP permission mismatch attempt

Client tries to spoof:
`myip=1.2.3.4` but peer is `203.0.113.10`

Legacy response:

```
badip
```

JSON response (`403` or `400`, your choice; I’d do `403`):

```json
{"status":"badip","changed":false,"peer_ip":"203.0.113.10","message":"ip must match peer address for dynip hostnames"}
```

---

## 10. Compatibility notes

* Returning a DynDNS token in the body is what many routers depend on; some ignore HTTP status codes entirely.
* Always send `Cache-Control: no-store` since GET is used and you don’t want proxies caching responses.

---

If you want, I can turn this into:

* a compact markdown RFC-style document to drop into your repo, or
* an OpenAPI spec for the JSON POST mode (keeping legacy GET described as “compatibility”).
