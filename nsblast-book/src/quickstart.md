# Setting up a simple cluster with one primary and one follower

Linux and docker must be installed on the server.

## Setting up the primary

Get the docker image.

```sh
docker pull jgaafromnorth/nsblast
```

**Create TLS certificates** for the gRPC server. These are
self-signed certs. Substitute `ns1.nsblast.com` with
the fqdn of your server.

```sh
mkdir -p nsblast/tls
docker run --user $UID --rm -it -v $(pwd)/nsblast/tls:/certs jgaafromnorth/nsblast \
    --create-cert-subject ns1.nsblast.com \
    --create-certs-num-clients 1 \
    --create-certs-path /certs
```

Now we should have a CA root cert, server cert and key and cert and key for one client.
The client is the follower server.

You can of cource also use openssl and create proper self signed certificates
if you prefer that. The command above is added for convenience.

**Bootstrap the server***

Create a shared secret for the cluster. This is a password used by
the follower to autheticate itself with the server.

```sh
dd if=/dev/random bs=256 count=1 | base64  > nsblast/cluster.secret
```

Nsblast will by default run as user 999. Make that user the owner of the `nsblast/` directory.

```sh
sudo chown -R 999:root nsblast/
```

Now, start the server in the bacground with automatic restart. This makes
sure that the server starts automatically when the server starts up and
if the nextappd application crash.

```sh
docker run --restart=always -d --name nsblast -v $(pwd)/nsblast:/var/lib/nsblast --network host jgaafromnorth/nsblast -C debug --backup-path /var/lib/nsblast/backup --hourly-backup-interval 6 --cluster-role primary --cluster-auth-key /var/lib/nsblast/cluster.secret --cluster-server-cert /var/lib/nsblast/tls/server1-cert.pem --cluster-server-key /var/lib/nsblast/tls/server1-key.pem --cluster-ca-cert /var/lib/nsblast/tls/ca-cert.pem --dns-endpoint 0.0.0.0 --dns-enable-ixfr 0 --default-nameserver ns1.nsblast.com --default-nameserver ns2.nsblast.com --with-swagger --http-endpoint 0.0.0.0 --http-port 443 --http-tls-key /var/lib/nsblast/tls/certbot/privkey1.pem --http-tls-cert /var/lib/nsblast/tls/certbot/fullchain1.pem  --cluster-server-address 0.0.0.0:10123
```

Now, before we can use the API from a remote location, we need to get a TLS certificate.
If we want to self-host the dommain for the nameserver, that means that
we need to configure the server locally or on a secure network, as the api interface
is currently running unencrypted on port 80.

Lets start by setting up netrc so we can use curl without entering the *admin* password in
clear text. The admin password was generated when the server was started the first time. It is
now stored in `nsblast/password.txt`.

```sh
echo -n "machine 127.0.0.1 login admin password" > .netrc
cat nsblast/password.txt  >> .netrc
```

We can now verify that the credential is OK by asking the server for it's version.

```sh
curl --netrc http://127.0.0.1/api/v1/version
```

That should return a json payload like this:

```json
{"rcode":200,"error":false,"message":"","value":{"app":"nsblast","version":"0.1.0","Boost":"1_83","RocksDB":"8.9.1","C++ standard":"C++23","Platform":"linux","Compiler":"GNU C++ version 14.0.1 20240412 (experimental) [master r14-9935-g67e1433a94f]","Build date":"Jul 12 2024"}}
```

Now, lets set the server up for use. In this example I will configure `ns1.nsblast.com`. You can substitute that with whatever server you are configuring.

```sh
echo "Creating the zone."

curl --netrc -X 'POST' \
  'http://127.0.0.1/api/v1/zone/nsblast.com?kind=brief' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "soa": {
    "mname": "ns1.nsblast.com",
    "rname": "jgaa.jgaa.com"
  }
}'

echo "Creating the A record for ns1"

curl --netrc -X 'PUT' \
  'http://127.0.0.1/api/v1/rr/ns1.nsblast.com' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "a": [
    "139.162.132.207"
  ]
}'

echo "Creating the A record for ns2"

curl --netrc -X 'POST' \
  'http://127.0.0.1/api/v1/rr/ns2.nsblast.com' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "a": [
    "172.105.103.206"
  ]
}'

echo "Creating ns records for the zone"

curl --netrc -X 'PATCH' \
  'http://127.0.0.1/api/v1/rr/nsblast.com' \
  -H 'accept: application/json' \
  -H 'Content-Type: application/json' \
  -d '{
  "ns": [
    "ns1.nsblast.com", "ns2.nsblast.com"
  ]
}'

echo "List the rr's we have created"

curl --netrc -X 'GET' \
  'http://127.0.0.1/api/v1/zone/nsblast.com?limit=100&kind=verbose' \
  -H 'accept: application/json'

```

At this point you can also verify that the nameserver is serving the
domain.

```sh
dig ns1.nsblast.com @<IP to your server>
```

## Setting up the follower.

Copy the cluster.secret, CA cert and client-sert to nsblast on the secondary server.

Then prepare to start the follower server.

```sh
docker pull jgaafromnorth/nsblast

docker run -it --rm --name nsblast -v $(pwd)/nsblast:/var/lib/nsblast -p 80:80 -p 53:53 -p 53:53/udp --add-host ns1.nsblast.com:139.162.132.207 jgaafromnorth/nsblast -C debug --backup-path /var/lib/nsblast/backup --hourly-backup-interval 6 --cluster-role follower --cluster-auth-key /var/lib/nsblast/cluster.secret --cluster-server-cert /var/lib/nsblast/tls/client1-cert.pem --cluster-server-key /var/lib/nsblast/tls/client1-key.pem --cluster-ca-cert /var/lib/nsblast/tls/ca-cert.pem --dns-endpoint 0.0.0.0 --dns-enable-ixfr 0 --default-nameserver ns1.nsblast.com --default-nameserver ns2.nsblast.com --http-port 80 --cluster-server-address ns1.nsblast.com:10123

```


