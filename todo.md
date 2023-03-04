# DNS server
- [x] Sockets / UDP
- [x] Sockets / TCP
- [x] Parse incoming messages
- [x] Compose replies to queries
- [x] Reply
- [ ] Logging of all or relevant requests
- [ ] Pending RR types
    - [ ] OPT
    - [X] AXFR
    - [ ] IXFR
    - [ ] DNSSEC types

# Internals
- [ ] Otimize the resolver to process related questions in in one loop (for example A and AAAA queries for the same fqdn)
- [ ] Handle the mname and NS entries for a new zone automatically
- [ ] See if a cache in front of rocksdb significantly speeds the server up.
- [ ] Handle child zones during zone-transfers if we happen to own both.
- [ ] Handle parallel TCP requests. (Use task-based work-flow in stead of connection based?).

# Master DNS configuration
- [ ] Redirect API requests from slaves to master
- [ ] Write transaction log to rocksdb
- [ ] Allow slaves to subscribe to SSE for zones that change
- [ ] Allow zone transfers to slaves
- [ ] Allow slaves to query for a list of all zones and their soa version
- [ ] Call slave servers hat are on-line to get noitificatiosn when zones are up to date after a change. 

# Slave DNS 
- [ ] Handle configuration-based Slave setup (interoperatibility with other servers)
  - [ ] API endpoint to tell an instance that it is a slave for a specific zone and it's master's IP.
  - [ ] AXFR client to fetch/refresh a slave zone
- [ ] Startup procedure 
    - [ ] Subscribe to zone changes
    - [ ] Get a list of all zones and their current version (bootstrap)
    - [ ] Add optimization so we only get zones that have changed since we went off-line. 
    - [ ] Get all zones that are not up to date from the master
    - [ ] Make sure we have the latest soa version for all zones
    - [ ] Tell the master server that we are ready and willing to provide SSE updates for zones we have updated.
    - [ ] Start answering requests
- [ ] Reconnect to the master server when we lose the connection.

# API
- [ ] Delete a RR type from a fqdn
- [ ] DDNS service to allow users to use the DNS for their home networks

# System
- [ ] Backup (user rocksdb's built-in backup)
- [ ] Restore (user rocksdb's built-in restore)
- [ ] Make sure rocksdb's performance analytics works
- [ ] Statistics. 
    - [ ] Define what to measure
    - [ ] Measure the required metrics internally.
    - [ ] Interface with Grafana (if it's still dominating when we get to this)

# Advanced clustering
- [ ] Add support for Apache Pulsar (or similar) to send changes and confirmations between servers
- [ ] Allow servers to be masters for some zones and slaves for others. (Sharding and faster access for regional users). 
- [ ] Allow transparent sharding with master/slave selection based on for example zone. 
- [ ] Caching server that use the notifications the stream to instantly invalidate zones that have changed.

# UI
- [ ] Web signup
- [ ] Web CRUD for own zones

# Unit tests
- [ ] Merge - PATCH rr: (add rr, overwrite rr, delete rr?) and check that nothing else cnahges, except the soa serial

# Functional tests
- [ ] Functional tests for the API (python)
- [ ] Functional tests for the DNS interface (see if we can re-use tests for other open source DNS servers)

# Performance tests
- [ ] See what other open source DNS servers use/do

# Design
- [ ] Signup work-flow. What goes in nsblast, what goes elsewhere and where is that?

# Tests
- [ ] child-zones are excluded from AXFR
- [ ] AXFR for non-existant key returns NAME_ERROR
- [ ] AXFR for valid key inside a zone (but not the key for the SOA) returns NAME_ERROR
- [ ] AXFR request from client without access returns REFUSED
- [ ] AXFR returns the correct data for it's SOA version even when the zone is changed during the transfer.
