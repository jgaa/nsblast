# REST API

The embedded HTTP server exposes the nsblast API under:

```text
/api/v1
```

When swagger support is compiled in and enabled with `--with-swagger`, the
interactive API UI is served under:

```text
/api/swagger
```

When UI support is compiled in and enabled with `--with-ui`, the web UI is
served under:

```text
/ui
```

## Authentication

Most administrative endpoints use normal HTTP authorization handled by the auth
manager. In practice that usually means Basic auth, for example with `curl -u`
or `curl --netrc`.

DynIP update endpoints are different:

- the primary JSON path accepts Bearer tokens
- the legacy DynDNS-compatible path also remains available when enabled by vars

See the DynIP client chapter for the current client-side contract.

## Important endpoint groups

The current API surface includes:

- `/api/v1/version`
- `/api/v1/tenant/...`
- `/api/v1/user/...`
- `/api/v1/role/...`
- `/api/v1/zone/...`
- `/api/v1/rr/...`
- `/api/v1/admin/vars`
- `/api/v1/admin/vars/{name}`
- `/api/v1/dynip/...`

The process also exposes:

- `/metrics`
- `/log/show`

## Permanent vars API

Permanent variables are available over both the CLI and REST API.

REST endpoints:

- `GET /api/v1/admin/vars`
- `GET /api/v1/admin/vars/{name}`
- `PUT /api/v1/admin/vars/{name}`
- `DELETE /api/v1/admin/vars/{name}`

This is the correct API surface for current runtime settings that have been
migrated to permanent vars, such as `cluster_role` and the DynIP policy values.

## Configuration split

The API documentation needs to be read with the current migration state in mind:

- some runtime state now lives in permanent vars
- many other settings still come from server CLI/config values

Examples of values that are in vars today:

- `cluster_role`
- `dynip_enabled`
- `dynip_realm`
- DynIP TTL and request-policy limits

Examples of values still configured outside the API vars surface:

- HTTP bind address and TLS files
- DNS bind address and ports
- cluster transport certs and addresses
- logging and backup paths

## Quick probe examples

Set scanner-safe shell variables for the examples:

```sh
export NSBLAST_ADMIN_USER=admin
export NSBLAST_ADMIN_PASS='<admin-password>'
```

Version:

```sh
curl --user "$NSBLAST_ADMIN_USER:$NSBLAST_ADMIN_PASS" \
  http://127.0.0.1:8080/api/v1/version
```

List vars:

```sh
curl --user "$NSBLAST_ADMIN_USER:$NSBLAST_ADMIN_PASS" \
  http://127.0.0.1:8080/api/v1/admin/vars
```

Enable DynIP:

```sh
curl --user "$NSBLAST_ADMIN_USER:$NSBLAST_ADMIN_PASS" \
  -X PUT \
  -H 'Content-Type: application/json' \
  http://127.0.0.1:8080/api/v1/admin/vars/dynip_enabled \
  -d '{"value":true}'
```
