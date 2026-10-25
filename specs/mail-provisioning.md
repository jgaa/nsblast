# nsblast: Mail DNS Provisioning

Status: implementation specification. See `doc/mail-provisioning.md` for the API guide,
operational requirements, limits, and implementation decisions.

## 1. Objective

Implement a REST API that provisions, updates, and removes the DNS records required to operate an email domain.

The primary goal is to allow a mail administration service to configure DNS with one API call.

Support two common workflows:

1. **Dedicated mail server:** A domain has its own mail server hostname and IP addresses. nsblast provisions both the mail domain records and the mail server's A/AAAA records.
2. **Existing mail server:** A domain is added to an existing mail server that may already handle many domains. nsblast provisions the new domain's mail-related records without changing the existing mail server's address records.

The feature must be independent of the actual mail server software.

It must support Courier, Postfix, Exim, and other mail servers without requiring knowledge of their internal configurations.

### Design principles

- Simple, declarative REST API.
- Reuse existing nsblast functionality.
- Atomic DNS updates.
- Respect existing authentication, authorization, and tenant isolation.
- Support both dedicated and shared mail hosting.
- Idempotent PUT and DELETE.
- No separate provisioning database.
- Do not require permanent manifests of every provisioned DNS record.
- Preserve unrelated DNS records.
- Integrate with the existing Swagger/OpenAPI documentation.
- Support public MTA-STS policy delivery through the existing HTTP server.
- Avoid unnecessary dependencies or new infrastructure.
- Reuse a general, database-backed internal timed-task executor for deferred changes.

## 2. Scope

### Included

- MX records.
- A and AAAA records for mail server hostnames.
- SPF.
- DKIM public key records.
- DMARC.
- MTA-STS DNS records.
- Public HTTPS delivery of MTA-STS policies.
- SMTP TLS reporting (TLS-RPT).
- POST, PUT, and DELETE provisioning operations.
- Persistent MTA-STS policy configuration.
- Durable internal timed tasks with bounded retries and configurable execution concurrency.
- Validation and conflict detection.
- Functional and unit tests.

### Excluded

- DNSSEC.
- DANE/TLSA.
- Automatic certificate issuance.
- ACME integration.
- Automatic certificate renewal.
- DKIM private key generation or management.
- Mail server account provisioning.
- Mailbox management.
- SMTP server configuration.
- Reverse DNS/PTR provisioning.
- Client autoconfiguration, unless already supported through reusable functionality.

nsblast currently does not support DNSSEC. Do not introduce DNSSEC dependencies as part of this feature.

## 3. REST API

Expose three mutation operations, plus an authenticated read-only operation for pending
transition status when timed execution is involved.

Suggested URL structure:

```http
POST   /api/v1/provision/mail/{domain}
PUT    /api/v1/provision/mail/{domain}
DELETE /api/v1/provision/mail/{domain}
```

The exact URL prefix should follow established nsblast API conventions. Inspect existing routes before implementation.

The domain is a DNS domain name such as `example.com`, not necessarily the hostname of the mail server.

Use the existing API authentication and authorization mechanisms.

Do not introduce a separate authentication scheme.

### 3.1 POST — Initial provisioning

Create the requested mail DNS configuration.

Requirements:

1. Validate the request.
2. Resolve the relevant DNS zone.
3. Verify authorization.
4. Generate the required DNS records.
5. Detect conflicts.
6. Abort if any record targeted for creation already exists.
7. Apply all changes atomically.
8. Return the resulting configuration.

POST must not partially provision a domain.

Validate and normalize the complete request and proposed final DNS state before writing
anything, including policy objects or scheduled tasks. A failure in any requested host,
selector, policy, permission check, or conflict check must leave the configuration unchanged.

If any targeted record exists, including one containing an identical value, return:

```http
409 Conflict
```

Do not overwrite any existing records.

A successful request should return:

```http
201 Created
```

### 3.2 PUT — Provision or update

Create or replace the desired mail DNS configuration.

PUT is authoritative for the mail-related configuration of the specified domain.

PUT has the logical effect of DELETE followed by POST, committed in one database
transaction. Do not call the public DELETE and POST handlers sequentially. Build one
replacement change set, validate the entire request and proposed final state first,
then remove the old recognized mail configuration and create the replacement atomically.
POST conflict checks for PUT run against the proposed state after logical removal of
the old mail configuration; unrelated records remain present and protected.

Omitted optional mail policy sections are evaluated using the documented creation
defaults, rather than preserving the previous policy. An omitted or empty DKIM selector
list removes obsolete recognized selectors. Explicitly disabled optional policies are
removed, subject to MTA-STS retirement. Apply these rules consistently to POST and PUT.

It must:

- Create missing records.
- Replace obsolete MX records.
- Update SPF.
- Update DKIM records.
- Update DMARC.
- Update MTA-STS.
- Update TLS-RPT.
- Update A/AAAA records when explicitly managed by the request.
- Remove obsolete mail records covered by the replacement operation.
- Preserve unrelated DNS records.

