# POC (Alfa)
Target: Deploy for internal use / testing

- [ ] Security
 - [ ] FLOOD protection (limit UDP/TCP connections)
 
- [ ] Usability
 - [ ] Finish RR API
 - [ ] DB level backup/restore

# MVP (Beta)
Target: Deploy for early SaaS users

- [ ] Usability
 - [ ] Json Backup/restore 
   - [ ] Subtree (zone, vzone)
   - [ ] Full (Zones, users, config)
 - [ ] Stats
  - [ ] To file / DB
  - [ ] To stream (gRPC)
  - [ ] To stream (generic, example with Pulsar)
  - [ ] Promethius

- [ ] UI
  - [ ] VZone UI
  - [ ] Org UI
  - [ ] Admin

- [ ] SaaS
 - [ ] Sign up
 - [ ] VZone
 - [ ] API keys
 - [ ] Examples
    - [ ] scripts creating certs from letsencrypt
    - [ ] Scripts creating zones
    - [ ] Script syncing IP for a RR/A record from home network
