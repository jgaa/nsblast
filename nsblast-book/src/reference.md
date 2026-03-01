# Reference

## Bootstrap and startup lifecycle

`nsblast` no longer auto-bootstraps a new database on normal startup.

The current lifecycle is:

1. Create an empty database directory.
2. Run `nsblast bootstrap --cluster-role <primary|follower|none>`.
3. Start the server normally against that bootstrapped database.

If the database has not been bootstrapped, normal startup fails with:

```text
Database is not initialized at ... Run `nsblast bootstrap` first.
```

## Built-in system tenant

Bootstrap creates the built-in tenant:

- tenant: `nsblast`
- user: `admin`

That user is granted the full permission set.

## Admin password initialization

During `bootstrap` and `reset-auth`:

- if `NSBLAST_ADMIN_PASSWORD` is set and non-empty, that value becomes the
  password for `admin`
- otherwise a random password is generated and written to `password.txt` under
  the database directory

Outside those commands, `NSBLAST_ADMIN_PASSWORD` is ignored.

## Resetting auth

`reset-auth` is still supported:

```sh
nsblast --db-path /path/to/db reset-auth
```

It recreates the built-in `nsblast/admin` identity and system tenant content.
It does not require the database to be bootstrapped again.

## Permanent variables

The current permanent-variable interface is:

```sh
nsblast --db-path /path/to/db vars list
nsblast --db-path /path/to/db vars get <name>
nsblast --db-path /path/to/db vars set <name=value>
nsblast --db-path /path/to/db vars unset <name>
```

`vars set` and `vars unset` support `--force` for non-mutable variables.
`vars list` and `vars get` support `--json`.

### Current variable families

As of the current implementation, permanent variables cover:

- `cluster_role`
- `dynip_enabled`
- `dynip_realm`
- `dynip_max_hosts_per_root`
- `dynip_default_ttl`
- `dynip_min_ttl`
- `dynip_max_ttl`
- `dynip_allow_txt`
- `dynip_enable_get`
- `dynip_enable_post_json`
- `dynip_allow_partial_multi_host`
- `dynip_max_hosts_per_request`
- `dynip_require_user_agent`
- `dynip_allow_private_ips`

There are also internal vars such as:

- `schema_version`
- `pv_bootstrapped`

### Important scope note

Many runtime settings have not yet been migrated to permanent vars.

These still come from normal CLI/config-file settings, for example:

- HTTP endpoint, port, TLS cert/key, and thread count
- DNS endpoint, ports, and worker settings
- logging
- backup paths and scheduling
- cluster TLS files and cluster transport address
- replication queue thresholds

So the correct mental model today is:

- `cluster_role` and DynIP policy live in `vars`
- transport and process wiring still live in normal startup config

## Bootstrap-only arguments

These arguments are only valid with the `bootstrap` command:

- `--cluster-role`
- `--set name=value`

Passing `--cluster-role` on normal startup is rejected by the server.

## Selected environment variables

- `NSBLAST_ADMIN_PASSWORD`
  Used by `bootstrap` and `reset-auth` to set the built-in admin password.
- `NSBLAST_CLUSTER_AUTH_KEY`
  Alternative to `--cluster-auth-key`; the environment variable contains the
  shared secret itself as plaintext.
