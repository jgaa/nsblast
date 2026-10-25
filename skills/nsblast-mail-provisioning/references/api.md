# Mail API client contract

This reference describes the nsblast mail provisioning implementation shipped with this
skill's source repository. It is a client contract snapshot, not proof that a particular
server has deployed the feature. Use the target service's Swagger for version differences.

## Connection and credentials

Configure an HTTPS API root such as `https://dns.example.net/api/v1`, a domain, and a secret
API key using the destination application's configuration/secret store. Join endpoints to
that API root once; avoid doubling `/api/v1`. Keep production credentials out of source,
URLs, debug output, fixtures, and error reports. Verify TLS certificates.

API keys use `Authorization: Bearer KEY`. Basic authentication is also supported. A key
has tenant-owned roles and a tenant-unique, case-insensitive name. An authorized issuer
can create one with:

```http
POST /api/v1/apikey
Content-Type: application/json
Authorization: Basic <issuer credentials>
```

```json
{"name":"mail-automation","roles":["mail-provisioner"]}
```

The role must already exist. Issuance requires `CREATE_APIKEY`, and the issuer can delegate
only permissions/scope it holds; a narrower DNS filter can be delegated from an unfiltered
grant, otherwise filters must match. The response exposes the plaintext key once at
`value.key`; treat the key as opaque rather than enforcing its current prefix or length.
The server stores its hash. Save the returned secret directly to the intended secret store.

`GET /apikey` requires `LIST_APIKEYS` and returns names/roles without secrets.
`DELETE /apikey/{encoded-name}` requires `DELETE_APIKEY` and revokes the key. These are
management permissions for the issuer, not permissions the mail automation itself needs.

For mail automation, use `USE_API` and the required subset of:

| Operation | Permission |
|---|---|
| POST | CREATE_MAIL |
| PUT with no recognized mail configuration | CREATE_MAIL |
| PUT replacing recognized mail configuration | UPDATE_MAIL |
| DELETE | DELETE_MAIL |
| GET transition status | At least one applicable mail permission |

A zone's existence alone does not make PUT an update. PUT does not require DELETE_MAIL.
Tenant allowed-permissions must permit these role grants. DNS filters must cover the mail
domain **and every affected name**, including selectors, policy names and explicitly
managed hosts in other zones. A recursive domain filter usually covers its descendants;
external managed hostnames need their own grant. The caller must own all affected zones.

## Endpoints and semantics

All paths below are relative to the configured `/api/v1` root. Normalize domains to ASCII
DNS names, lowercase and without a trailing dot for client consistency; use the project's
existing IDNA support when accepting internationalized user input. Encode the domain as
one path segment, not as an arbitrary path or query string.

| Method and path | Behavior |
|---|---|
| POST `/provision/mail/{domain}` | Atomic create-only operation. 201 on success; existing requested mail records, even identical ones, conflict. |
| PUT `/provision/mail/{domain}` | Atomic complete replacement of recognized mail configuration. 200 for provisioned, unchanged, or pending work. |
| DELETE `/provision/mail/{domain}` | No body. Removes recognized mail records, with 200 even for a no-op or pending deletion. |
| DELETE `/provision/mail/{domain}?preserve_ip=true` | Retains host A/AAAA records while removing mail configuration. |
| GET `/provision/mail/{domain}/status` | Current domain transition and retry state, or `complete` when no transition is pending. |

Zones must already exist. Secondary DNS zones and cluster followers cannot accept these
mail mutations. There is no mail-specific list/export endpoint, tenant impersonation, or
public scheduling API. Do not substitute generic RR or zone endpoints to evade a conflict.

PUT omission rules are destructive within mail configuration:

- Omitted SPF/DMARC sections use creation defaults; they do not preserve old policies.
- Omitted/empty DKIM removes obsolete recognized selectors. Send the full desired list,
  retaining old selectors during rotation until messages signed with them have drained.
- Omitted MTA-STS and TLS reporting are disabled, subject to policy retirement.
- Omitted address fields preserve addresses of retained MX hosts. Obsolete dedicated
  addresses inside the domain may be cleaned up when exclusive ownership is established.

