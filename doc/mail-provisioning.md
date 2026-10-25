# Mail DNS provisioning

Use `POST`, `PUT`, or `DELETE /api/v1/provision/mail/{domain}` with Basic authentication
or an API key in `Authorization: Bearer KEY`. The zone must already exist and belong
to the authenticated tenant. Mutations run on the primary (or a standalone server).
There are no UI changes.

Create a role with `USE_API`, `CREATE_MAIL`, `UPDATE_MAIL`, and `DELETE_MAIL` and a
recursive DNS filter for the domains it manages. Generic RR, zone, user, role, and
key-management permissions are unnecessary. An API-key issuer can delegate only its
own permissions and scope. Tenant administrators must include the mail permissions
in the tenant's allowed-permissions list before assigning them to roles.

For a dedicated server:

```json
{
  "mail_servers": [
    {"hostname": "mail.example.com", "priority": 10,
     "ipv4": ["192.0.2.10"], "ipv6": ["2001:db8::10"]}
  ],
  "dkim": [
    {"selector": "2026.mail", "algorithm": "rsa",
     "public_key": "BASE64_DER_RSA_PUBLIC_KEY"}
  ],
  "ttl": 3600
}
```

Supply a real, canonical Base64 DER **RSAPublicKey** (the DNS DKIM `p=` encoding),
2048–8192 bits. PEM, private keys, SubjectPublicKeyInfo wrappers, and malformed keys
are rejected. Long TXT values are split into DNS strings automatically. Keep older
selectors in PUT requests during rotation until signed messages have drained.

For another domain on a shared server, omit address fields:

```json
{
  "mail_servers": [{"hostname": "mail.provider.example", "priority": 10}]
}
```

Multiple MX hosts are supported. Defaults are SPF `v=spf1 mx -all`, DMARC `p=none`,
no DKIM, and disabled MTA-STS/TLS reporting. The default TTL is the server's configured
DNS default. Configure SPF explicitly if outbound senders differ from inbound MX hosts.
Reporting mailboxes are never invented. Optional `dmarc.rua`/`ruf` accept mailto URIs;
`tls_reporting` requires `enabled: true` and a supplied `rua` (mailto or HTTPS).

POST returns 201 and conflicts with existing requested mail records, even identical
ones. PUT validates a complete replacement, then logically removes the old recognized
mail records and creates the requested records in **one database transaction**. Omitted
policy sections use creation defaults; omitted selectors disappear. Identical PUTs do
not change DNS serials or notify secondaries. Failed validation leaves the previous
configuration intact. Unrelated TXT values and other RRs survive. New TXT policies must
use the same TTL as unrelated TXT values at that owner; conflicting TTLs return 409.

Addresses created at a previously empty hostname receive a small ownership claim,
not a complete provisioning manifest. Changing the hostname through another DNS
mutation invalidates that claim. Any other known MX owner, or an NS/CNAME/PTR/SRV/SOA reference to it, makes it
shared. A provisioning PUT can update exclusively owned addresses; ambiguous or manually
managed addresses require the generic RR API. Omitted address fields preserve the
addresses of retained MX hosts. Cleanup removes obsolete local addresses only when
exclusive ownership is still established. External and ambiguous addresses survive with
warnings. DNS cannot detect services or references outside this server; keep ownership
exclusive in your administration workflow and use `preserve_ip=true` for shared services.

DELETE returns 200 with a change report, including no-op deletes. It discovers recognized
mail records even if they were created manually. It **never deletes the zone, SOA, NS,
unrelated TXT, or unrelated RRs**. DKIM discovery recognizes optional version tags and
dotted selectors; CNAME delegations and delegated subtrees survive.

**When moving mail to a domain or mail host not handled by this DNS server, publish
replacement MX records after DELETE has completed.** DELETE installs no replacement MX.
If retirement is pending, wait for completion first. Publishing replacement records early
causes deferred cleanup to fail safely rather than delete your edits.

## MTA-STS and retirement

For MTA-STS, add:

```json
{
  "mail_servers": [{"hostname": "mail.example.com", "ipv4": ["192.0.2.10"]}],
  "mta_sts": {"enabled": true, "mode": "testing", "max_age": 604800,
              "ipv4": ["192.0.2.20"]},
  "tls_reporting": {"enabled": true, "rua": "mailto:tls@example.com"}
}
```

The optional MTA-STS addresses belong to `mta-sts.example.com`, independently of MX
addresses. Without them, existing policy-host addresses are left alone. Configure trusted
TLS on port 443 **before enabling MTA-STS**. There is no ACME or certificate automation.

The policy is anonymously available only at
`https://mta-sts.example.com/.well-known/mta-sts.txt`. Host selection uses the validated
HTTP Host header, never X-Forwarded-Host. Unknown policy hosts return 404. Administrative
routes remain authenticated. A reverse proxy can terminate TLS:

```nginx
server {
    listen 443 ssl;
    server_name mta-sts.example.com;
    ssl_certificate /etc/ssl/example/fullchain.pem;
    ssl_certificate_key /etc/ssl/example/privkey.pem;
    location = /.well-known/mta-sts.txt {
        proxy_set_header Host $host;
        proxy_pass http://127.0.0.1:8080;
    }
}
```

Replace the upstream with your nsblast HTTP listener. Restrict this virtual host to the
policy location; expose administrative endpoints separately according to your deployment.

Downgrading/removing a policy, or changing enforced MX hosts incompatibly, follows
[RFC 8461 section 8.3](https://www.rfc-editor.org/rfc/rfc8461.html#section-8.3).
The first transaction publishes `mode: none`, a small max_age and a new discovery ID,
while retaining the working mail configuration. The final transaction waits for every
possibly cached old policy, including older policies with longer max_age. Imported
MTA-STS TXT without stored policy history returns 409 instead of guessing a deadline.

A pending response includes `transition`, `applied_policy`, and `earliest_completion`
(UTC Unix seconds). Poll `GET /api/v1/provision/mail/{domain}/status`. It returns the
current transition and attempt count, retry limit, next time and failure diagnostic,
or `status: complete` when no transition is pending. It requires a mail permission
for the domain. This is current-domain status, not a historical task-listing API.

The same pending request reuses its transition without extending deadlines or retry
budgets. A different request cancels/supersedes it atomically. Terminal tasks retain diagnostics,
but discard the temporary desired payload. Deferred execution rechecks
current tenant ownership, the initiating user/key and permissions, and DNS fingerprints.
Manual edits and revoked authority produce a terminal failure. Inspect the diagnostic,
reconcile DNS and desired configuration, then submit a corrected request. An identical
failed request does not reset its retry budget.

## Internal timed execution and operations

The reusable executor stores versioned typed tasks, UTC deadlines, claims, attempts,
retry limits, payloads and diagnostics in RocksDB. There is no public scheduling or shell
execution API. One timer targets the next due event, and handlers are posted to the
existing Asio context. Queueing plus running work is bounded by `timed_task_concurrency`
(default 8, range 1–256). Changes take effect at runtime without cancelling active work.

`timed_task_max_retries` defaults to 5 **after** the first attempt (six attempts total),
range 0–100, captured when each task is scheduled. Transient failures use exponential
backoff from 30 seconds with jitter, capped at one hour. Permanent validation/authority
failures are not retried. Attempts persist before dispatch. Interrupted claims can be
recovered after a five-minute lease; handlers use generation checks and idempotent database
effects. Task completion commits with its DNS/policy effects. Shutdown leaves durable work.
A backward clock adjustment cannot cause an early mutation; deadlines are rechecked in UTC.
A forward adjustment can delay execution until the already armed timer is reconsidered.

Only a standalone server or configured primary dispatches. Cluster roles are static;
promotion requires stopping/fencing the former primary, configuring the new primary and
restarting it. This feature does not add automatic leader election or split-brain fencing.
Authorization state, ownership/reference indexes, policies, tasks/retries and DNS/IXFR are
replicated. A replication-apply failure rolls back the entire transaction and reconnects
from the last committed ID. Backup/restore includes all these RocksDB column families.
Replicated transactions are atomic on each node; DNS caches and asynchronous followers
do not change simultaneously. Do not promote a stale follower.

Schema upgrade 4 rebuilds the RR ownership index from zone suffixes, backfills DNS host references and emits current authorization state in batches
of 256 records before advancing the version marker. Interrupted upgrades can be rerun.
Existing schema upgrades from v0.5.3 remain in order. Follower local maintenance does not
create primary transaction-log IDs.

Finite limits: request 128 KiB, 16 MX hosts, 32 requested DKIM selectors, 16 addresses per
family/host, SPF 4096 bytes, reporting fields 2048 bytes/eight URIs. Discovery scans at most
10000 DKIM owners and handles at most 128 recognized old selectors. The current IXFR
storage format limits one atomic zone change to a conservative 60000 bytes (before and
after entries plus overhead); larger changes return 413 without committing anything.
SPF validates literal-domain syntax, IP/CIDR terms, known mechanisms and redirect/exp,
with at most ten directly visible DNS terms. Macros and unknown modifiers are rejected.
External includes, redirects and aliases are not recursively resolved; their runtime DNS
lookup counts or deliverability cannot be guaranteed by provisioning.

There is currently no nsblast MCP interface to update. Mail provisioning was evaluated
for MCP exposure; no new MCP tool or public task API is introduced.