PUT must be idempotent.

Repeating an identical request must not introduce duplicate records or unnecessary changes.

A successful PUT returns `200 OK` with a change report and warnings. A policy downgrade
that needs a retirement interval returns a documented pending transition as specified
in section 6.6, rather than reporting the requested final state as already applied.

The logical DELETE phase follows the address-record safeguards in section 3.4.
Omitted address fields request no new address records; they never authorize deletion
of shared, external, unrelated, or ambiguously owned address records. Validate local MX
targets against the complete proposed final state so replacement cannot leave a
managed local mail host without usable address records.

When switching from a dedicated mail hostname to a shared server, old dedicated address records may require cleanup. If ownership cannot be established safely, preserve them and return a warning rather than deleting potentially shared infrastructure.

### 3.3 DELETE — Remove mail configuration

```http
DELETE /api/v1/provision/mail/example.com
```

Remove all identifiable mail-related DNS records for the specified domain.

Never delete the DNS zone, its SOA, its NS records, or any unrelated RR. A domain may
continue hosting websites and other services after its mail configuration is removed.

The request requires only the domain name.

It must work even if the domain's mail records were created manually rather than through this provisioning API.

By default, also remove the A/AAAA records associated with mail server hostnames belonging to the domain, subject to the safeguards below.

Support:

```http
DELETE /api/v1/provision/mail/example.com?preserve_ip=true
```

`preserve_ip` is optional and defaults to `false`.

When enabled, preserve the mail server A/AAAA records.

DELETE must discover the current MX targets before removing the MX records.

Record removal should include:

- All MX records at the domain.
- The domain's SPF TXT policy.
- The domain's DMARC TXT policy.
- DKIM selector records under `_domainkey`.
- MTA-STS TXT records.
- TLS-RPT TXT records.
- Stored MTA-STS policy configuration.
- Mail server A/AAAA records, when applicable.

Other TXT records must remain untouched.

Other DNS records must remain untouched unless explicitly identified as mail-related.

DELETE should be idempotent. Repeating it should succeed without damaging other records.

Return `200 OK` with the change report and warnings, including for an idempotent no-op.
If MTA-STS retirement prevents immediate completion, report the pending work and its
earliest completion time as specified in section 6.6.

Swagger must state explicitly: when moving mail service to a domain or mail host not
handled by this DNS server, populate the replacement MX records after this DELETE has
completed. DELETE removes the current MX configuration; it does not install replacement
MX records. If deletion is pending, wait for completion before publishing replacement
records, so delayed cleanup cannot remove the new configuration.

### 3.4 DELETE address-record safeguards

The default is to remove mail-server IP address records, but this must not endanger shared mail infrastructure.

Rules:

1. Identify MX target hostnames before deleting MX records.
2. Only consider A/AAAA records belonging to hostnames within the specified domain by default.
3. Never automatically delete A/AAAA records belonging to an external mail-server hostname.
4. Do not delete A/AAAA records if another known MX configuration references the hostname.
5. If ownership or sharing cannot be determined reliably, preserve the records and return a warning.
6. Respect `preserve_ip=true`.
7. Never remove zone metadata or unrelated records, including addresses also used for
   websites or other services. A name inside the domain and the absence of other local
   MX references do not, by themselves, prove exclusive mail use.

For example:

```text
Domain: example.com
MX:     mail.example.com
```

Default DELETE may remove:

```dns
mail.example.com. A
mail.example.com. AAAA
```

However:

```text
Domain: customer.com
MX:     mail.hosting.net
```

Deleting `customer.com` must not remove:

```dns
mail.hosting.net. A
mail.hosting.net. AAAA
```

The referenced hostname could serve hundreds of domains.

Shared-host detection is necessarily limited to DNS data visible to nsblast. Document this limitation.

## 4. Request format

Use JSON.

The following schema is illustrative. Adapt naming conventions to the existing nsblast API.

### 4.1 Dedicated mail server

```json
{
  "mail_servers": [
    {
      "hostname": "mail.example.com",
      "priority": 10,
      "ipv4": ["192.0.2.10"],
      "ipv6": ["2001:db8::10"]
    }
  ],
  "spf": {
    "enabled": true,
    "policy": "v=spf1 mx -all"
  },
  "dkim": [
    {
      "selector": "mail",
      "algorithm": "rsa",
      "public_key": "<base64-public-key>"
    }
  ],
  "dmarc": {
    "enabled": true,
    "policy": "none",
    "rua": "mailto:dmarc@example.com"
  },
  "mta_sts": {
    "enabled": false
  },
  "tls_reporting": {
    "enabled": false
  },
  "ttl": 3600
}
```

The IP addresses above are examples reserved for documentation.

This request provisions the domain's MX and authentication records, along with the mail server's address records.

### 4.2 Additional domain on an existing mail server

