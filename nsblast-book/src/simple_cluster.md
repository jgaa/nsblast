# Simple Cluster

This chapter documents the current bootstrap strategy for a primary/follower
cluster.

The key point is that cluster role is persisted in permanent vars during
bootstrap, while the replication transport settings still come from the normal
startup CLI/config file.

## What is persisted versus what is not

Persisted in vars:

- `cluster_role=primary`
- `cluster_role=follower`
- `cluster_role=none`

Still configured through normal startup settings:

- `--cluster-auth-key`
- `--cluster-server-cert`
- `--cluster-server-key`
- `--cluster-ca-cert`
- `--cluster-server-address`
- `--cluster-repl-agent-queue-size`

That split is intentional for now. The role is part of the bootstrapped
database state; the transport wiring is still process configuration.

## Bootstrap sequence

Bootstrap each node once, before normal startup:

Primary:

```sh
nsblast --db-path /srv/nsblast/primary bootstrap --cluster-role primary
```

Follower:

```sh
nsblast --db-path /srv/nsblast/follower1 bootstrap --cluster-role follower
nsblast --db-path /srv/nsblast/follower2 bootstrap --cluster-role follower
```

Do not pass `--cluster-role` during normal startup. It is rejected outside the
`bootstrap` command.

## Start the primary

Example:

```sh
nsblast \
  --db-path /srv/nsblast/primary \
  --cluster-auth-key /srv/nsblast/shared/cluster.secret \
  --cluster-server-cert /srv/nsblast/tls/server-cert.pem \
  --cluster-server-key /srv/nsblast/tls/server-key.pem \
  --cluster-ca-cert /srv/nsblast/tls/ca-cert.pem \
  --cluster-server-address 0.0.0.0:10123 \
  --http-endpoint 0.0.0.0 \
  --http-port 8080 \
  --dns-endpoint 0.0.0.0
```

The primary serves writes and exposes the gRPC replication service.

## Start a follower

Example:

```sh
nsblast \
  --db-path /srv/nsblast/follower1 \
  --cluster-auth-key /srv/nsblast/shared/cluster.secret \
  --cluster-server-cert /srv/nsblast/tls/client-cert.pem \
  --cluster-server-key /srv/nsblast/tls/client-key.pem \
  --cluster-ca-cert /srv/nsblast/tls/ca-cert.pem \
  --cluster-server-address primary.example.net:10123 \
  --disable-http-server
```

For followers, `--cluster-server-address` points to the primary gRPC address.

## Operational notes

- All nodes in the cluster must use the same cluster auth secret.
- Followers must trust the CA that signed the primary gRPC certificate.
- The role used at runtime comes from the bootstrapped vars snapshot, not from
  startup flags.
- Changing a node from follower to primary is not a normal startup toggle; it is
  a database/bootstrap concern.

## DynIP and cluster mode

DynIP runtime policy is also driven by permanent vars. In a cluster that means:

- enable DynIP on the primary through `vars`
- let the vars snapshot replicate to followers
- keep the transport, TLS, and addresses configured through normal startup
  settings

The acceptance harness in
`tests/acceptance/run_full_acceptance.sh` exercises this model with one primary,
two followers, explicit bootstrap, vars-based DynIP configuration, and ongoing
replication checks.
