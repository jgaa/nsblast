

# DNS server
-[ ] Sockets / UDP
-[ ] Sockets / TCP
-[ ] Parse incoming messages
-[ ] Compose replies to queries
-[ ] Reply
-[ ] Logging of all or relevant requests

# Master DNS configuration
-[ ] Redirect API requests from slaves to master
-[ ] Write transaction log to rocksdb
-[ ] Allow slaves to subscribe to SSE for zones that change
-[ ] Allow zone transfers to slaves
-[ ] Allow slaves to query for a list of all zones and their soa version
-[ ] Call slave servers hat are on-line to get noitificatiosn when zones are up to date after a change. 

# Slave DNS configuration
-[ ] Startup procedure 
    -[ ] Subscribe to zone changes
    -[ ] Get a list of all zones and their current version (bootstrap)
    -[ ] Add optimization so we only get zones that have changed since we went off-line. 
    -[ ] Get all zones that are not up to date from the master
    -[ ] Make sure we have the latest soa version fro all zones
    -[ ] Tell the master server that we are ready and willing to provide SSE updates for zones we have updated.
    -[ ] Start answering requests
    -[ ] Reconnect to the master server when we lose the connection.

# API
-[ ] Delete a RR type from a fqdn
-[ ] DDNS service to allow users to use the DNS for their home networks

# UI
-[ ] Web signup
-[ ] Web CRUD for own zones

# Unit tests
-[ ] Merge - PATCH rr: (add rr, overwrite rr, delete rr?) and check that nothing else cnahges, except the soa serial

# Functional tests
-[ ] Functional tests for the API (python?)
-[ ] Functional tests for the DNS interface (see if we can re-use tests for other open source DNS servers)

# Design
-[ ] Signup work-flow. What goes in nsblast, what goes elsewhere and where is that?
