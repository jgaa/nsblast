# nsblast-dynip

DynIP client for nsblast.

## Config

Default config path: `~/.config/nsblast-dynip/config.yaml`

Example:

```yaml
url: "https://dns.example.com"
token: "dynip-capability-token"
fqdn: "office.example.com"
repeat_minutes: 15
# optional explicit IP for current server versions that require it
# ip: "203.0.113.10"
# optional
# client_ref: "router-01"
# lock_file: "/tmp/nsblast-dynip.lock"
# timeout_seconds: 10
# tls_ca_file: "/etc/ssl/certs/custom-ca.pem"
```

`token` is the DynIP capability token returned when the host is provisioned.

Config file permissions must be owner-only (`0600`).

## Build

From `cli/`:

```sh
cargo build -p dynip-client
```

Binary:

```sh
./target/debug/nsblast-dynip --config ~/.config/nsblast-dynip/config.yaml
```

## One-shot usage

```sh
nsblast-dynip --config ~/.config/nsblast-dynip/config.yaml --ip 203.0.113.10
```

`--ip` is optional. If omitted, the server uses the HTTP peer IP address for the update.
The flag may be repeated syntactically, but the current server endpoint only supports one explicit IP per request, so dual-stack `A` + `AAAA` updates cannot be completed in a single invocation yet.

Exit codes:
- `0` success, no change
- `2` success, changed
- `3` auth/authz failure (`401`/`403`)
- `4` network/transient server failure (`429`/`500`/`503`)
- `5` other failure

## JSON request/response example

Request:

```json
{
  "fqdn": "office.example.com",
  "client_ref": "router-01"
}
```

Response:

```json
{
  "status": "good",
  "changed": true,
  "effective_ip": "203.0.113.10",
  "fqdn": "office.example.com"
}
```

## Daemon mode

```sh
nsblast-dynip --daemon --config ~/.config/nsblast-dynip/config.yaml --repeat-minutes 15
```

Optional hook script on changed IP:

```sh
nsblast-dynip --config ~/.config/nsblast-dynip/config.yaml --on-changed /usr/local/bin/on-ip-changed.sh
```

Hook environment variables:
- `NSBLAST_DYNIP_FQDN`
- `NSBLAST_DYNIP_PREV_IP` (if known)
- `NSBLAST_DYNIP_NEW_IP`

## systemd examples

Service (`/etc/systemd/system/nsblast-dynip.service`):

```ini
[Unit]
Description=nsblast DynIP updater
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/nsblast-dynip --config /etc/nsblast-dynip/config.yaml
User=nsblast
Group=nsblast
```

Timer (`/etc/systemd/system/nsblast-dynip.timer`):

```ini
[Unit]
Description=Run nsblast DynIP updater every 15 minutes

[Timer]
OnBootSec=45s
OnUnitActiveSec=15min
Persistent=true
Unit=nsblast-dynip.service

[Install]
WantedBy=timers.target
```

Enable:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now nsblast-dynip.timer
```
