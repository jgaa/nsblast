# DNS server
- [x] Sockets / UDP
- [x] Sockets / TCP
- [x] Parse incoming messages
- [x] Compose replies to queries
- [x] Reply
- [ ] Logging of all or relevant requests


## RR types
Data types, stored in the DNS server
| Name | Description                    | RFC | Status |
|------|--------------------------------|-----|--------|
|A     | (Address) Record: Maps a domain name to an IPv4 address.|1035|implemented|
|AAAA  | (IPv6 Address) Record: Maps a domain name to an IPv6 address.|3596|implemented|
|AFSDB | Location of database servers of an AFS cell.|1183|implemented|
|CAA   | DNS Certification Authority Authorization, constraining acceptable CAs for a host/domain |6844|pending|
|CDNSKEY|Child copy of DNSKEY record, for transfer to parent |7344|pending|
|CDS   | Child copy of DS record, for transfer to parent |7344|pending|
|CERT  | Stores PKIX, SPKI, PGP, etc. |4398|pending|
|CNAME | (Canonical Name) Record: Specifies an alias for a domain name.|1035|implemented|
|CSYNC | Specify a synchronization mechanism between a child and a parent DNS zone.|7477|pending|
|DHCID | DHCP identifier|4701|implemented|
|DLV   | For publishing DNSSEC trust anchors outside of the DNS delegation chain.|4431|pending|
|DNAME | Delegation name record. Alias for a name and all its subnames, unlike CNAME, which is an alias for only the exact name.|6672|pending|
|DNSKEY| (DNS Security Key) Record: Contains a public key used for DNSSEC authentication.|4034|pending|
|DNSKEY| The key record used in DNSSEC. |4034|pending|
|DS    | (Delegation Signer) Record: Specifies the hash of a DNSKEY record for a child zone, used in DNSSEC.|4034|pending|
|EUI48 | MAC address (EUI-48)|7043|pending|
|EUI64 | MAC address (EUI-64)|7043|pending|
|HINFO | Host Information. Providing Minimal-Sized Responses to DNS Queries That Have QTYPE=ANY|8482|implemented|
|HIP   | Host Identity Protocol. Method of separating the end-point identifier and locator roles of IP addresses. |8005|pending/low priority|
|IPSECKEY | IPsec Key |4025|pending/low priority|
|KEY   | Key record (DNSSEC)|2535 2930|pending|
|KX    | Key Exchanger record |2230|pending|
|LOC   | Specifies a geographical location associated with a domain name |1876|maybe (experimental)|
|MX    | Mail Exchange) Record: Specifies the mail exchange servers for a domain name.|1035|implemented|
|NAPTR | Naming Authority Pointer|3403|pending/low priority|
|NS    | (Name Server) Record: Specifies the authoritative name servers for a domain name.|1035|implemented|
|NSEC  | (Next-Secure) Record: Specifies the next domain name in a zone and indicates which resource record types exist or do not exist for that name, used in DNSSEC|4034|pending|
|NSEC3 | (Next-Secure 3) Record: Similar to NSEC but uses hashed domain names to provide better zone enumeration protection, used in DNSSEC|5155|pending|
|NSEC3PARAM| (NSEC3 Parameters) Record: Specifies the parameters used for NSEC3, used in DNSSEC|5155|pending|
|OPENPGPKEY| OpenPGP public key record|7929|implemented|
|PTR   | (Pointer) Record: Maps an IP address to a domain name|1035|implemented|
|RP    | Responsible Person|1183|implemented|
|SMIMEA| Associates an S/MIME certificate with a domain name for sender authentication. |8162|experimental|
|SOA   | (Start of Authority) Record: Specifies administrative information about a DNS zone, including the primary name server, contact email address, and other parameters|1035|implemented|
|SRV   | (Service) Record: Specifies the location of servers for specific services, such as SIP or XMPP|2782|implemented|
|SSHFP | SSH Public Key Fingerprint |4255|pending|
|TLSA  | (Transport Layer Security Authentication) Record: Specifies the certificate or public key of a TLS server for a specific service, used for certificate pinning|6698|pending|
|TLSA  | TLSA certificate association |6698|pending|
|TXT   | (Text) Record: Contains text data associated with a domain name. Can be used for various purposes, such as domain verification or anti-spam measures.|1035|implemented|
|URI   | Uniform Resource Identifier. Can be used for publishing mappings from hostnames to URIs.|7553|pending|

* Experimental RR's are not a general priority at this time. 


Processing/other
| Name | Description                    | RFC | Status |
|------|--------------------------------|-----|--------|
|RRSIG | Holds a DNSSEC signature for a set of one or more DNS records with the same name and type. These signatures can be verified with the public keys stored in DNSKEY records|4034|pending|
|SIG   | Signature record used in SIG(0) (RFC 2931) and TKEY (RFC 2930). RFC 3755 designated RRSIG as the replacement for SIG for use within DNSSEC|2535|maybe|
|ZONEMD| Message Digests for DNS Zones|8976|pending|

Other types
- [x] OPT
- [X] AXFR
- [x] IXFR

# Important Todos
- [ ] TODO: Check if the caller is allowed to do AXFR!
- [ ] TODO: Validate that the client has access to this operation on this zone
- [ ] TODO: set up configuration for the session; idle time etc.
- [ ] TODO: Set reasonable defaults
- [ ] TODO: Add re-try loop in replication.
- [ ] TODO: Set up timer / util.cpp
- [ ] TODO: Add more tests with pointers