DELETE preserves the zone, SOA, NS and unrelated RRs/TXT. It also preserves external,
shared, manually changed, or ambiguously owned addresses and reports warnings. Dedicated
address ownership can be invalidated by generic DNS edits or other DNS host references.
Changing ambiguous existing addresses requires a separately authorized generic RR workflow;
do not silently broaden a mail-only key's permissions.

**When moving mail to a domain or mail host outside this DNS server, populate replacement
MX records after DELETE completes. If pending, await completion before publishing them.**
DELETE installs no replacement MX. Manual edits during retirement can cause a safe terminal
failure; report it and reconcile rather than force cleanup.

## Request examples

Dedicated host, relying on default SPF and DMARC:

```json
{
  "mail_servers": [
    {"hostname":"mail.example.com","priority":10,
     "ipv4":["192.0.2.10"],"ipv6":["2001:db8::10"]}
  ],
  "ttl":3600
}
```

Additional domain using an existing shared/external host; no address management:

```json
{
  "mail_servers":[{"hostname":"mail.provider.example","priority":10}],
  "spf":{"enabled":true,"policy":"v=spf1 include:outbound.provider.example -all"},
  "dmarc":{"enabled":true,"policy":"none","rua":"mailto:dmarc@example.com"},
  "mta_sts":{"enabled":false},
  "tls_reporting":{"enabled":false}
}
```

Multiple hosts are entries in `mail_servers` with their own priorities and address arrays.
An authoritative local MX target needs usable A/AAAA in the proposed final state and must
not be a CNAME. External targets are not recursively resolved or validated for deliverability.

Optional sections:

| Field | Shape and defaults |
|---|---|
| `ttl` | Integer 1..2147483647; server-configured default if omitted. New TXT policies must match unrelated TXT TTLs at that owner. |
| `spf` | `enabled` defaults true; `policy` defaults `v=spf1 mx -all`. Configure actual outbound senders rather than assuming MX sends mail. |
| `dkim` | Array of objects with `selector`, `public_key`, optional `algorithm` (only `rsa`). Dotted selectors accepted. |
| `dmarc` | `enabled` defaults true; `policy`: `none` (default), `quarantine`, `reject`; optional `rua`/`ruf` mailto destinations. |
| `mta_sts` | `enabled` defaults false; `mode`: `testing` (default), `enforce`, `none`; `max_age` defaults 604800, range 0..31557600 seconds; optional `ipv4`/`ipv6` for the HTTPS policy host. |
| `tls_reporting` | `enabled` defaults false; enabling requires `rua` containing mailto or HTTPS destinations. Independent of MTA-STS. |

DKIM input is canonical Base64 DER **RSAPublicKey**, 2048..8192 bits, suitable for the DNS
`p=` tag. PEM, private keys and SubjectPublicKeyInfo wrappers are not accepted. Obtain the
public key from the mail system; use existing crypto tooling for conversion if needed,
without transmitting private material to nsblast. The server splits long TXT strings.

Example DKIM section, with an illustrative key placeholder that must be replaced:

```json
{"dkim":[{"selector":"2026.mail","algorithm":"rsa","public_key":"BASE64_DER_RSA_PUBLIC_KEY"}]}
```

Limits: request 128 KiB; 16 unique MX hosts; 32 requested DKIM selectors; 16 addresses per
family/host; encoded DKIM key 2048 bytes; SPF 4096 bytes; each reporting field 2048 bytes
and at most eight comma-separated destinations. IP arrays, when provided, contain 1..16
addresses. Unknown fields are rejected. SPF supports known mechanisms and redirect/exp
with literal domains and valid IP/CIDR terms; macros and unknown modifiers are rejected.
At most ten directly visible lookup terms are accepted. External include/redirect runtime
lookup counts are not guaranteed. Reporting mailboxes are never invented.

Discovery handles at most 128 recognized old DKIM selectors and scans at most 10000 owners.
The current IXFR format limits an atomic zone change to a conservative 60000 bytes of
before/after entries plus overhead. A larger change returns 413 without a partial commit.

## MTA-STS deployment and deferred changes

Before enabling, configure trusted HTTPS on port 443 for `mta-sts.DOMAIN`, serving:

```text
https://mta-sts.DOMAIN/.well-known/mta-sts.txt
```

This is an anonymous policy endpoint **outside `/api/v1`**. HTTP Host selects the policy;
X-Forwarded-Host is ignored. Point the policy hostname at the actual HTTPS service or
reverse proxy, not automatically at the mail server. Optional `mta_sts.ipv4`/`ipv6` manage
that host's addresses; omission leaves them unchanged. Certificate issuance/renewal and
SMTP/mailbox/PTR/DNSSEC setup are outside provisioning. Keep administrative endpoints
protected when configuring a proxy.

Downgrading/removing MTA-STS, or making enforced MX changes incompatible with cached
policy, can require retirement. The server atomically publishes `mode: none` with a new
TXT revision and retains the working mail configuration until all old policies can expire,
including older policies with longer max_age. Do not implement a competing retirement timer
or remove the policy endpoint early in the client.

A pending mutation returns HTTP 200 with an envelope such as:

```json
{
  "error":false,
  "status":200,
  "value":{
    "domain":"example.com",
    "status":"pending",
    "transition":"TRANSITION_ID",
    "earliest_completion":1800000000,
    "applied_policy":{"mode":"none","mx":["mail.example.com"],"max_age":86400},
    "created":[],"updated":[],"deleted":[],"warnings":[]
  }
}
```

Client handling:

1. Inspect HTTP status and `error`; then inspect `value.status`. Immediate mutation success
   uses `provisioned` or `deleted`. Repeated pending requests can report `running` or `failed`
   with HTTP 200. Do not treat every 2xx response as completed work.
2. Persist domain, operation, desired configuration and transition ID in the application's
   existing job store. `earliest_completion` is UTC Unix seconds, an earliest safe time,
   not a completion guarantee. Avoid active polling throughout a potentially long cache wait.
3. Use GET status, with bounded backoff and a polling deadline/cancellation condition. Hand
   longer waits to the application's durable job mechanism; do not block a request thread
   or keep an agent turn alive for days. Surface `pending`, `running`, and `failed` states.
4. Status includes `attempts`, `max_retries`, `next`, and `failure` while pending. Server
   transient retries default to five after the initial attempt, persist across restarts,
   and stop at the recorded limit. A failed identical request does not reset that budget.
5. A different desired request supersedes pending work. Serialize application writes by
   domain and revision; if a returned transition differs, do not attribute its completion
   to the old job or replay the old desired state. Status describes current domain state,
   not historical tasks. `complete` means no current pending transition.

Deferred execution rechecks initiating credentials, tenant ownership, permissions and DNS
fingerprints. Keep the initiating key valid through completion. Manual DNS edits, revoked
credentials and unsafe conflicts fail without deleting the intervening configuration.
Retain diagnostics for the operator and submit a corrected request only after reconciliation.

## Failures and retries

| HTTP/result | Client action |
|---|---|
| 400 | Fix input/unsupported fields/limits; do not retry unchanged. |
| 401 | Correct missing or revoked credentials; do not log their values. |
| 403 | Check mail permission, per-name scope, tenant and write authority. Do not fall back to broader credentials automatically. |
| 404 | Check deployed feature availability, route, domain/zone and authority. Do not create a zone as an implicit fallback. |
| 409 | Explain existing record, TXT TTL, delegation, ambiguous ownership or unknown policy history. Do not overwrite with PUT unless complete replacement was intended. |
| 413 | Report atomic IXFR size limit; do not split a replacement into partial commits. |
| 5xx/network timeout | Outcome may be unknown. Use bounded backoff and the reconciliation rules below. |
| 200 with failed state | Report terminal transition failure; stop polling/retrying as though it were pending. |

PUT and DELETE are idempotent for identical intent, but a replay must still belong to the
application's current domain revision; it must not supersede newer work. Do not use rapid
mutation resubmissions as status polling. For an ambiguous POST outcome, a subsequent 409
can mean the first POST succeeded, but does not prove who created those records. Reconcile
using authorized observations/application state; do not automatically convert POST to PUT
or delete records to retry. Distinguish an application polling timeout from a server task
failure: the durable server operation may still be pending.