```json
{
  "mail_servers": [
    {
      "hostname": "mail.hosting.net",
      "priority": 10
    }
  ],
  "spf": {
    "enabled": true,
    "policy": "v=spf1 mx -all"
  },
  "dkim": [
    {
      "selector": "mail",
      "algorithm": "rsa",
      "public_key": "<base64-public-key>"
    }
  ],
  "dmarc": {
    "enabled": true,
    "policy": "none"
  },
  "ttl": 3600
}
```

This request does not manage the address records of `mail.hosting.net`.

The existence or absence of IP address fields determines whether address-record provisioning is requested.

Do not require an additional mode field.

### 4.3 Multiple mail servers

Support multiple MX records from the beginning.

Example:

```json
{
  "mail_servers": [
    {
      "hostname": "mx1.example.com",
      "priority": 10,
      "ipv4": ["192.0.2.10"]
    },
    {
      "hostname": "mx2.example.com",
      "priority": 20,
      "ipv4": ["192.0.2.20"]
    }
  ]
}
```

All MX records are generated as part of the same atomic operation.

### 4.4 Defaults

Use practical defaults.

Recommended:

| Setting | Default |
|---|---|
| MX priority | 10 |
| TTL | Existing nsblast zone default, otherwise 3600 |
| SPF enabled | true |
| SPF policy | `v=spf1 mx -all` |
| DKIM | Optional; require key when configured |
| DMARC enabled | true |
| DMARC policy | `none` |
| MTA-STS enabled | false |
| TLS-RPT enabled | false |
| MTA-STS policy mode | `testing` when enabled |
| MTA-STS max_age | 604800 |

SPF must remain configurable. The `mx` mechanism is only a useful default when the MX servers also send outgoing mail. Some deployments use separate outbound relays.

Do not generate restrictive SPF policies that silently exclude explicitly configured outbound mail servers.

Avoid manufacturing default reporting mailboxes. If reporting addresses are omitted, do not assume that mailboxes such as `dmarc@example.com` exist.

Use accepted DNS syntax and normalize names consistently with existing nsblast behavior.

## 5. DNS record generation

For:

```text
Domain: example.com
Mail server: mail.example.com
IPv4: 192.0.2.10
```

Generate:

```dns
example.com.                 IN MX 10 mail.example.com.
mail.example.com.            IN A 192.0.2.10

example.com.                 IN TXT "v=spf1 mx -all"

mail._domainkey.example.com. IN TXT "v=DKIM1; k=rsa; p=..."

_dmarc.example.com.          IN TXT "v=DMARC1; p=none"
```

Support optional IPv6 records.

### 5.1 SPF

SPF records use TXT.

There must be no more than one SPF policy at the same owner name.

Validate:

- SPF syntax.
- Mechanisms and modifiers.
- IP address syntax.
- Included domain names.
- DNS lookup limitations where practical.

SPF updates must preserve unrelated TXT records.

### 5.2 DKIM

Accept one or more selectors.

Required fields:

- Selector.
- Public key.
- Optional key algorithm.

The public key is supplied by the mail administration service.

nsblast must never require the private key.

Generate the appropriate TXT records under:

```text
<selector>._domainkey.<domain>
```

Validate selectors and public keys.

Support RSA keys initially. Support Ed25519 if the existing DNS representation makes doing so straightforward.

Correctly split long TXT values into DNS character strings of no more than 255 octets, preserving the logical TXT value.

For PUT, the supplied DKIM selector list represents the desired configuration. Remove obsolete DKIM TXT selectors belonging to this domain, including selectors created before the provisioning API existed.

For DELETE, remove DKIM records for all recognized selectors.

Do not delete unrelated records merely because they occur beneath `_domainkey`. Only recognized DKIM records should be removed.

Recognize manually created records by parsing their tag lists, not solely by checking
for a `v=DKIM1` prefix: the version tag is optional under RFC 6376. Support dotted selector
names. Preserve selector CNAME delegations and delegated DNS subtrees unless an explicit,
authorized operation can manage them safely; report an unsafe replacement as a conflict.
Document that key rotation requires retaining old selectors in the supplied list while
messages signed with those keys may still be in transit.

### 5.3 DMARC

Generate:

```dns
_dmarc.example.com. IN TXT "v=DMARC1; p=none"
```

Support at least:

- `none`
- `quarantine`
- `reject`

Allow optional aggregate and forensic reporting URIs where supported by the standard.

Default to `none` for new configurations.

Do not require reporting addresses.

### 5.4 MX records

Normalize hostnames and priorities.

Reject invalid MX configurations.

Ensure that the mail domain does not end up with conflicting CNAME records at the MX owner name.

MX targets must not be CNAME aliases.

Do not create A/AAAA records for external mail hosts unless the caller is authorized to manage those names and the request explicitly asks for it.

### 5.5 Address records

When IP addresses are supplied, generate A and/or AAAA records.

When omitted, leave the host's existing A/AAAA records untouched.

Validate addresses using the project's existing IP parsing utilities where possible.

Do not permit accidental replacement of unrelated address records outside the caller's authorized zones.

## 6. MTA-STS

Implement MTA-STS support with both DNS records and a publicly accessible HTTPS policy endpoint.

MTA-STS is optional and disabled by default.

