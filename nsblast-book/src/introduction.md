# Introduction

nsBLAST is a massively scalable authoritative Domain Name Server (DNS).

It is written in C++ and designed to be memory and CPU efficient - because excessive memory or CPU usage is expensive (it consumes electric energy). Unlike a web-page written in JavaScript and running on some other persons laptop, cost is a concern for shard internet servers. An authoritative DNS server simply put a publicly available database where you map internet names (like `example.com`) that you owm, to an IP address. Some DNS servers will only serve a few, rarely used addresses, and in this case it don't matter much if it is written in C++, Python or Java. However, if your site is popular, or if you are hosting other peoples domains, things are different. You want the service to be fast and reliable. You also want to run it on cheap hardware or reasonable small VPS hosts, to keep your costs down.

nsBLAST can run on a Rasberry Pi II, serving a few domains and remain pretty performant for a handful of domains. It can also run on a VPS with 4 GB RAM, 4 VCPU's and serve 10 million resource records with 100k DNS requests per second (@TODO: Replace with actual measurements)

Unlike some DNS servers, nsBLAST is designed for the cloud. It has it's own fast and efficient replication mechanism to keep it's cluster of instances in sync. Any name-server can do that using the DNS protocol and some configuration, but the standard way is not very efficient. Especially for SAAS or DevOps use cases, where you may have thousands or even millions of resource records in a zone - or millions of zones! nsBLAST use lower level, direct database replication, via gRPC streams, to keep the zones in sync and the costs down.

nsBLAST is designed to be simple to install and use. It is a single binary that can be deployed as a Linux application or as a standard container. The binary contains everything: the DNS server, the REST API and the web based User Interface. There are two pre-built container images available, one with everything, and one with just the DNS server. If you run a cluster, you can save some RAM by deploying pure DNS instances that syncs against a primary server that also have the API (so you can make changes).

nsBLAST is designed for the cloud, and although the typical use is expected to be entities maintaining their own zones, it has everything you need to deploy it as a SaaS built in. The REST API's use RBAC for authorization of the individual operations. For authorization it support Tenants with their own unique Users, Roles and API keys.

Some DNS servers can use a database server or key value store for their data. nsBLAST use *RocksDB*, a battle-tested and quite efficient Open Source key-value store engine, maintained by Facebook. It's the actual core of a large number of famous database servers, stream engines (at the storage layer), caches and key value stores - the kind of things a DNS server may want to connect connect to in order to lookup and synchronize data. nsBLAST