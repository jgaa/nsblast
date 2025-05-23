# nsblast
Massively scalable authoritative DNS server

# Why?
I needed a DNS server to make it easy to deploy stuff in Kubernetes with my own tool, [k8deployer](https://github.com/jgaa/k8deployer). The tool will generate all the required TLS certificates with Let's Encrypt and configure the ingress controller in Kubernetes automatically. This applies even for zones on local networks in the 192.168.*.* IP address space. That means that my tool need fast and reliable access to a DNS server so it can use Let's Encrypt DNS option for validating ownership of the domains.

I also needed something fun to work with after I ran into a severe work burn-out in 2022 that almost killed me. A DNS server was a perfect project for me to take on to re-gain my full energy, health and enthusiasm. 

# Design Goals
When I worked with a startup in 2022, we frequently bumped in to problems with the commercial DNS server provider the company was using (and paying a *lot* of cash for every month). When we exceeded 1 million host-names (fqdn's) we had reached our limit, and our tests failed. The number of production host-names, and host-names provided for QA and individual teams and developers were just enormous. Prior to this, I never even thought of a use-case for a DNS zone with more than a few hundred names. 

When I worked with DNS servers in the past (yea, I was kind of a part time sysadmin back in the days) I always ended up writing my own user interfaces to simplify my work. I would use a DNS server that supported a SQL database back-end and then wrote a web-app or real application to manage the zones in the database server. 

So, when I started on nsblast, I had a few requirements:
- Nsblast's primary role is to be the perfect Authoritative DNS server for me.
- It must be secure and reliable, following best practices and modern engineering principles. 
- It must be *very* easy to install, manage and use (because I'm lazy and like things that are simple, yet powerful).
- It must have a REST API that is intuitive and easy to use for developers. I choose REST over for example gRPC because it's very simple to use directly for all kinds of use-cases, including bash scripts. 
- It must support SaaS use-cases, where a user (for example "Alice") can sign up and create her own logical sub-zone (like alice.k8deployer.nsblast.com if k8deployer.nsblast.com is an existing zone for SaaS users). Then, Alice can manager a number of Resource Records in her sub-zone (or rather, k8deployer could do that for her and also create and deploy Let's Encrypt certificates for those host-names, giving her the opportunity to focus 100% on whatever she is making or experimenting with in the Kubernetes cluster). 
- It must be massively scalable. A server should be able to handle millions of real zones and tens of millions of Resource Records for each zone. 
- It must handle a huge number of DNS and API request per second on each instance. 
- It must be CPU, memory and energy-efficient. Cloud infrastructure is *expensive*. Ideally, it should run comfortably on a raspberry PI 3 for most real use-cases (developers, developer-teams or small or medium companies and organizations running their own DNS servers for fun or to save costs).

# Status
Under initial development.

MVP expected late 2024.

The POC requirements has grown significantly in the last months, as
I want the server to be cluster ready (one primary, any number of followers)
using direct database replication via streaming gRPC. 
I also want backup and restore and some other mature features ready
before I deploy the server to production myself.

DNSSEC will not be implemented in the MVP, but if people need it, it will follow soon.

# RFC compliance (MVP)

- [x] [RFC 1034](https://www.rfc-editor.org/rfc/rfc1034) DOMAIN NAMES - CONCEPTS AND FACILITIES
- [x] [RFC 1035](https://www.rfc-editor.org/rfc/rfc1035) DOMAIN NAMES - IMPLEMENTATION AND SPECIFICATION
- [x] [RFC 1123](https://www.rfc-editor.org/rfc/rfc1123) Requirements for Internet Hosts -- Application and Support
- [x] [RFC 1183](https://www.rfc-editor.org/rfc/rfc1183) New DNS RR Definitions (Rr, Asfdb)
- [x] [RFC 1995](https://www.rfc-editor.org/rfc/rfc1995) Incremental Zone Transfer in DNS
- [x] [RFC 1996](https://www.rfc-editor.org/rfc/rfc1996) A Mechanism for Prompt Notification of Zone Changes (DNS NOTIFY)
- [x] [RFC 2181](https://www.rfc-editor.org/rfc/rfc2181) Clarifications to the DNS Specification
- [x] [RFC 2782](https://www.rfc-editor.org/rfc/rfc2782) A DNS RR for specifying the location of services (DNS SRV)
- [x] [RFC 3596](https://www.rfc-editor.org/rfc/rfc3596) DNS Extensions to Support IP Version 6
- [x] [RFC 3597](https://www.rfc-editor.org/rfc/rfc3597) Handling of Unknown DNS Resource Record (RR) Types
- [x] [RFC 4701](https://www.rfc-editor.org/rfc/rfc4701) A DNS Resource Record (RR) for Encoding Dynamic Host Configuration Protocol (DHCP) Information (DHCID RR)
- [x] [RFC 5936](https://www.rfc-editor.org/rfc/rfc5936) DNS Zone Transfer Protocol (AXFR)
- [x] [RFC 6891](https://www.rfc-editor.org/rfc/rfc6891) Extension Mechanisms for DNS (EDNS(0))
- [x] [RFC 7766](https://www.rfc-editor.org/rfc/rfc7766) DNS Transport over TCP - Implementation Requirements
- [x] [RFC 7929](https://www.rfc-editor.org/rfc/rfc7929) DNS-Based Authentication of Named Entities (DANE) Bindings for OpenPGP
- [x] [RFC 8482](https://www.rfc-editor.org/rfc/rfc8482) Providing Minimal-Sized Responses to DNS Queries That Have QTYPE=ANY

*note*: Things in old RFC's that has later been obsoleted are ignored.

# Features
- Standards compliant, authoritative DNS server
  - Can ast as primary DNS server in a group of standard DNS servers
  - Can act as a follower DNS server in a group of standard DNS servers
- HTTP REST API
  - Optional swagger endpoint to document/experiment with the API
  - RBAC (Role Based Access Control)
- Support SaaS use cases with individual users grouped under organizations/tenants. 
- Simple Cluster mode
  - Allows a cluster of Nsblast server to work as a typical database cluster with one primary server for all writes, and any number of follower servers for reads.
  - Replication on the database layer via fast/memory efficient gRPC streams (no separate streaming components like Kafka or Pulsar).
  - In this mode, users of the REST API can wait until a change is propagated to all the servers before the request returns. Useful for example for letsencrypt wildchar certs or certs for fqdn's that resolve to private IP ranges.
- Uses RocksDB as it's internal database engine. This is one of the most battle-tested and fastest database engines available.
- Backup and restore at the database layer.
  - Incremental backups
  - Backups can be created, deleted, listed and verified from the REST API
  - Backups can be listed, verified and restored from the command-line. The server must be off-line in order to perform a restore.
  - Backups can easily be copied to/from remote storage via *rsync* or for example *restic*.

# Building
The project use CMake.

It uses C++ 20 features and require g++-13 / clang-15 or newer.

It require boost version 1.82 or newer.

Other dependencies that are handled automatically by CMake:

- logfault:  For logging
- yahat-cpp: Embedded HTTP server for the REST API interface
- rocksdb:   Database-engine

## Debian dependencies
```sh
sudo apt install googletest libgtest-dev protobuf-compiler libprotobuf-dev libicu-dev libsnappy-dev libssl-dev libz3-dev libgflags-dev libbz2-dev liblz4-dev libgrpc-dev libgrpc++-dev grpc-proto protobuf-compiler-grpc
```

Example on building the application (with custom built boost-library in /opt):
```sh
cd nsblast
mkdir build
cd build
cmake -DBOOST_ROOT=/opt/boost/boost_1_83_0 ..
make -j `nproc`
LD_LIBRARY_PATH=/opt/boost/boost_1_83_0/stage/lib/ ctest
LD_LIBRARY_PATH=/opt/boost/boost_1_83_0/stage/lib/ ./bin/nsblast --help
```

# Docker image

Building:
```sh
./build-docker-image.sh
```

If you want [logbt](https://github.com/mapbox/logbt) to be used to dump backtraces from crashes in the containers log:
```
./build-docker-image.sh --logbt

# With logbt, you need to set the coredump pattern, for example like this
sudo bash -c "echo '/tmp/logbt-coredumps/core.%p.%E' > /proc/sys/kernel/core_pattern"
```

**Official image**
You can also use the [official Docker image](https://hub.docker.com/repository/docker/jgaafromnorth/nsblast/general):
```sh
  docker pull jgaafromnorth/nsblast:latest
```

**Starting**:
The example use the local docker IP. You can substitute that with a machines real IP.

```sh
docker run --name nsblast --rm -it -p 172.17.0.1:53:5353/udp -p 172.17.0.1:53:5353/tcp -p 172.17.0.1:80:8080/tcp jgaafromnorth/nsblast -l trace --dns-udp-port 5353 --dns-tcp-port 5353 --http-port 8080 --dns-endpoint 0.0.0.0 --http-endpoint 0.0.0.0
```