### 6.1 DNS records

When enabled for `example.com`, provision:

```dns
_mta-sts.example.com. IN TXT "v=STSv1; id=<policy-id>"
```

Also support the A/AAAA record required for:

```text
mta-sts.example.com
```

The address must point to the HTTPS service exposing the MTA-STS policy.

Do not assume the mail server IP and the policy server IP are identical.

Provide an optional configuration field for the MTA-STS HTTPS endpoint's IPv4/IPv6 addresses.

If those addresses are omitted, do not modify existing records for `mta-sts.example.com`.

### 6.2 HTTPS policy endpoint

Serve:

```http
GET /.well-known/mta-sts.txt
Host: mta-sts.example.com
```

The response must be available without authentication.

Example:

```text
version: STSv1
mode: testing
mx: mail.example.com
max_age: 604800
```

Requirements:

- Use the existing nsblast HTTP server.
- No authorization required for this specific endpoint.
- All existing administrative routes remain protected.
- Return `Content-Type: text/plain`.
- Derive the mail domain from the validated request hostname.
- Load the stored policy configuration.
- Generate the policy response.
- Return 404 for unknown policies.
- Do not disclose unrelated domain configuration.

The HTTP hostname must be validated independently from any reverse proxy headers.

Do not trust arbitrary `X-Forwarded-Host` values.

The public endpoint is selected by hostname, not by an administrative API domain parameter.

### 6.3 TLS requirements

The public endpoint must work through HTTPS on port 443.

It requires a publicly trusted TLS certificate valid for:

```text
mta-sts.example.com
```

For the initial implementation:

- Reuse existing HTTPS capabilities.
- Support deployment behind an existing reverse proxy.
- Do not implement ACME.
- Do not implement automatic TLS certificate selection.
- Do not implement certificate caching.
- Do not implement certificate renewal.

Documentation must explain the required HTTPS deployment.

If the deployment does not have valid certificates for the policy hostnames, MTA-STS must remain disabled until the infrastructure is configured.

### 6.4 Policy storage

Store the MTA-STS configuration persistently in RocksDB.

Reuse existing data access patterns.

There is no requirement to persist complete manifests of provisioned DNS records.

The stored MTA-STS configuration should include:

- Domain.
- Mode.
- Allowed MX hostnames.
- Maximum policy age.
- Policy revision identifier.
- A conservative expiry bound covering every previously served policy that may still
  be cached, including older policies with a longer maximum age.
- Pending transition generation and its final desired configuration, when applicable.
- Any additional information necessary to serve the policy.

A PUT operation must update the DNS record and stored policy atomically.

DELETE must remove the stored policy in accordance with the lifecycle rules below.

### 6.5 Policy revision

Generate a deterministic policy revision identifier from the policy contents.

A hash of the canonical policy document is appropriate. Encode the revision as at most
32 alphanumeric characters, for example the first 32 hexadecimal characters of SHA-256,
to satisfy RFC 8461 section 3.1. Do not use a full-length SHA-256 hex string or ordinary
Base64 as the DNS policy ID.

Do not change the revision when PUT receives an identical policy.

When the policy contents change, update the DNS revision identifier.

### 6.6 MTA-STS removal

MTA-STS policies may remain cached by remote mail servers.

Immediately deleting an enforced policy may therefore cause delivery failures if an SMTP server is simultaneously decommissioned.

Support ordinary removal for domains without an active enforcement policy.

For enforced policies, provide a documented safe retirement path that publishes `mode: none` with a new policy ID and maintains the HTTPS endpoint while cached policies expire.

Do not silently pretend that deleting a TXT record immediately disables previously cached MTA-STS policies.