# Internals
- [ ] Otimize the resolver to process related questions in in one loop (for example A and AAAA queries for the same fqdn)
- [ ] Handle the mname and NS entries for a new zone automatically
- [ ] See if a cache in front of rocksdb significantly speeds the server up.
- [x] Handle child zones during zone-transfers if we happen to own both.
- [ ] Handle parallel TCP requests. (Use task-based work-flow in stead of connection based?).
- [ ] Add TCP keep-alive option (mentioned in RFC 5966)

# Security
- [ ] Limit the numb er of concurrent UDP connections
- [ ] Limit the numb er of concurrent TCP connections
- [x] Authenticate gRPC connections
- [ ] Authenticate / validate zone transfers using the DNS protocol
 - [ ] IP whitelist
- [ ] Add optional TLS for gRPC connections
 - [ ] Args in grpc init.
 - [ ] Self signed certs script for incoming connections/ automatic setup

# Master DNS configuration
- [ ] Redirect API requests from slaves to master
- [x] Write transaction log to rocksdb
- [ ] Simple Cluster / Efficient slave sync
    - [x] Write latest zone changes to rocksdb (key: type/sequenceid/soa-fqdn value: soa.serial)
    - [x] Replicate ENTRY changes at the db layer with gRPC
- [x] Allow zone transfers to slaves

# Slave DNS (legacy)
- [x] Handle configuration-based Slave setup (interoperatibility with other servers)
  - [x] API endpoint to tell an instance that it is a slave for a specific zone and it's master's IP.
  - [x] AXFR client to fetch/refresh a slave zone
  
# Slave DNS simple cluster
- [ ] Replication
    - [ ] Sync with master on startup
    - [ ] Disable DNS engine until DB is in sync (streaming)
    - [ ] Enable DNS engine when in sync
    - [ ] Option: Disable DNS engine if/when DB gets out of synch (or remove authoritive flag)
    
# SaaS
- [ ] API keys
    - [ ] CRUD
- [ ] VZone
    - [ ] CRUD
    - [ ] Internal data entry (in Entries?)
    - [ ] Stats (for security, billing)

# API
- [x] Get an Entry as json
- [x] Delete a RR type from a fqdn
- [ ] DDNS service to allow users to use the DNS for their home networks
- [ ] Import/export zones via a standard DNS zone file
- [ ] Import/export zones via a json file

# System
- [ ] System Backup to json file
- [ ] System Restore from json file
- [ ] System Backup (use rocksdb's built-in backup)
- [ ] System Restore (use rocksdb's built-in restore)

- [ ] Make sure rocksdb's performance analytics works
- [ ] Allow changing the log-level from the API
- [ ] Add Yaml config file
  - [ ] List of DNS IP's to listen to their port and protocll (UDP/TCP)
  - [ ] List of HTTP IP's to listen to with their port and protocol (http/https) and key/pubkey
  - [ ] Other options that are available on the command-line today
- [ ] Statistics. 
    - [ ] Define what to measure
    - [ ] Measure the required metrics internally.
    - [ ] Interface with Grafana (if it's still dominating when we get to this)

# Advanced clustering
- [ ] Add support for Apache Pulsar (or similar) to send changes and confirmations between servers
- [ ] Allow servers to be masters for some zones and slaves for others. (Sharding and faster access for regional users). 
- [ ] Allow transparent sharding with master/slave selection based on for example zone. 
- [ ] Caching server that use the notifications the stream to instantly invalidate zones that have changed.
- [ ] Fail-over for master. Zookeeper?

# UI
- [ ] Web CRUD for own zones
- [ ] Web signup

# Unit tests
- [ ] Merge - PATCH rr: (add rr, overwrite rr, delete rr?) and check that nothing else cnahges, except the soa serial
- [ ] CRUD

# Functional tests
- [x] Functional tests for the API (python)
- [x] Functional tests for the DNS interface (see if we can re-use tests for other open source DNS servers)

# Performance tests
- [ ] See what other open source DNS servers use/do

# Design
- [x] How to handle authentication. What to put in nsblast, and what to put elsewhere.
- [ ] Signup work-flow. What goes in nsblast, what goes elsewhere and where is that?

# Tests
- [x] Automatic replication to child zone using AXFR and timeout polling
- [ ] child-zones are excluded from AXFR
- [ ] AXFR for non-existant key returns NAME_ERROR
- [ ] AXFR for valid key inside a zone (but not the key for the SOA) returns NAME_ERROR
- [ ] AXFR request from client without access returns REFUSED
- [ ] AXFR returns the correct data for it's SOA version even when the zone is changed during the transfer.
- [ ] Long-runnig tests / 48 hours / Simple Cluster
 - [ ] Auto Backup
 - [ ] Backup
 - [ ] Restore
 - [ ] Many zones
 - [ ] Many RR's
 - [ ] Many tenants, users, zones, rr's
 - [ ] High-freqnency DNS lookups for existing records in existing zones
 - [ ] High-freqnency DNS lookups for non-existing records and zones
 - [ ] High-frequency add RR's and zones via API (10000 siumultaneous connections)
 - [ ] Deletes

# CI
- [x] Compile to docker image
- [ ] Automated CI pipeline
 - [ ] build
 - [ ] run unit-tests
 - [ ] run functional tests
 - [ ] push docker-image somewhere

# Tools
- [ ] Load testing tool for API
- [ ] Load testing tool for DNS records

