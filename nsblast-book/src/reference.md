
# Bootstrapping

The first time the server is started, it will go trough a "bootstrapping" procedure. It will create a new database. Then it will add a tenant with the name "nsblast" with all privileges available. Then it will create a user "admin" with all permissions. This is the system user for the administrator for the server. If the environment variable `NSBLAST_ADMIN_PASSWORD` is set, the user will be initialized with the password set to the value of this setting. If no such variable is set, the server will generate a random password for the admin user and write it out in the servers database directory in a file named `password.txt`.

# Resetting the admin password.

if the admin password is lost, or the admin user deleted, the server can re-create it. If you start the server from the command line with the command-line argument `--reset-auth`, the server will delete the `nsblast` tenant and re-create it and its `admin` user. All roles, API keys, and users for the `nsblast` user will be deleted. It's basically re-bootstrapping the system tenant. Zones owned by this tenant will not be affected. Other tenants will also not be affected.

The server must be shut down if you go trough this procedure.


# Environment variables

- NSBLAST_ADMIN_PASSWORD If set, the server will set this password for the system user `admin` for the `nsblast` tenant when the system is bootstrapped.

# Starting the server

## Running locally as an unprivileged user

The following command can be used to start the server locally as a normal user.
It will use port *8080* for the HTTP endpoint, and *5353* for the DNS endpoints (UDP and TCP).

```sh
nsblast -d /var/tmp/nsblast/master -l trace -C info -L /var/tmp/nsblast.log --dns-udp-port 5353 --dns-tcp-port 5353 --http-port 8080
```