Follow [RFC 8461 section 8.3](https://www.rfc-editor.org/rfc/rfc8461.html#section-8.3)
when PUT downgrades or disables an enforced policy, and when DELETE retires it:

1. Publish an intermediate `mode: none` policy with a small `max_age` (initial default:
   one day) and a new policy ID, preserving the HTTPS policy endpoint.
2. Persist the final desired operation and a timed task in the same transaction as the
   intermediate policy and DNS revision. Until it is safe to apply the replacement,
   keep the old mail configuration complete and compatible with still-cached enforced
   policies; do not switch MX hosts and assume that cached senders have fetched `none`.
3. Calculate the earliest safe execution time from all still-valid previous policies,
   rather than using the intermediate policy's smaller `max_age`. Persist this bound.
   Apply this protection even if the currently served policy is already `testing` or
   `none` but an older enforced policy may still be cached. If the previous policy
   history cannot be bounded reliably, report a conflict rather than guess a safe time.
4. Once that time is reached, atomically apply the validated final replacement or
   deletion, updating the policy, DNS, and task state together. A requested final
   `testing` policy is published at this stage; a disabled policy is removed.

The temporary pending request is operational state, not a permanent manifest of all
provisioned DNS records. Do not retain a complete record manifest after completion.

Return `200 OK` for the accepted transition with `status: pending`, a stable transition
identifier, the currently applied configuration, and the earliest completion time.
Make pending and failed transition status observable to the authorized provisioning
client through a read-only status operation; its final URL must follow project conventions.
Do not report a pending configuration as fully provisioned or deleted.

Repeated identical requests must reuse the pending transition and must not reset its
deadline or retry counter. A newer authorized request must supersede the old transition
transactionally. The old task must check its expected domain generation before every
execution and become a harmless no-op if superseded. Re-enabling enforcement must prevent
an old retirement task from deleting the new configuration. Deferred changes must also
detect intervening manual DNS changes and fail safely rather than remove replacement MX
records or unrelated data.

### 6.7 General internal timed-task execution

Implement a reusable internal task executor backed by the existing RocksDB database.
Use typed handlers and versioned payloads; tasks are not shell commands, arbitrary code,
or a new public scheduling API. Mail policy retirement is its first consumer.

Persist each task's stable ID, handler type/payload version, tenant and affected resource,
expected resource generation, due time in UTC, state, attempts already made, retry limit,
next retry time, claim/lease information, and last failure. Persist the initiating stable
principal identity and enough authorization context to recheck the operation without
storing passwords or plaintext API keys.

Maintain an ordered database index by next execution time and task ID. Read only the next
eligible work and bounded batches; do not load every future task into memory or scan the
whole queue on a polling interval.

Execution requirements:

- Use one internal Asio timer for the next eligible time-point. Rearm it when an earlier
  task is added, cancelled, retried, or completed, or execution capacity becomes available.
  Persist UTC deadlines and use the timer's monotonic wait mechanism; recheck the UTC
  deadline before execution and recalculate waits after clock changes.
- On wakeup, claim due tasks transactionally and post their handlers to the existing
  Asio context. Limit the combined number of queued-for-execution and running tasks.
- Make the concurrency limit configurable through the existing configuration/Vars
  mechanism, default **8**, minimum 1. Lowering the limit must not cancel running work;
  refrain from starting more work until the active count falls below the new limit.
- When all slots are occupied, do not repeatedly fire an already-expired timer. Task
  completion or the next relevant lease event must trigger reconsideration of due work.
- Persist the attempt count before dispatch. A retry must retain its task ID, history,
  payload, deadline constraints, and resource generation across restart and failover.
- Make the maximum retry count configurable, initially **5 retries after the first
  attempt** (six attempts total). Record the chosen limit on the task. Use bounded
  exponential retry backoff with jitter; proposed defaults are 30 seconds initially,
  capped at one hour. Never retry indefinitely or schedule before the safety deadline.
- Retry transient failures only. Invalid payloads, revoked authorization, ownership
  changes, and unsafe DNS conflicts are terminal or cancelled states, with a clear reason.
- Mark exhausted tasks failed, retain their diagnostic state, and expose the failure
  through logs and transition status. Do not automatically reset their retry budget.
- Recover interrupted claimed tasks after their leases expire. Delivery may occur more
  than once, so handlers must be idempotent and fence stale claims/generations. Where
  effects are in this database, commit them and task completion in the same transaction.
- Recheck current tenant ownership and the initiating principal's authorization before
  applying deferred mutations. Serialize affected resources with the same mutation
  mechanism as ordinary API writes.
- In clustered deployments, only the active write authority may dispatch mutations.
  Replicate pending tasks, retry/claim state, policy state, and DNS changes as needed for
  failover; a former writer must not retain authority through an old lease.
- Shut down without losing pending work. Do not block an Asio worker by sleeping until
  a deadline, and do not introduce a separate thread or timer per task.

Add storage records and indexes through incremental schema upgrades compatible with
every supported prior version, starting with v0.5.3. Reuse existing logging, configuration,
Asio, transaction, and lifecycle facilities.

## 7. SMTP TLS Reporting

Optionally provision:

```dns
_smtp._tls.example.com. IN TXT \
  "v=TLSRPTv1; rua=mailto:tls-reports@example.com"
```

Use the reporting destination supplied by the caller.

Do not invent a reporting mailbox.

Support TLS-RPT independently of MTA-STS.

Only publish the record when the caller enables reporting and supplies a valid reporting URI.

PUT replaces the current TLS-RPT configuration.

DELETE removes recognized TLS-RPT records.

## 8. Conflict handling

POST must fail if any generated record already exists.

PUT must reconcile the specified domain's mail configuration.

DELETE must clean up recognized mail-related records.

Record discovery and conflict detection must use existing nsblast DNS storage APIs.

Do not perform non-atomic sequences of independent REST requests internally.

Pay special attention to:

- Existing MX records.
- Existing SPF policies.
- Multiple TXT records at the domain.
- Existing DKIM selectors.
- Existing DMARC policies.
- Existing MTA-STS records.
- Existing TLS-RPT records.
- Existing address records.
- Shared MX hostnames.
- Hostnames outside the current zone.
- CNAME conflicts.
- Concurrent provisioning requests.

For PUT and DELETE, preserve unrelated records.

A manual DNS configuration must remain manageable through these operations without requiring an existing provisioning manifest.

If an operation encounters an ambiguous or unsafe conflict, return an error rather than guessing.

## 9. Atomicity and persistence

Use existing RocksDB storage facilities.

The DNS update must be atomic at the database level.

MTA-STS policy persistence must participate in the same logical transaction as its DNS changes.

Task creation, cancellation, completion, and the policy transition must participate in
that transaction whenever applicable. A crash must not leave DNS changed without its
corresponding policy/task state, or a task scheduled for a configuration that never committed.

Do not leave a policy object inconsistent with its DNS TXT record after a failed operation.

Update any relevant in-memory DNS caches and zone metadata through the existing mutation path.

Ensure normal DNS serving sees the new configuration consistently.

Respect existing serial-number handling, change notification mechanisms, and replication behavior.

Do not bypass established nsblast update mechanisms simply to simplify the feature.

Extract shared internal methods for change-set validation and application instead of
duplicating generic RR mutation logic inside the mail handler. Both the ordinary RR API
and mail provisioning must use the shared lower-level facilities where applicable.
Preserve per-record TXT values when replacing only recognized mail policies.

Acquire all affected zone locks in a consistent order, reread state under the locks,
validate the final change set, and commit once. Increment each changed zone's SOA serial
once, produce a complete IXFR diff for all changes in that zone, and notify after commit.
An identical PUT must produce no writes, serial increments, or notifications.

The current transaction logger only includes DNS ENTRY mutations. Extend replication
explicitly for policy and timed-task records and indexes; storing them in ACCOUNT alone
does not replicate them. Applying a replicated transaction must also be all-or-nothing;
do not catch a failed part and commit the remaining parts. Define atomicity as database
transaction atomicity on each node, not simultaneous visibility across DNS caches or
asynchronously replicated nodes. Verify backup/restore and promotion behavior.

## 10. Authorization

Use existing RBAC and tenant isolation.

Add `CREATE_MAIL`, `UPDATE_MAIL`, and `DELETE_MAIL` to the existing permission model.
Do not require generic `CREATE_RR`, `UPDATE_RR`, or `DELETE_RR` grants for these operations.
POST requires `CREATE_MAIL`; PUT requires `CREATE_MAIL` when no recognized mail configuration
exists and `UPDATE_MAIL` when replacing an existing configuration; DELETE requires
`DELETE_MAIL`. A zone's existence alone does not mean it already has a mail configuration.
The logical DELETE phase of PUT does not independently require `DELETE_MAIL`.

Apply domain/name filters to the provisioning domain and every affected DNS name.
The read-only transition-status operation requires at least one applicable mail permission
for the domain and must disclose only that tenant's authorized mail-transition state.
Mail-only roles must not gain access to generic RR, zone, user, role, or key management.
An API-key issuer may delegate only permissions and scope they already hold.

The authenticated caller must be authorized to modify every DNS record affected by the request.

Cross-zone modifications require authorization for every affected zone.

Never assume that permission to manage `example.com` implies permission to modify `hosting.net`.

For requests involving multiple zones, either ensure that all required changes can be committed atomically or reject the operation before changing anything.

The public MTA-STS endpoint requires no authentication but must be strictly read-only.

Avoid exposing arbitrary database lookup capabilities through the public endpoint.

## 11. Responses and errors

Use the project's existing JSON response conventions.

Suggested success response:

```json
{
  "error": false,
  "status": 201,
  "value": {
    "domain": "example.com",
    "status": "provisioned",
    "created": [
      {"name": "example.com", "type": "MX"},
      {"name": "mail.example.com", "type": "A"},
      {"name": "example.com", "type": "TXT", "purpose": "spf"}
    ],
    "updated": [],
    "deleted": [],
    "warnings": []
  }
}
```

For conflicts:

```json
{
  "error": true,
  "status": 409,
  "code": "record_conflict",
  "message": "Mail provisioning conflicts with existing DNS records",
  "conflicts": [
    {
      "name": "example.com",
      "type": "MX"
    }
  ]
}
```

Recommended status codes:

| Code | Meaning |
|---|---|
| 200 | Successful update/deletion, no-op, or accepted pending transition with explicit status |
| 201 | Successful creation |
| 400 | Invalid input |
| 401 | Authentication required |
| 403 | Not authorized |
| 404 | Zone not found |
| 409 | Existing record or unsafe conflict |
| 500 | Internal failure |

Respect existing project conventions if they differ.

Errors must be descriptive enough for an automated provisioning client to act on them.

Pending-transition responses and status reads must identify the task/transition, current
state, earliest completion time, and any terminal failure. Include actual task retry
information in the authorized status response, without exposing credentials or unrelated
tenant data. Do not put a warning or change report in a `204` response.

## 12. Validation

Validate all input before modifying the database.

Include:

- Domain name syntax.
- Hostname syntax.
- DNS label lengths.
- IP address syntax.
- MX priorities.
- Duplicate MX entries.
- SPF syntax.
- DKIM selector syntax.
- DKIM public keys.
- DMARC policy values.
- Reporting URI syntax.
- TTL bounds.
- MTA-STS mode.
- MTA-STS maximum age.
- Requested record ownership.
- Zone existence.
- Caller authorization.

Normalize DNS names before comparison.

Handle trailing dots and case-insensitive DNS names consistently.

Reject input that would generate invalid DNS records.

Define and document finite limits for request bytes, MX hosts, selectors, address counts,
policy lengths, and reporting destinations. Define DKIM input encoding precisely: initial
RSA support accepts the Base64 DER RSAPublicKey representation used in the DNS `p=` tag,
not an unspecified PEM/private-key format, and validates key type and size with OpenSSL.

Separate deterministic local validation from external DNS inspection. Validate SPF
syntax and directly detectable lookup-limit problems, but do not claim to guarantee the
runtime lookup count of external includes or redirects. Local authoritative CNAME/MX
conflicts must be checked against the proposed final state. Any external DNS validation
must be bounded, performed before locking/writing, and have documented failure semantics;
do not perform unbounded recursive lookups inside a database transaction.

POST conflict detection must distinguish a targeted mail record from unrelated records
at the same owner name. For example, an unrelated TXT record must not prevent creation
of an SPF policy or be overwritten by it. Parse recognized policies after concatenating
their TXT character strings, and preserve unrelated TXT records individually.

## 13. Tests

Add tests using the existing nsblast testing infrastructure.

### POST tests

1. Provision a dedicated mail server.
2. Provision an additional domain using an existing mail server.
3. Provision multiple MX hosts.
4. Provision with IPv4 only.
5. Provision with IPv6 only.
6. Provision with both IPv4 and IPv6.
7. Provision with multiple DKIM selectors.
8. Reject conflicting MX records.
9. Reject conflicting SPF records.
10. Reject conflicting DKIM records.
11. Verify no partial changes after failure.

### PUT tests

1. Create a previously unconfigured domain.
2. Update MX targets.
3. Change mail server IP addresses.
4. Change DKIM selectors.
5. Remove obsolete DKIM selectors.
6. Change DMARC policy.
7. Enable and disable MTA-STS.
8. Repeat identical requests without changing stored records.
9. Preserve unrelated TXT records.
10. Preserve external mail server addresses.
11. Verify atomicity after simulated failures.
12. Verify logical DELETE-plus-POST replacement, including omitted policies and selectors.
13. Reject an invalid replacement without removing the previous working configuration.
14. Verify identical PUTs leave SOA serials, notifications, and pending retry budgets unchanged.

### DELETE tests

1. Delete a dedicated mail domain.
2. Verify default removal of associated A/AAAA records.
3. Verify `preserve_ip=true`.
4. Delete an additional domain on a shared server.
5. Verify shared mail server addresses remain untouched.
6. Remove all recognized DKIM selectors.
7. Preserve unrelated TXT records.
8. Delete a manually provisioned mail domain.
9. Repeat DELETE safely.
10. Delete a domain with no MX records.
11. Reject unauthorized cross-zone modifications.
12. Test MTA-STS policy retirement behavior.
13. Verify the zone, SOA, NS, and unrelated website/service records always survive.
14. Verify `200` responses report preserved addresses and pending retirement accurately.

### MTA-STS tests

1. Publish a policy.
2. Fetch the policy anonymously.
3. Verify the hostname selects the correct policy.
4. Reject unknown policy hostnames.
5. Verify no authentication is required.
6. Verify unrelated API endpoints remain protected.
7. Verify policy revision changes.
8. Verify identical PUT requests preserve the revision.
9. Verify stored policy survives server restart.
10. Verify policy storage and DNS remain consistent after failure.
11. Downgrade enforce to testing/disabled without prematurely switching incompatible MX hosts.
12. Keep the retirement endpoint until every potentially cached old policy has expired.
13. Supersede pending retirement with re-enabled enforcement and reject stale task execution.
14. Detect manual DNS edits before deferred cleanup and preserve replacement MX records.

### Timed-task tests

Use an injectable clock/timer so tests need not sleep for retirement or backoff intervals.

1. Recover pending deadlines, attempts, retry limits, and claims after restart.
2. Arm only the next eligible time-point and rearm for a newly inserted earlier task.
3. Post handlers to the Asio context and enforce the default limit of eight active/queued tasks.
4. Change concurrency at runtime without cancelling active work or starting excess tasks.
5. Avoid a timer busy loop while every execution slot is occupied.
6. Persist retry increments before dispatch and stop after the configured retry budget.
7. Apply bounded backoff and handle permanent failures without retries.
8. Recover expired leases safely; fence stale workers and superseded task generations.
9. Commit handler database effects and completion together; retry safely after a crash.
10. Recheck tenant/principal authorization before applying deferred changes.
11. Replicate policy, task, and DNS mutations atomically; allow only the active writer to dispatch.
12. Preserve safety deadlines across clock changes and interrupted shutdown.

### Security tests

Test:

- Unauthorized provisioning.
- Unauthorized deletion.
- Cross-tenant access.
- Malformed domain names.
- Malformed DKIM keys.
- Host-header manipulation.
- Reverse-proxy header manipulation.
- Conflicting CNAME records.
- Concurrent updates.
- Attempts to delete external mail server addresses.

## 14. Swagger and documentation

Document all three operations in the existing OpenAPI/Swagger interface.

Include examples for:

- Dedicated mail server.
- Existing shared mail server.
- Multiple MX servers.
- DKIM configuration.
- MTA-STS configuration.
- DELETE with `preserve_ip=true`.
- Atomic PUT replacement and validation failure preserving the previous configuration.
- Pending MTA-STS downgrade/removal, status checks, and terminal timed-task failure.

Document default values, side effects, error responses, and limitations.

Swagger must prominently state that DELETE removes only recognized email records and
never deletes the zone or unrelated RRs. Include the migration instruction from section
3.3: when moving to mail infrastructure outside this DNS server, populate replacement MX
records after deletion has completed. Explain pending deletion and the need to await it.
Document the three mail permissions, finite validation limits, and `200` change reports
with warnings. Add these paths when their implementation is available; do not advertise
unimplemented endpoints as working APIs.

Add a short user-facing guide explaining the two provisioning workflows.

Include a section describing how to expose the MTA-STS policy endpoint through nginx or an equivalent reverse proxy.

## 15. Implementation guidance

Before implementing:

1. Inspect the existing REST API conventions.
2. Find the existing DNS record creation, update, and removal facilities.
3. Examine zone and tenant authorization.
4. Examine RocksDB transaction or batch-update facilities.
5. Examine how DNS mutations update caches, zone serials, and replication.
6. Examine Swagger generation.
7. Examine the HTTP routing and public endpoint mechanisms.
8. Examine the existing test infrastructure.

Prefer existing abstractions over new ones.

Keep the mail provisioning implementation in its own logical component rather than adding large amounts of specialized logic to generic request handlers.

The provisioning component should generate a proposed record change set, validate it, and submit it through existing database mutation mechanisms.

Use one shared planning/validation/application pipeline for POST, PUT, DELETE, and deferred
mail transitions. PUT composes the logical removal and creation into a single plan;
tasks execute that same pipeline with current ownership, authorization, and generation
checks. Keep the reusable task executor independent of mail record generation.

Evaluate MCP impact as required by the project rules. No new public scheduling or MCP
task API is authorized by this internal executor requirement.

Do not create an independent DNS record storage path.

## 16. Future work — explicitly not part of this implementation

nsblast could eventually manage certificates for MTA-STS HTTPS endpoints itself.

A future implementation could:

- Use ACME DNS-01.
- Publish challenge records directly.
- Issue certificates from Let's Encrypt.
- Cache certificates locally.
- Select certificates using TLS SNI.
- Renew certificates proactively.
- Support many MTA-STS domains on one HTTPS listener.

Do not implement these features now.

Do not introduce dependencies or abstractions solely for hypothetical ACME support.

However, avoid assumptions in the HTTP policy handler that would make SNI-based certificate selection difficult to add later.

## 17. Deliverables

Implement:

1. Three REST provisioning operations.
2. DNS record generation and validation.
3. Atomic record updates.
4. Dedicated and shared mail server support.
5. DELETE cleanup with `preserve_ip`.
6. Persistent MTA-STS policies.
7. Public MTA-STS policy endpoint.
8. Swagger/OpenAPI documentation.
9. Unit tests.
10. Functional tests.
11. Short user documentation.
12. General durable timed-task execution with a single next-deadline timer, persisted
    bounded retries, and configurable Asio concurrency defaulting to eight.
13. Authenticated transition status and operational failure reporting.
14. Reusable DNS mutation methods shared with existing RR operations, with atomic policy/task replication.

The implementation is complete when all three operations work against an actual nsblast database, generate standards-compliant DNS records, preserve unrelated records, obey authorization rules, and pass the relevant test suite.

If repository architecture requires deviations from this specification, prefer existing project conventions and document the deviations.

Do not expand the scope into general mail server administration, certificate management, or DNSSEC.

## 18. Implementation decisions

- Dedicated address ownership uses a minimal MAIL_HOST claim and DNS_REF index, rather
  than a full record manifest. Generic DNS changes and sharing invalidate exclusivity.
  Manual or ambiguous addresses are preserved; changing them uses the generic RR API.
- The current 16-bit IXFR entry format imposes a conservative 60000-byte atomic change
  limit per zone. Oversized changes are rejected before commit.
- Initial SPF supports literal domains and known mechanisms/modifiers; macros and unknown
  modifiers are rejected. External DNS is not recursively resolved by provisioning.
- Mail mutations do not support tenant impersonation. The task principal belongs to the
  authenticated tenant and is revalidated when deferred work executes.
- Cluster roles are static. Operators must stop/fence the previous primary before
  promotion; automatic leader election is outside this feature.
- A single steady timer targets the next stored UTC deadline. UTC is rechecked before
  execution; clock changes may delay execution but cannot permit an early policy downgrade.
