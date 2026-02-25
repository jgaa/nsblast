# Disaster Recovery: Promote Follower DB to Primary

This document covers emergency recovery when a new primary is created from a raw copy of a follower RocksDB directory.

## Emergency repair command

Run on the promoted node (with nsblast service stopped):

```bash
nsblast --db-path <db_path> --repair-zone-indexes
```

What it does:

- Scans `ENTRY` and discovers zone apexes (entries containing SOA).
- Rebuilds `ACCOUNT` keys for `ZONE`, `TZONE`, and `ZRR`.
- Preserves existing `ZONE.id` when present.
- Uses tenant ID from entry tenant UUID when available.
- Falls back to the built-in `nsblast` tenant UUID when tenant ID is missing in an entry.
- Clears `TRXLOG` and emits a new synthetic full-state replication stream so followers can replay from the next transaction ids.

## Emit full sync stream only

If indexes are already correct and you only need a fresh replication event stream:

```bash
nsblast --db-path <db_path> --emit-full-sync-stream
```

What it does:

- Clears `TRXLOG`.
- Re-emits current database content as synthetic replication transactions.
- Starts ids at the next transaction id after the previous tip.
- Does not start DNS, HTTP, or gRPC services (CLI maintenance mode only).

## Replica full resync command

For follower/replica maintenance, you can force replication to restart from transaction id `0` and exit when the follower reports `IN_SYNC`:

```bash
nsblast --db-path <db_path> --cluster-role follower --full-resync
```

Behavior:

- Only valid when `--cluster-role=follower`.
- Runs replication-only maintenance flow and exits when sync is complete.
- Intended for replicas that need to replay available transaction history from the primary.

Limit:

- This uses replication transaction history. It is not a physical snapshot transfer.
- If required history is not available on the primary, use backup/restore or rebuild/reseed procedures instead.

## Recommended DR flow (primary loss)

1. Stop nsblast on the new primary candidate.
2. Restore/copy the follower RocksDB data to that node.
3. Run:
   `nsblast --db-path <db_path> --repair-zone-indexes`
4. Start nsblast on the new primary.
5. Repoint/restart followers toward the new primary.
6. Verify followers return to `IN_SYNC`.

Follower catch-up caveat:

- If a follower's local trx-id cannot be satisfied by the new primary stream, run follower maintenance:
  `nsblast --db-path <db_path> --cluster-role follower --full-resync`

## Verify after repair

1. `nsblast --db-path <db_path> --dump-zones /tmp/zones.json`
2. Check `zoneCount` and `orphanRrsetCount` in `/tmp/zones.json`.
3. Confirm Swagger/API zone listing works.
4. Confirm DNS answers still match expected records.

## Replication implications

`--repair-zone-indexes` emits a synthetic full-state stream in `TRXLOG` after rebuilding indexes. This is what allows reconnecting followers to catch up from their current position.

Operational guidance:

1. Run `--repair-zone-indexes` on the promoted primary before opening for normal traffic.
2. Existing followers should catch up automatically when they reconnect.
3. If a follower cannot catch up from its current trx-id, use `--full-resync` or reseed it from backup.
4. This command still does **not** reconstruct missing tenant/user/auth objects (`TENANT`, `USER`, `APIKEY`, `TENANT_NAME`, etc.) that are absent from the promoted DB. Restore those from backup if needed.

## Preferred recovery path

When available, use nsblast backup/restore as primary recovery method. Use raw DB copy + `--repair-zone-indexes` only as emergency fallback.
