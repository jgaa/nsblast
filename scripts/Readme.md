# Using with certbot

One of the motivations behind nsblast was to make it simple and secure
to use dns authentication with certbot (Let's encrypt).

Two scripts in this folder, `certbot-authenticator.sh` and `certbot-cleanup.sh`
can be used with the `certbot` command to create TLS certificates, including
wildcard certificates.

In order to use it securely, create a role for a specific fqdn with
permissions to create, list and delete RR's and to use the API.

Then add a nsblast user with just this role. This ensures
that if the credentials for this user is leaked, it cannot
change any other zones or subzones.

Now, create a `.netrc` file for this specific user. This allows
us run `curl` from the command-line without providing
or being asked for a password in an insecure manner. The
scrips use `curl` to set dns entries to prove to the
certificate authority that you have permissions to
change resource records in the dns zone you are requesting
certificates for.

To test that it works, you can run a command like this from this folder:

```sh
certbot certonly --test-cert --manual --preferred-challenges=dns \
    --manual-auth-hook ./certbot-authenticator.sh \
    --manual-cleanup-hook ./certbot-cleanup.sh \
    --config-dir /tmp/test-certs --work-dir /tmp/test-certs --logs-dir /tmp/test-certs \
    --agree-tos -m you@example.com -q \
    -d teste.some-zone.example.com -d '*.all.teste.some-zone.example.com' -d www.teste.some-zone.example.com
```

Note that the command above is not run as root, and use the `/tmp` directory for certbots files. It also
tells certbot to provide test certs (--test-cert). Don't do that when you request real certs.

There is also a wrapper script that calls *certbot* and requests your certs.

Example:
```sh
./certbot-wrapper.sh you@example.com -d '*.all.teste.some-zone.example.com' -d www.teste.some-zone.example.com

```

The wrapper is designed to run as a non-root user, and saves the certs and other files under `~/.certbot-files`.

