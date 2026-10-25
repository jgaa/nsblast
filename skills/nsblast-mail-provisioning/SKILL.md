---
name: nsblast-mail-provisioning
description: Integrate another project's mail administration or automation with the nsblast mail DNS provisioning API, including scoped API keys, complete configuration replacement, safe removal, and deferred MTA-STS transitions. Use for nsblast mail DNS clients, not mailbox or SMTP server administration.
---

# nsblast mail provisioning

Use nsblast's mail API to create, reconcile, or remove a domain's mail DNS configuration.
This skill is portable: copy the entire folder into the destination agent's supported
skills directory, or load this file by path. Its bundled reference needs no nsblast checkout.

Read [references/api.md](references/api.md) when implementing requests, credentials,
validation, or transition handling. It contains the API contract, examples, limits, and
response handling. In an nsblast checkout, maintain it against `swagger/swagger.yaml`,
`doc/mail-provisioning.md`, and `src/lib/MailProvisioner.cpp`. For a deployed service,
check its published Swagger/capabilities when available; do not assume the new endpoints
are present on every installed version.

## Integration workflow

1. Find the destination project's HTTP client, secret configuration, domain model, job
   storage, and tests. Reuse those facilities. Obtain the nsblast HTTPS API root, target
   domain, credentials, desired MX hosts, and policy choices from existing configuration
   or the user. Do not invent real addresses, credentials, selectors, or reporting mailboxes.
2. Use a tenant-owned key with `USE_API`, `CREATE_MAIL`, `UPDATE_MAIL`, and `DELETE_MAIL`,
   restricted to the managed domain and affected names. Provisioning needs no generic RR
   or zone permissions. Request only the subset of mail permissions the integration uses.
3. Construct the complete desired configuration. Use POST for create-only intent and PUT
   for authoritative reconciliation. PUT is an atomic logical DELETE plus POST: omitted
   policies take defaults and omitted DKIM selectors disappear. Preserve rotation selectors
   and explicit policy choices in the application's desired state before submitting PUT.
4. Send requests using the existing HTTP client with TLS verification, finite timeouts,
   encoded domain path segments, and redacted authentication headers. A request to build
   an integration does not itself authorize changing live DNS; use mocks or an authorized
   test deployment for development verification.
5. Parse both HTTP status and the JSON body. HTTP 200 can mean pending or failed work,
   not completed provisioning. Persist transitions in the application's job system; follow
   the bounded polling/retry rules in the reference. Serialize application writes per domain
   and do not replay an older desired configuration over a newer request.
6. Surface change reports, preserved-address warnings, pending deadlines, and terminal
   failures. Test complete replacement, invalid-input atomicity, pending retirement,
   authorization failures, ambiguous network outcomes, and deletion ordering with mocks.

## Essential constraints

- The zone must already exist and belong to the credential's tenant. Mutations target the
  primary or a standalone server. This API does not support tenant impersonation.
- DELETE removes recognized mail records, never the zone or unrelated RRs. Preserve shared
  infrastructure; use `preserve_ip=true` when removing mail while retaining host addresses.
- **For migration to mail infrastructure outside this DNS server, publish replacement MX
  records after DELETE has completed. If deletion is pending, wait for completion first.**
- MTA-STS retirement is server-managed. Do not shorten its deadline, bypass it with generic
  DNS writes, repeatedly resubmit changed requests, or reset retries to force completion.
- Enable MTA-STS only after trusted HTTPS delivery for `mta-sts.DOMAIN` is configured. Its
  policy-host addresses are independent of MX addresses. nsblast supplies no certificates.
- Submit DKIM public keys only. Mailboxes, SMTP setup, private-key generation, PTR, DNSSEC,
  and certificate automation are outside this API.

Report the integration files and tests changed, configuration names without secret values,
and whether behavior was verified with mocks, a test deployment, or live DNS. Distinguish
an accepted pending operation from completed mail provisioning.
