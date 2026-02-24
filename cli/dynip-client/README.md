# nsblast-dynip

DynIP client for nsblast.

## Config

Default config path: `~/.config/nsblast-dynip/config.yaml`

Example:

```yaml
url: "https://dns.example.com"
username: "dynip-user"
password: "super-secret-password"
fqdn: "office.example.com"
repeat_minutes: 15
# optional
# client_ref: "router-01"
# lock_file: "/tmp/nsblast-dynip.lock"
# timeout_seconds: 10
# tls_ca_file: "/etc/ssl/certs/custom-ca.pem"
```

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
nsblast-dynip --config ~/.config/nsblast-dynip/config.yaml
```

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
  "hostname": "office.example.com",
  "client_ref": "router-01"
}
```

Response:

```json
{
  "status": "good",
  "changed": true,
  "effective_ip": "203.0.113.10",
  "hostname": "office.example.com"
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
