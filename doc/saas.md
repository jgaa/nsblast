# SAAS support

## API keys and RBAC

**Role**
To keep things simple, a *Role* is attached to a vzone, and have the following permission attributes:
- name
- recursive, applies for virtual sub-zones
- create
- read
- update
- delete
- apikeys (create/delete)
- roles (create/update/delete)
- pattern (regex)
- types

The *pattern* and *types* can be used to restrict the access, for example to only allow access to keys relevant for letsencrypt TXT keys (type=TXT, pattern="_acme-challenge")

When a vzone is created, the role "admin" is created automatically.

## Features
- [ ] Define a fqdn as a virtual zone, for example `k8d.nsblast.com` with it's own API keys and RBAC.
- [ ] Virtual sob-zones
 - [ ] API for CRUD sub-zone's
 - [ ] Store data for the owner of a sub-zone
 - [ ] Maintain stats for the sub-zone (hourly, daily).
 - [ ] API to create API-Keys (store the hash of the API key, not the key itself).
 - [ ] RBACK for API keys
   - [ ] CRUD role
   - [ ] Create API key for role
   - [ ] Create API key for fill access to vzone (using default 'admin' role)

## Nice To Have
- [ ] Subzone with API key. For example `mail.nsblast.com`, where we can delegate access to a specific fqdn and sub-nodes so that a software package can update those entries safely (for example to configure the DNS zones, handle letsencrypt or other TLS certs etc.) from the install script for a mail server package. All without compromising the security of the parent zone. 

## Signup

The signup web-site is probably best implemented as a stand-alone app/container.

- [ ] Website where a user can sign up for their own sub-vzone (for example `jgaa.k8d.nsblast.com`).
 - [ ] Optional email verification
 - [ ] Optional, use OAuth to sign up (for example google, github).
 - [ ] Optional, set ttl for the vzone (delete if inactive for the duration of the ttl)
 - [ ] API to create the vzone.
 - [ ] API to check if a name is available
