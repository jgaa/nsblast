# DynIP Client

`nsblast-dynip` is the Rust DynIP client for nsblast.

It is intended for lightweight dynamic address updates against the primary DynIP
JSON endpoint:

```text
/api/v1/dynip/update
```

The client uses capability-token authentication and is separate from the
general-purpose operator CLI.

## Location

The client lives in:

- `cli/dynip-client`

Build it with:

```sh
cd cli
cargo build -p dynip-client
```

The resulting binary is:

```sh
target/debug/nsblast-dynip
```

## Configuration

The default config path is:

```text
~/.config/nsblast-dynip/config.yaml
```

Typical config:

```yaml
url: "http://127.0.0.1:8080"
token: "<dynip capability token>"
fqdn: "router.office.dynip.example.net"
timeout_seconds: 10
```

The config file must be owner-only. The client rejects broader permissions.

## Authentication

The client uses:

- `Authorization: Bearer <token>`

The token is the DynIP capability token returned when the host is provisioned.

The server also remains backward compatible with legacy DynIP clients and router
integrations, but `nsblast-dynip` targets the primary JSON API and Bearer auth.

## Address updates

The client updates one fqdn at a time.

For current server behavior, explicit IP mode is supported with repeated
`--ip` flags, but the server currently accepts only one explicit IP per update
request. That means:

- one explicit `--ip` works
- multiple `--ip` values are rejected by the client for now

Example:

```sh
nsblast-dynip --config ~/.config/nsblast-dynip/config.yaml --ip 203.0.113.10
```

Exit behavior:

- `2` for a successful change
- `0` for no change
- `3` for auth failure
- `4` for network failure
- `5` for other errors

## Daemon mode

The client can also run as a daemon and periodically repeat the update:

```sh
nsblast-dynip --config ~/.config/nsblast-dynip/config.yaml --daemon
```

It supports:

- periodic execution
- optional custom CA file
- lock file protection
- optional `on-changed` hook execution

## Acceptance coverage

The end-to-end acceptance harness for the DynIP client is documented in:

- [`acceptance_tests.md`](./acceptance_tests.md)
- [`../../specs/dynip-client-acceptance-test.md`](../../specs/dynip-client-acceptance-test.md)

That harness runs the real client against a real standalone nsblast container
and validates both success and failure paths.
