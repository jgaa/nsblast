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
|AFSDB | Location of database servers of an AFS cell.|1183|pending|
|CAA   | DNS Certification Authority Authorization, constraining acceptable CAs for a host/domain |6844|pending|
|CDNSKEY|Child copy of DNSKEY record, for transfer to parent |7344|pending|
|CDS   | Child copy of DS record, for transfer to parent |7344|pending|
|CERT  | Stores PKIX, SPKI, PGP, etc. |4398|pending|
|CNAME | (Canonical Name) Record: Specifies an alias for a domain name.|1035|implemented|
|CSYNC | Specify a synchronization mechanism between a child and a parent DNS zone.|7477|pending|
|DHCID | DHCP identifier|4701|pending|
|DLV   | For publishing DNSSEC trust anchors outside of the DNS delegation chain.|4431|pending|
|DNAME | Delegation name record. Alias for a name and all its subnames, unlike CNAME, which is an alias for only the exact name.|6672|pending|
|DNSKEY| (DNS Security Key) Record: Contains a public key used for DNSSEC authentication.|4034|pending|
|DNSKEY| The key record used in DNSSEC. |4034|pending|
|DS    | (Delegation Signer) Record: Specifies the hash of a DNSKEY record for a child zone, used in DNSSEC.|4034|pending|
|EUI48 | MAC address (EUI-48)|7043|pending|
|EUI64 | MAC address (EUI-64)|7043|pending|
|HINFO | Host Information. Providing Minimal-Sized Responses to DNS Queries That Have QTYPE=ANY|8482|pending|
|HIP   | Host Identity Protocol. Method of separating the end-point identifier and locator roles of IP addresses. |8005| |
|IPSECKEY | IPsec Key |4025| |
|KEY   | Key record (DNSSEC)|2535 2930|pending|
|KX    | Key Exchanger record |2230|pending|
|LOC   | Specifies a geographical location associated with a domain name |1876|pending|
|MX    | Mail Exchange) Record: Specifies the mail exchange servers for a domain name.|1035|implemented|
|NAPTR | Naming Authority Pointer|3403|pending|
|NS    | (Name Server) Record: Specifies the authoritative name servers for a domain name.|1035|implemented|
|NSEC  | (Next-Secure) Record: Specifies the next domain name in a zone and indicates which resource record types exist or do not exist for that name, used in DNSSEC|4034|pending|
|NSEC3 | (Next-Secure 3) Record: Similar to NSEC but uses hashed domain names to provide better zone enumeration protection, used in DNSSEC|5155|pending|
|NSEC3PARAM| (NSEC3 Parameters) Record: Specifies the parameters used for NSEC3, used in DNSSEC|5155|pending|
|OPENPGPKEY| OpenPGP public key record|7929|pending|
|PTR   | (Pointer) Record: Maps an IP address to a domain name|1035|pending|
|RP    | Responsible Person|1183|pending|
|SMIMEA| Associates an S/MIME certificate with a domain name for sender authentication. |8162|pending|
|SOA   | (Start of Authority) Record: Specifies administrative information about a DNS zone, including the primary name server, contact email address, and other parameters|1035|implemented|
|SRV   | (Service) Record: Specifies the location of servers for specific services, such as SIP or XMPP|2782|pending|
|SSHFP | SSH Public Key Fingerprint |4255|pending|
|TLSA  | (Transport Layer Security Authentication) Record: Specifies the certificate or public key of a TLS server for a specific service, used for certificate pinning|6698|pending|
|TLSA  | TLSA certificate association |6698|pending|
|TXT   | (Text) Record: Contains text data associated with a domain name. Can be used for various purposes, such as domain verification or anti-spam measures.|1035|implemented|
|URI   | Uniform Resource Identifier. Can be used for publishing mappings from hostnames to URIs.|7553|pending|


Processing/other
| Name | Description                    | RFC | Status |
|------|--------------------------------|-----|--------|
|RRSIG | Holds a DNSSEC signature for a set of one or more DNS records with the same name and type. These signatures can be verified with the public keys stored in DNSKEY records|4034|pending|
|SIG   | Signature record used in SIG(0) (RFC 2931) and TKEY (RFC 2930). RFC 3755 designated RRSIG as the replacement for SIG for use within DNSSEC|2535|maybe|
|ZONEMD| Message Digests for DNS Zones|8976|pending|

Other types
- [ ] OPT
- [X] AXFR
- [ ] IXFR

# Internals
- [ ] Otimize the resolver to process related questions in in one loop (for example A and AAAA queries for the same fqdn)
- [ ] Handle the mname and NS entries for a new zone automatically
- [ ] See if a cache in front of rocksdb significantly speeds the server up.
- [x] Handle child zones during zone-transfers if we happen to own both.
- [ ] Handle parallel TCP requests. (Use task-based work-flow in stead of connection based?).

# Master DNS configuration
- [ ] Redirect API requests from slaves to master
- [ ] Write transaction log to rocksdb
- [ ] Allow slaves to subscribe to SSE for zones that change
- [ ] Allow zone transfers to slaves
- [ ] Allow slaves to query for a list of all zones and their soa version
- [ ] Call slave servers hat are on-line to get noitificatiosn when zones are up to date after a change. 

# Slave DNS 
- [x] Handle configuration-based Slave setup (interoperatibility with other servers)
  - [x] API endpoint to tell an instance that it is a slave for a specific zone and it's master's IP.
  - [x] AXFR client to fetch/refresh a slave zone
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
- [ ] Get an Entry as json
- [ ] Delete a RR type from a fqdn
- [ ] DDNS service to allow users to use the DNS for their home networks
- [ ] Import/export zones via a standard DNS zone file

# System
- [ ] Backup (user rocksdb's built-in backup)
- [ ] Restore (user rocksdb's built-in restore)
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

# UI
- [ ] Web CRUD for own zones
- [ ] Web signup

# Unit tests
- [ ] Merge - PATCH rr: (add rr, overwrite rr, delete rr?) and check that nothing else cnahges, except the soa serial

# Functional tests
- [ ] Functional tests for the API (python)
- [ ] Functional tests for the DNS interface (see if we can re-use tests for other open source DNS servers)

# Performance tests
- [ ] See what other open source DNS servers use/do

# Design
- [ ] How to handle authentication. What to put in nsblast, and what to put elsewhere.
- [ ] Signup work-flow. What goes in nsblast, what goes elsewhere and where is that?

# Tests
- [ ] Automatic replication to child zone using AXFR and timeout polling
- [ ] child-zones are excluded from AXFR
- [ ] AXFR for non-existant key returns NAME_ERROR
- [ ] AXFR for valid key inside a zone (but not the key for the SOA) returns NAME_ERROR
- [ ] AXFR request from client without access returns REFUSED
- [ ] AXFR returns the correct data for it's SOA version even when the zone is changed during the transfer.

# CI
- [x] Compile to docker image
