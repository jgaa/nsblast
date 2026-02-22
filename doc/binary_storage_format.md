# nsblast Storage Format {#binary_storage_format}

This document describes the storage formats used by nsblast in RocksDB.

It covers:

- storage namespaces (column families)
- key encoding (`RealKey` classes)
- value encoding per namespace
- binary entry/diff record format
- account/tenant/user storage
- index structures and ordering
- byte order rules

## Overview

nsblast stores data in RocksDB using typed binary keys and namespace-specific values.

- Keys are prefixed with one byte identifying key class (`RealKey::Class`).
- Values are either:
  - nsblast RR binary blobs (for `entry` and `diff`), or
  - protobuf payloads / simple strings (for account and replication metadata).

## RocksDB Namespaces

nsblast uses these column families:

| Namespace | Category | Purpose |
|---|---|---|
| `default` | `DEFAULT` | Reserved RocksDB default namespace. Not used by nsblast application logic. |
| `masterZone` | `MASTER_ZONE` | Stores slave replication configuration (`pb::SlaveZone`). |
| `entry` | `ENTRY` | Primary DNS RR storage. |
| `diff` | `DIFF` | IXFR history per zone. |
| `account` | `ACCOUNT` | Tenant, user, zone metadata, and account indexes. |
| `trxlog` | `TRXLOG` | Replication transaction log (`pb::Transaction`). |

## Key Encoding (`RealKey`)

All keys begin with a 1-byte key class.

Some classes reverse the string payload for lexical grouping by zone suffix:

- reversed payload: `ENTRY`, `DIFF`, `ZONE`
- non-reversed payload: `TENANT`, `USER`, `TZONE`, `ZRR`, `TENANT_NAME`, `TRXID`

### Key Classes Used in Storage

| Key class | Encoded key shape | Typical namespace |
|---|---|---|
| `ENTRY` | `[class][reverse(fqdn)]` | `entry`, `masterZone` |
| `DIFF` | `[class][reverse(zone-fqdn)][0x00][serial-u32-be]` | `diff` |
| `TENANT` | `[class][tenant-id]` | `account` |
| `USER` | `[class][lowercase-login]` | `account` |
| `TENANT_NAME` | `[class][tenant-name]` | `account` |
| `ZONE` | `[class][reverse(zone-fqdn)]` | `account` |
| `TZONE` | `[class][tenant-id '/' fqdn]` | `account` |
| `ZRR` | `[class][zone '/' fqdn]` | `account` |
| `TRXID` | `[class][trxid-u64-be]` | `trxlog` |

Notes:

- DIFF key serial is appended as 32-bit big-endian after a zero byte separator.
- TRXID uses a big-endian `uint64_t` to preserve numeric order in key ordering.

## Value Formats by Namespace

| Namespace | Value format |
|---|---|
| `entry` | nsblast binary entry blob (header + optional tenant UUID + RR bytes + RR index) |
| `diff` | same binary blob format as `entry`, but ordered as IXFR sequence |
| `account` | mixed: protobuf (`pb::Tenant`, `pb::Zone`) and short string/sentinel values |
| `masterZone` | protobuf `pb::SlaveZone` |
| `trxlog` | protobuf `pb::Transaction` |

## Binary Entry Format (`entry` and `diff` values)

### Layout

```text
+-------------------------------+
| Header (8 bytes)              |
+-------------------------------+
| Tenant UUID (16 bytes, opt)   |
+-------------------------------+
| RR bytes...                   |
+-------------------------------+
| Index[rrcount]                |
+-------------------------------+
```

### Header (`StorageTypes::Header`)

Packed 8-byte structure:

```text
offset  size  field
0       1     version
1       1     flags
2       2     rrcount
4       1     labelsize
5       1     zonelen
6       2     ixoffset
```

Field semantics:

- `version`: storage format version (`CURRENT_STORAGE_VERSION`, currently `1`)
- `flags`: presence flags (`soa`, `ns`, `a`, `aaaa`, `cname`, `txt`, `tenantId`)
- `rrcount`: number of records and index entries
- `labelsize`: label byte length for first record name
- `zonelen`: zone length hint used when deriving zone SOA lookup from a non-SOA RR
- `ixoffset`: absolute offset to index table

### Optional Tenant UUID

If `flags.tenantId` is set, 16 raw UUID bytes follow header.

### RR Payload

Each RR is stored in DNS RR layout:

```text
NAME | TYPE(2) | CLASS(2) | TTL(4) | RDLENGTH(2) | RDATA
```

- First RR typically carries full NAME labels.
- Following RRs may use DNS compression pointers.

### RR Index Table

Each index entry is packed as:

```text
offset  size  field
0       2     type
2       2     offset
```

- `type`: RR type
- `offset`: absolute offset to RR start in blob

There is one index entry per RR (`rrcount` entries).

## Entry and Diff Ordering Rules

### `entry` namespace

By default, index entries are sorted by type-priority (lookup-optimized), so iteration is type-clustered.

Priority order:

1. `SOA`
2. `NS`
3. `A`
4. `AAAA`
5. `CNAME`
6. `MX`
7. `TXT`
8. other types

### `diff` namespace

Diff values use the same binary format but different builder policy:

- sorting disabled (`doSort(false)`)
- multiple SOA records allowed (`oneSoa(false)`)

This preserves IXFR semantic order (delete/add sequence boundaries).

## Account Storage Model (`account` namespace)

### Canonical tenant object

- key: `TENANT`
- value: serialized `pb::Tenant`

`pb::Tenant` embeds users, roles, permissions, and properties. Users are not stored as independent protobuf records keyed by user id.

### User lookup index

- key: `USER` with lowercase login name
- value: tenant id (string)

Used to enforce global login uniqueness and to resolve login -> tenant during BasicAuth.

Auth flow:

1. lowercase login name
2. read `USER` index -> tenant id
3. read `TENANT` object
4. locate user in `tenant.users[]`
5. verify stored hash using seed

### Additional account indexes

- `TENANT_NAME` -> tenant-id
- `ZONE` -> `pb::Zone`
- `TZONE` -> fqdn string (tenant/fqdn listing)
- `ZRR` -> sentinel/empty value (zone/fqdn ownership index)

## Master Zone Storage (`masterZone` namespace)

Slave replication settings are stored as:

- key: `ENTRY` `[class][reverse(fqdn)]`
- value: serialized `pb::SlaveZone`

## Transaction Log Storage (`trxlog` namespace)

Transaction records are stored as:

- key: `TRXID` `[class][id-u64-be]`
- value: serialized `pb::Transaction`

This key encoding preserves monotonic ordering by transaction id.

## Byte Order Rules

Unless otherwise stated, numerical fields in binary formats use network byte order (big-endian).

Big-endian fields include:

- entry header: `rrcount`, `ixoffset`
- entry index items: `type`, `offset`
- RR fixed fields: `TYPE`, `CLASS`, `TTL`, `RDLENGTH`
- RR numeric RDATA parts (SOA serial/refresh/retry/expire/minimum, MX priority, SRV fields, AFSDB subtype)
- DIFF key serial (`u32`)
- TRXID key id (`u64`)

Not covered by fixed-endian packing:

- tenant UUID in entry blob: raw 16 UUID bytes
- protobuf values in `account`, `masterZone`, `trxlog`: protobuf wire encoding

## Practical Lookup Notes

- `zonelen` in entry header is used to infer zone key from an RR key when resolving RR + zone SOA together.
- RR iteration is driven by the trailing index table, not by linear RR boundary scanning.
- DIFF iteration starts from `DIFF(zone, from_serial)` and scans forward in key order.
