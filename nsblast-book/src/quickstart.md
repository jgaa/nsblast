# Quick Start

This chapter shows the current bootstrap and startup flow for a single local
`nsblast` instance.

The important change is that a new database must be bootstrapped explicitly
before the server can start serving DNS, HTTP, or gRPC. Bootstrap also writes
the initial permanent variables snapshot.

## Build

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

The server binary will normally be available as:

```text
build/bin/nsblast
```

## Bootstrap a new standalone database

Choose a database directory and an admin password:

```sh
export NSBLAST_ADMIN_PASSWORD='change-me'
DB_DIR=/tmp/nsblast-quickstart
mkdir -p "$DB_DIR"
```

Bootstrap the database once:

```sh
build/bin/nsblast \
  --db-path "$DB_DIR" \
  bootstrap \
  --cluster-role none
```

Notes:

- `bootstrap` aborts if the database already exists.
- `--cluster-role` is required for `bootstrap` and is persisted as the permanent
  variable `cluster_role`.
- `NSBLAST_ADMIN_PASSWORD` is only used during `bootstrap` and `reset-auth`.
- If `NSBLAST_ADMIN_PASSWORD` is unset, bootstrap generates a random password
  and writes it to `password.txt` under the database directory.

You can inspect the initialized permanent variables with:

```sh
build/bin/nsblast --db-path "$DB_DIR" vars list
```

## Start the server

Once bootstrapped, start the server normally:

```sh
build/bin/nsblast \
  --db-path "$DB_DIR" \
  --http-endpoint 127.0.0.1 \
  --http-port 8080 \
  --dns-endpoint 127.0.0.1 \
  --dns-udp-port 5353 \
  --dns-tcp-port 5353 \
  --with-swagger \
  --with-ui \
  --log-to-console info
```

This starts:

- the REST API on `http://127.0.0.1:8080/api/v1`
- Swagger on `http://127.0.0.1:8080/api/swagger` if built with swagger support
- the UI on `http://127.0.0.1:8080/ui` if built with UI support
- DNS on `127.0.0.1:5353` over UDP and TCP

If you try to start `nsblast` against a non-bootstrapped database, startup
fails with an error telling you to run `nsblast bootstrap` first.

## Verify the server

Check the version endpoint:

```sh
curl -u admin:change-me http://127.0.0.1:8080/api/v1/version
```

Create a zone:

```sh
curl -u admin:change-me \
  -X POST \
  -H 'Content-Type: application/json' \
  'http://127.0.0.1:8080/api/v1/zone/example.test?kind=brief' \
  -d '{
    "soa": {
      "mname": "ns1.example.test",
      "rname": "hostmaster.example.test"
    }
  }'
```

Add an `A` record:

```sh
curl -u admin:change-me \
  -X PUT \
  -H 'Content-Type: application/json' \
  'http://127.0.0.1:8080/api/v1/rr/ns1.example.test' \
  -d '{
    "a": ["127.0.0.1"]
  }'
```

Read the zone back:

```sh
curl -u admin:change-me \
  'http://127.0.0.1:8080/api/v1/zone/example.test?limit=100&kind=verbose'
```

And verify DNS:

```sh
dig @127.0.0.1 -p 5353 ns1.example.test
```

## Permanent vars versus CLI arguments

Today the runtime configuration is split across three places:

- bootstrap-only arguments: currently `bootstrap --cluster-role ...` and
  optional `bootstrap --set name=value`
- permanent variables: currently `cluster_role` and the DynIP-related settings
- normal CLI/config-file settings: HTTP, DNS, logging, backup, cluster TLS, and
  cluster transport addresses

That means:

- `cluster_role` is no longer a normal startup flag; it is set during bootstrap
  and later visible through `vars`
- DynIP enablement and limits are configured through `vars`, not normal server
  flags
- many other settings have not yet been migrated and still live in the normal
  command line or config file

Examples:

```sh
build/bin/nsblast --db-path "$DB_DIR" vars get cluster_role
build/bin/nsblast --db-path "$DB_DIR" vars set dynip_enabled=true
build/bin/nsblast --db-path "$DB_DIR" vars set dynip_realm=dynip.example.test
```

## Reset the built-in admin account

If the `nsblast/admin` system account needs to be recreated:

```sh
export NSBLAST_ADMIN_PASSWORD='new-password'
build/bin/nsblast --db-path "$DB_DIR" reset-auth
```

`reset-auth` keeps the existing zones and tenant data outside the system tenant,
but it recreates the built-in `nsblast` tenant and its `admin` user.
