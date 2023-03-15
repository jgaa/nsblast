# nsblast
Massively scalable authoritative DNS server

## Status
Under initial development.
MVP expected by spring 2023


## RFC compliance (MVP)

- [x] [RFC 1034](https://www.rfc-editor.org/rfc/rfc1034) DOMAIN NAMES - CONCEPTS AND FACILITIES
- [x] [RFC 1035](https://www.rfc-editor.org/rfc/rfc1035) DOMAIN NAMES - IMPLEMENTATION AND SPECIFICATION
- [ ] [RFC 1123](https://www.rfc-editor.org/rfc/rfc1123) Requirements for Internet Hosts -- Application and Support
- [x] [RFC 1183](https://www.rfc-editor.org/rfc/rfc1183) New DNS RR Definitions (Rr, Asfdb)
- [ ] [RFC 1995](https://www.rfc-editor.org/rfc/rfc1995) Incremental Zone Transfer in DNS
- [ ] [RFC 2181](https://www.rfc-editor.org/rfc/rfc2181) Clarifications to the DNS Specification
- [x] [RFC 2782](https://www.rfc-editor.org/rfc/rfc2782) A DNS RR for specifying the location of services (DNS SRV)
- [x] [RFC 3596](https://www.rfc-editor.org/rfc/rfc3596) DNS Extensions to Support IP Version 6
- [ ] [RFC 3597](https://www.rfc-editor.org/rfc/rfc3597) Handling of Unknown DNS Resource Record (RR) Types
- [x] [RFC 5936](https://www.rfc-editor.org/rfc/rfc5936) DNS Zone Transfer Protocol (AXFR)
- [ ] [RFC 6891](https://www.rfc-editor.org/rfc/rfc6891) Extension Mechanisms for DNS (EDNS(0))
- [ ] [RFC 7766](https://www.rfc-editor.org/rfc/rfc7766) DNS Transport over TCP - Implementation Requirements
- [x] [RFC 8482](https://www.rfc-editor.org/rfc/rfc8482) Providing Minimal-Sized Responses to DNS Queries That Have QTYPE=ANY

*note*: Things in old RFC's that has later been obsoleted are ignored.

## Docker image

Building:
```sh
./build-docker-image.sh
```

Starting:
The example use the local docker IP. You can substitute that with a machines real IP.

```sh
docker run --name nsblast --rm -it -p 172.17.0.1:53:5353/udp -p 172.17.0.1:53:5353/tcp -p 172.17.0.1:80:8080/tcp lastviking/nsblast -l trace --dns-udp-port 5353 --dns-tcp-port 5353 --http-port 8080 --dns-endpoint 0.0.0.0 --http-endpoint 0.0.0.0
```
