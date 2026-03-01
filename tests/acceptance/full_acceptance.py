#!/usr/bin/env python3
import argparse
import concurrent.futures
import datetime as dt
import json
import os
import pathlib
import random
import re
import shutil
import statistics
import string
import subprocess
import sys
import time
import traceback
import uuid
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import dns.resolver
import requests


PRIMARY_API_PORT = 18080
PRIMARY_DNS_PORT = 15354
FOLLOWER1_HTTP_PORT = 18081
FOLLOWER2_HTTP_PORT = 18082
FOLLOWER1_DNS_PORT = 15355
FOLLOWER2_DNS_PORT = 15356
GRPC_PORT = 10123

PRIMARY_NAME = "nsb-1"
FOLLOWER1_NAME = "nsb-2"
FOLLOWER2_NAME = "nsb-3"

DEFAULT_IMAGE_CANDIDATES = [
    "ghcr.io/jgaa/nsblast:latest",
    "nsblast:latest",
    "jgaafromnorth/nsblast:latest",
]


class AcceptanceError(RuntimeError):
    pass


@dataclass
class TenantInfo:
    tenant_id: str
    tenant_name: str
    admin_user: str
    admin_pass: str
    zones: List[str]


@dataclass
class DynIpHostInfo:
    tenant_id: str
    tenant_user: str
    tenant_pass: str
    root: str
    host: str
    fqdn: str
    token: str


class Runner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.seed = args.seed if args.seed is not None else random.randint(1, 2**31 - 1)
        self.rng = random.Random(self.seed)

        stamp = dt.datetime.utcnow().strftime("%Y%m%d-%H%M%S")
        self.run_id = f"full-acceptance-{stamp}-{self.seed}"

        self.acceptance_dir = pathlib.Path(__file__).resolve().parent
        self.artifact_root = self.acceptance_dir / "artifacts" / self.run_id
        self.runtime_root = self.acceptance_dir / "runtime" / self.run_id
        self.cert_dir = self.runtime_root / "certs"
        self.node_dirs = {
            PRIMARY_NAME: self.runtime_root / PRIMARY_NAME,
            FOLLOWER1_NAME: self.runtime_root / FOLLOWER1_NAME,
            FOLLOWER2_NAME: self.runtime_root / FOLLOWER2_NAME,
        }

        self.network = f"nsb-acceptance-{self.seed}-{os.getpid()}"
        self.cluster_secret = self._random_secret(64)
        self.admin_password = os.environ.get("NSBLAST_ADMIN_PASSWORD", "VerySecret")

        self.image = self._resolve_image()

        self.primary_api = f"http://127.0.0.1:{PRIMARY_API_PORT}/api/v1"
        self.primary_metrics = f"http://127.0.0.1:{PRIMARY_API_PORT}/metrics"
        self.follower_metrics = [
            f"http://127.0.0.1:{FOLLOWER1_HTTP_PORT}/metrics",
            f"http://127.0.0.1:{FOLLOWER2_HTTP_PORT}/metrics",
        ]

        self.containers = [PRIMARY_NAME, FOLLOWER1_NAME, FOLLOWER2_NAME]
        self.auth_admin = ("admin", self.admin_password)
        self.server_threads = max(12, args.parallelism * 4)
        self.http_threads = max(8, args.parallelism * 2)
        self.rocksdb_background_threads = max(4, min(8, args.parallelism))

        self.request_samples: List[dict] = []
        self.latencies: Dict[str, List[float]] = {
            "zone_rr_updates": [],
            "auth_updates": [],
            "dynip_updates": [],
        }
        self.failures: List[str] = []

        self.summary = {
            "seed": self.seed,
            "image": self.image,
            "zones": args.zones,
            "tenants": args.tenants,
            "dynip_tenants": args.dynip_tenants,
            "duration_min": args.duration_min,
            "wave_interval_sec": args.wave_interval_sec,
            "max_repl_lag_sec": args.max_repl_lag_sec,
            "parallelism": args.parallelism,
            "server_threads": self.server_threads,
            "http_threads": self.http_threads,
            "rocksdb_background_threads": self.rocksdb_background_threads,
            "zone_profiles": {},
            "private_ip_zones": 0,
            "rr_writes": 0,
            "auth_checks": {"allow_ok": 0, "deny_ok": 0},
            "dynip_hosts": 0,
            "waves": 0,
            "dynip_updates": 0,
            "follower_metrics_available": {},
        }

    def _random_secret(self, n: int) -> str:
        alphabet = string.ascii_letters + string.digits + "-_"
        return "".join(self.rng.choice(alphabet) for _ in range(n))

    def _resolve_image(self) -> str:
        if self.args.image:
            return self.args.image
        if self.args.image_tag:
            return f"ghcr.io/jgaa/nsblast:{self.args.image_tag}"

        for cand in DEFAULT_IMAGE_CANDIDATES:
            rc = subprocess.run(["docker", "image", "inspect", cand], capture_output=True)
            if rc.returncode == 0:
                return cand

        return DEFAULT_IMAGE_CANDIDATES[0]

    def log(self, msg: str) -> None:
        print(f"[{dt.datetime.utcnow().isoformat()}Z] {msg}", flush=True)

    def run_cmd(self, cmd: List[str], check: bool = True, capture: bool = True) -> subprocess.CompletedProcess:
        result = subprocess.run(
            cmd,
            check=False,
            text=True,
            capture_output=capture,
        )
        if check and result.returncode != 0:
            raise AcceptanceError(
                f"Command failed ({result.returncode}): {' '.join(cmd)}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
        return result

    def get_container_state(self, name: str) -> Tuple[bool, str]:
        r = subprocess.run(
            ["docker", "inspect", "-f", "{{.State.Running}}|{{.State.ExitCode}}|{{.State.Status}}", name],
            text=True,
            capture_output=True,
        )
        if r.returncode != 0:
            return False, "missing"
        txt = (r.stdout or "").strip()
        parts = txt.split("|")
        if len(parts) != 3:
            return False, txt or "unknown"
        running = parts[0].lower() == "true"
        return running, f"exit={parts[1]} status={parts[2]}"

    def prepare_dirs(self) -> None:
        self.runtime_root.mkdir(parents=True, exist_ok=True)
        self.runtime_root.chmod(0o777)
        self.cert_dir.mkdir(parents=True, exist_ok=True)
        self.cert_dir.chmod(0o777)
        for node_dir in self.node_dirs.values():
            node_dir.mkdir(parents=True, exist_ok=True)
            node_dir.chmod(0o777)

    def create_certs(self) -> None:
        self.log("Generating TLS certs with openssl")
        ca_key = self.cert_dir / "ca-key.pem"
        ca_cert = self.cert_dir / "ca-cert.pem"
        srv_key = self.cert_dir / "server-key.pem"
        srv_csr = self.cert_dir / "server.csr"
        srv_cert = self.cert_dir / "server-cert.pem"
        ext = self.cert_dir / "server-ext.cnf"

        ext.write_text(
            "subjectAltName=DNS:nsb-1,DNS:localhost,IP:127.0.0.1\n"
            "extendedKeyUsage=serverAuth\n",
            encoding="utf-8",
        )

        self.run_cmd(["openssl", "genrsa", "-out", str(ca_key), "2048"])
        self.run_cmd(
            [
                "openssl",
                "req",
                "-x509",
                "-new",
                "-nodes",
                "-key",
                str(ca_key),
                "-sha256",
                "-days",
                "365",
                "-subj",
                "/CN=nsblast-test-ca",
                "-out",
                str(ca_cert),
            ]
        )
        self.run_cmd(["openssl", "genrsa", "-out", str(srv_key), "2048"])
        self.run_cmd(
            [
                "openssl",
                "req",
                "-new",
                "-key",
                str(srv_key),
                "-subj",
                "/CN=nsb-1",
                "-out",
                str(srv_csr),
            ]
        )
        self.run_cmd(
            [
                "openssl",
                "x509",
                "-req",
                "-in",
                str(srv_csr),
                "-CA",
                str(ca_cert),
                "-CAkey",
                str(ca_key),
                "-CAcreateserial",
                "-out",
                str(srv_cert),
                "-days",
                "365",
                "-sha256",
                "-extfile",
                str(ext),
            ]
        )
        for p in (ca_key, ca_cert, srv_key, srv_csr, srv_cert, ext):
            try:
                os.chmod(p, 0o644)
            except Exception:
                pass

    def ensure_clean_containers(self) -> None:
        for name in self.containers:
            subprocess.run(["docker", "rm", "-f", name], capture_output=True)

    def create_network(self) -> None:
        self.run_cmd(["docker", "network", "create", self.network])

    def remove_network(self) -> None:
        subprocess.run(["docker", "network", "rm", self.network], capture_output=True)

    def bootstrap_node(self, name: str, role: str) -> None:
        self.log(f"Bootstrapping {name} as {role}")
        cmd = [
            "docker", "run", "--rm",
            "-e", f"NSBLAST_ADMIN_PASSWORD={self.admin_password}",
            "-v", f"{self.node_dirs[name]}:/var/lib/nsblast",
            self.image,
            "--db-path", "/var/lib/nsblast",
            "bootstrap",
            "--cluster-role", role,
        ]
        self.run_cmd(cmd)

    def start_containers(self) -> None:
        self.log("Starting primary and followers")

        common_env = [
            "-e", f"NSBLAST_ADMIN_PASSWORD={self.admin_password}",
            "-e", f"NSBLAST_CLUSTER_AUTH_KEY={self.cluster_secret}",
        ]

        primary_cmd = [
            "docker", "run", "-d", "--name", PRIMARY_NAME,
            "--network", self.network,
            "-p", f"127.0.0.1:{PRIMARY_API_PORT}:80",
            "-p", f"127.0.0.1:{PRIMARY_DNS_PORT}:53/udp",
            "-p", f"127.0.0.1:{PRIMARY_DNS_PORT}:53/tcp",
            *common_env,
            "-v", f"{self.node_dirs[PRIMARY_NAME]}:/var/lib/nsblast",
            "-v", f"{self.cert_dir}:/certs",
            self.image,
            "--db-path", "/var/lib/nsblast",
            "--dns-endpoint", "0.0.0.0",
            "--http-endpoint", "0.0.0.0",
            "--http-port", "80",
            "--http-num-threads", str(self.http_threads),
            "--dns-num-threads", str(self.server_threads),
            "--rocksdb-background-threads", str(self.rocksdb_background_threads),
            "--disable-metrics-auth",
            "--cluster-server-address", f"0.0.0.0:{GRPC_PORT}",
            "--cluster-ca-cert", "/certs/ca-cert.pem",
            "--cluster-server-cert", "/certs/server-cert.pem",
            "--cluster-server-key", "/certs/server-key.pem",
            "-C", self.args.node_log_level,
        ]
        self.run_cmd(primary_cmd)

        follower_specs = [
            (FOLLOWER1_NAME, FOLLOWER1_HTTP_PORT, FOLLOWER1_DNS_PORT),
            (FOLLOWER2_NAME, FOLLOWER2_HTTP_PORT, FOLLOWER2_DNS_PORT),
        ]
        for name, http_port, dns_port in follower_specs:
            cmd = [
                "docker", "run", "-d", "--name", name,
                "--network", self.network,
                "-p", f"127.0.0.1:{http_port}:80",
                "-p", f"127.0.0.1:{dns_port}:53/udp",
                "-p", f"127.0.0.1:{dns_port}:53/tcp",
                *common_env,
                "-v", f"{self.node_dirs[name]}:/var/lib/nsblast",
                "-v", f"{self.cert_dir}:/certs",
                self.image,
                "--db-path", "/var/lib/nsblast",
                "--dns-endpoint", "0.0.0.0",
                "--http-endpoint", "0.0.0.0",
                "--http-port", "80",
                "--http-num-threads", str(self.http_threads),
                "--dns-num-threads", str(self.server_threads),
                "--rocksdb-background-threads", str(self.rocksdb_background_threads),
                "--disable-metrics-auth",
                "--cluster-server-address", f"{PRIMARY_NAME}:{GRPC_PORT}",
                "--cluster-ca-cert", "/certs/ca-cert.pem",
                "-C", self.args.node_log_level,
            ]
            self.run_cmd(cmd)

    def wait_http_ok(self, url: str, timeout_sec: float, auth: Optional[Tuple[str, str]] = None) -> None:
        deadline = time.time() + timeout_sec
        while time.time() < deadline:
            try:
                r = requests.get(url, auth=auth, timeout=2)
                if r.status_code == 200:
                    return
            except Exception:
                pass
            time.sleep(0.5)
        raise AcceptanceError(f"Timed out waiting for HTTP endpoint: {url}")

    def wait_cluster_ready(self) -> None:
        self.log("Waiting for API and metrics readiness")
        self.wait_http_ok(f"{self.primary_api}/version", timeout_sec=120, auth=self.auth_admin)
        self.wait_http_ok(self.primary_metrics, timeout_sec=60)
        for i, m in enumerate(self.follower_metrics):
            name = FOLLOWER1_NAME if i == 0 else FOLLOWER2_NAME
            try:
                self.wait_http_ok(m, timeout_sec=5)
                self.summary["follower_metrics_available"][name] = True
            except Exception:
                self.summary["follower_metrics_available"][name] = False

        self.log("Verifying follower convergence with DNS probe")
        probe_zone = f"bootstrap-probe-{self.seed}.test"
        zone_payload = {
            "ttl": 60,
            "soa": {
                "refresh": 120,
                "retry": 60,
                "expire": 600,
                "minimum": 60,
                "mname": f"ns1.{probe_zone}",
                "rname": f"hostmaster.{probe_zone}",
            },
            "ns": [f"ns1.{probe_zone}", f"ns2.{probe_zone}"],
        }
        zr = self.api_request("POST", f"/zone/{probe_zone}", self.auth_admin, json=zone_payload)
        if zr.status_code not in (200, 201):
            raise AcceptanceError(f"Failed creating replication probe zone: {zr.status_code} {zr.text}")

        for host in (f"ns1.{probe_zone}", f"ns2.{probe_zone}"):
            rr = self.api_request("PUT", f"/rr/{host}", self.auth_admin, json={"a": [self._public_ipv4()]})
            if rr.status_code not in (200, 201):
                raise AcceptanceError(f"Failed creating replication probe NS host {host}: {rr.status_code} {rr.text}")

        probe_fqdn = f"ready.{probe_zone}"
        probe_ip = [self._public_ipv4()]
        rr = self.api_request("PUT", f"/rr/{probe_fqdn}", self.auth_admin, json={"a": probe_ip})
        if rr.status_code not in (200, 201):
            raise AcceptanceError(f"Failed creating replication probe RR: {rr.status_code} {rr.text}")
        self.wait_dns_values(probe_fqdn, "A", probe_ip, timeout_sec=120)

    def api_request(self, method: str, path: str, auth: Optional[Tuple[str, str]], **kwargs) -> requests.Response:
        url = f"{self.primary_api}{path}"
        params = kwargs.pop("params", {})
        timeout = kwargs.pop("timeout", 60)
        r = requests.request(method, url, auth=auth, params=params, timeout=timeout, **kwargs)

        if len(self.request_samples) < 300:
            entry = {
                "method": method,
                "path": path,
                "status": r.status_code,
                "body": r.text[:1200],
                "params": params,
            }
            self.request_samples.append(entry)

        return r

    def parse_enveloped(self, resp: requests.Response) -> dict:
        try:
            payload = resp.json()
        except Exception as ex:
            raise AcceptanceError(f"Invalid JSON response ({resp.status_code}): {resp.text[:800]}") from ex

        if isinstance(payload, dict) and "error" in payload:
            return payload
        return {"error": False, "value": payload}

    def set_var(self, name: str, value) -> None:
        r = self.api_request("PUT", f"/admin/vars/{name}", self.auth_admin, json={"value": value})
        if r.status_code != 200:
            raise AcceptanceError(f"Failed setting var {name}: {r.status_code} {r.text}")

    def configure_dynip_vars(self, realm: str) -> None:
        self.log("Configuring permanent vars for DynIP")
        self.set_var("dynip_realm", realm)
        self.set_var("dynip_enabled", True)
        self.set_var("dynip_min_ttl", 60)
        self.set_var("dynip_default_ttl", 300)
        self.set_var("dynip_max_ttl", 1800)
        self.set_var("dynip_max_hosts_per_root", 8)
        self.set_var("dynip_allow_txt", True)
        self.set_var("dynip_allow_private_ips", True)

    def get_permissions(self) -> List[str]:
        r = self.api_request("GET", "/permissions", self.auth_admin)
        if r.status_code != 200:
            raise AcceptanceError(f"Failed to fetch permissions: {r.status_code} {r.text}")
        payload = self.parse_enveloped(r)
        return payload.get("value", [])

    def create_tenants(self) -> List[TenantInfo]:
        self.log(f"Creating {self.args.tenants} tenants")
        all_permissions = self.get_permissions()

        tenants: List[TenantInfo] = []
        for i in range(self.args.tenants):
            tenant_id = str(uuid.uuid4())
            tenant_name = f"acc-{i:04d}-{self.seed}"
            admin_user = f"admin-{i:04d}@{tenant_name}.test"
            admin_pass = self._random_secret(24)

            payload = {
                "id": tenant_id,
                "name": tenant_name,
                "active": True,
                "allowedPermissions": all_permissions,
                "roles": [
                    {
                        "name": "tenant-admin",
                        "permissions": all_permissions,
                        "filter": {"fqdn": "", "recursive": True},
                    }
                ],
                "users": [
                    {
                        "name": admin_user,
                        "active": True,
                        "roles": ["tenant-admin"],
                        "auth": {"password": admin_pass},
                    }
                ],
            }

            r = self.api_request("POST", "/tenant", self.auth_admin, json=payload)
            if r.status_code not in (200, 201):
                raise AcceptanceError(f"Tenant creation failed: {r.status_code} {r.text}")

            tenants.append(TenantInfo(tenant_id, tenant_name, admin_user, admin_pass, []))

        return tenants

    def _choose_zone_distribution(self, tenant_count: int, total_zones: int) -> List[int]:
        weights = [1, 2, 3, 5, 8, 13]
        counts = [1 for _ in range(tenant_count)]
        remaining = max(0, total_zones - tenant_count)

        while remaining > 0:
            ix = self.rng.randrange(tenant_count)
            add = self.rng.choice(weights)
            add = min(add, remaining)
            counts[ix] += add
            remaining -= add

        return counts

    def _zone_payload(self, zone: str) -> dict:
        return {
            "ttl": 300,
            "soa": {
                "refresh": 120,
                "retry": 60,
                "expire": 3600,
                "minimum": 60,
                "mname": f"ns1.{zone}",
                "rname": f"hostmaster.{zone}",
            },
            "ns": [f"ns1.{zone}", f"ns2.{zone}"],
        }

    def _public_ipv4(self) -> str:
        return f"198.51.{self.rng.randint(0, 254)}.{self.rng.randint(1, 254)}"

    def _private_ipv4(self) -> str:
        pools = [
            (10, self.rng.randint(0, 255), self.rng.randint(0, 255), self.rng.randint(1, 254)),
            (172, self.rng.randint(16, 31), self.rng.randint(0, 255), self.rng.randint(1, 254)),
            (192, 168, self.rng.randint(0, 255), self.rng.randint(1, 254)),
        ]
        a, b, c, d = self.rng.choice(pools)
        return f"{a}.{b}.{c}.{d}"

    def _query_rrset(self, port: int, fqdn: str, rrtype: str) -> List[str]:
        resolver = dns.resolver.Resolver(configure=False)
        resolver.nameservers = ["127.0.0.1"]
        resolver.port = port
        resolver.timeout = 1.0
        resolver.lifetime = 2.0
        answers = resolver.resolve(fqdn, rrtype)

        vals = []
        for rr in answers:
            if rrtype == "TXT":
                vals.append("".join([b.decode("utf-8") for b in rr.strings]))
            else:
                vals.append(rr.to_text().strip('"'))
        return sorted(vals)

    def wait_dns_values(self, fqdn: str, rrtype: str, expected: List[str], timeout_sec: float) -> float:
        exp = sorted(expected)
        start = time.time()
        deadline = start + timeout_sec

        while time.time() < deadline:
            for follower in (FOLLOWER1_NAME, FOLLOWER2_NAME):
                running, detail = self.get_container_state(follower)
                if not running:
                    raise AcceptanceError(f"Follower container {follower} is not running ({detail})")
            ok = True
            for port in (FOLLOWER1_DNS_PORT, FOLLOWER2_DNS_PORT):
                try:
                    got = self._query_rrset(port, fqdn, rrtype)
                    if got != exp:
                        ok = False
                        break
                except Exception:
                    ok = False
                    break
            if ok:
                return time.time() - start
            time.sleep(0.2)

        raise AcceptanceError(f"DNS convergence timeout for {fqdn} {rrtype}; expected={exp}")

    def measure_and_enforce_lag(self, scenario: str, fqdn: str, rrtype: str, expected: List[str], t0: float) -> None:
        lag = self.wait_dns_values(fqdn, rrtype, expected, timeout_sec=max(2.0, self.args.max_repl_lag_sec * 4))
        elapsed = (time.time() - t0) if lag < 0 else lag
        self.latencies[scenario].append(elapsed)
        if elapsed > self.args.max_repl_lag_sec:
            raise AcceptanceError(
                f"Replication lag exceeded threshold: scenario={scenario} fqdn={fqdn} lag={elapsed:.3f}s "
                f"max={self.args.max_repl_lag_sec:.3f}s"
            )

    def create_zone_and_records_for_tenant(self, tenant: TenantInfo, zone: str, profile: str, private_zone: bool) -> None:
        params = {"tenant": tenant.tenant_id}

        t0 = time.time()
        r = self.api_request("POST", f"/zone/{zone}", self.auth_admin, params=params, json=self._zone_payload(zone))
        if r.status_code not in (200, 201):
            raise AcceptanceError(f"Zone create failed for {zone}: {r.status_code} {r.text}")

        ns_payload = {"a": [self._public_ipv4()]}
        for host in (f"ns1.{zone}", f"ns2.{zone}"):
            rr = self.api_request("PUT", f"/rr/{host}", self.auth_admin, params=params, json=ns_payload)
            if rr.status_code not in (200, 201):
                raise AcceptanceError(f"Failed creating NS A RR {host}: {rr.status_code} {rr.text}")
            self.summary["rr_writes"] += 1

        lag_probe_fqdn = f"ns1.{zone}"
        lag_probe_values = sorted(ns_payload["a"])

        if profile == "small":
            fqdn = f"small-{self.rng.randint(1, 9999)}.{zone}"
            values = [self._public_ipv4()]
            rr = self.api_request("PUT", f"/rr/{fqdn}", self.auth_admin, params=params, json={"a": values})
            if rr.status_code not in (200, 201):
                raise AcceptanceError(f"Failed creating {fqdn}: {rr.status_code} {rr.text}")
            self.summary["rr_writes"] += 1

        elif profile == "web":
            www = f"www.{zone}"
            mail = f"mail.{zone}"
            spf = f"v=spf1 a mx -all"
            dkim = f"v=DKIM1; k=rsa; p={self._random_secret(64)}"
            for fqdn, payload in [
                (www, {"a": [self._public_ipv4(), self._public_ipv4()]}),
                (mail, {"a": [self._public_ipv4()]}),
                (zone, {"mx": [{"priority": 10, "host": mail}], "txt": [spf, dkim]}),
            ]:
                rr = self.api_request("PATCH", f"/rr/{fqdn}", self.auth_admin, params=params, json=payload)
                if rr.status_code not in (200, 201):
                    raise AcceptanceError(f"Failed creating {fqdn}: {rr.status_code} {rr.text}")
                self.summary["rr_writes"] += 1

        elif profile == "backend":
            for name in ("api", "db", "cache", "mq"):
                fqdn = f"{name}.{zone}"
                ip = self._private_ipv4() if private_zone and name in ("db", "cache") else self._public_ipv4()
                rr = self.api_request("PUT", f"/rr/{fqdn}", self.auth_admin, params=params, json={"a": [ip]})
                if rr.status_code not in (200, 201):
                    raise AcceptanceError(f"Failed creating {fqdn}: {rr.status_code} {rr.text}")
                self.summary["rr_writes"] += 1

        else:
            for name in ("staging1", "staging2", "dev", "test"):
                fqdn = f"{name}.{zone}"
                ip = self._private_ipv4() if private_zone else self._public_ipv4()
                rr = self.api_request("PUT", f"/rr/{fqdn}", self.auth_admin, params=params, json={"a": [ip], "txt": ["env=mixed"]})
                if rr.status_code not in (200, 201):
                    raise AcceptanceError(f"Failed creating {fqdn}: {rr.status_code} {rr.text}")
                self.summary["rr_writes"] += 1

        self.measure_and_enforce_lag("zone_rr_updates", lag_probe_fqdn, "A", lag_probe_values, t0)

    def generate_and_apply_dataset(self, tenants: List[TenantInfo]) -> None:
        self.log("Generating and provisioning zones/RRs")
        distribution = self._choose_zone_distribution(len(tenants), self.args.zones)

        total_private_target = max(1, int(self.args.zones * 0.10))
        private_budget = total_private_target

        jobs = []
        zone_ix = 0
        profiles = ["small", "web", "backend", "mixed"]
        profile_weights = [25, 35, 20, 20]

        for tix, tenant in enumerate(tenants):
            zcount = distribution[tix]
            for _ in range(zcount):
                zone_ix += 1
                zone = f"z{zone_ix:05d}.t{tix:04d}.acc-{self.seed}.test"
                tenant.zones.append(zone)
                profile = self.rng.choices(profiles, weights=profile_weights, k=1)[0]
                private_zone = False
                if private_budget > 0:
                    private_zone = self.rng.random() < 0.35
                    if private_zone:
                        private_budget -= 1
                jobs.append((tenant, zone, profile, private_zone))
                self.summary["zone_profiles"][profile] = self.summary["zone_profiles"].get(profile, 0) + 1
                if private_zone:
                    self.summary["private_ip_zones"] += 1

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, self.args.parallelism)) as pool:
            futures = [pool.submit(self.create_zone_and_records_for_tenant, *job) for job in jobs]
            for fut in concurrent.futures.as_completed(futures):
                fut.result()

        # Full-state validation via DNS-only follower checks using random sample for time control.
        self.log("Validating baseline replication on followers via DNS")
        sample_size = min(250, len(jobs))
        sampled = self.rng.sample(jobs, sample_size) if jobs else []
        for tenant, zone, _, _ in sampled:
            _ = tenant
            try:
                vals1 = self._query_rrset(FOLLOWER1_DNS_PORT, zone, "SOA")
                vals2 = self._query_rrset(FOLLOWER2_DNS_PORT, zone, "SOA")
                if vals1 != vals2:
                    raise AcceptanceError(f"Follower SOA mismatch for {zone}: {vals1} vs {vals2}")
            except Exception as ex:
                raise AcceptanceError(f"Baseline replication validation failed for {zone}: {ex}") from ex

    def run_auth_scenarios(self, tenants: List[TenantInfo]) -> None:
        self.log("Running role-scoped authorization scenarios")
        selected = [t for t in tenants if t.zones][: max(1, min(self.args.auth_tenants, len(tenants)))]

        for ix, tenant in enumerate(selected):
            zone = tenant.zones[0]
            tenant_auth = (tenant.admin_user, tenant.admin_pass)

            branch = f"allow{ix}.{zone}"
            deny = f"deny{ix}.{zone}"
            exact = f"exact{ix}.{zone}"
            offscope = f"offscope{ix}.{zone}"

            r1 = self.api_request("PUT", f"/rr/{exact}", tenant_auth, json={"a": [self._public_ipv4()]})
            if r1.status_code not in (200, 201):
                raise AcceptanceError(f"Precreate exact RR failed: {r1.status_code} {r1.text}")

            role_sub = f"sub-role-{ix}-{self.seed}"
            user_sub = f"sub-user-{ix}@{tenant.tenant_name}.test"
            pass_sub = self._random_secret(20)
            sub_role_payload = {
                "name": role_sub,
                "permissions": ["USE_API", "CREATE_RR", "UPDATE_RR", "READ_RR"],
                "filter": {"fqdn": branch, "recursive": True},
            }
            cr = self.api_request("POST", "/role", tenant_auth, json=sub_role_payload)
            if cr.status_code not in (200, 201):
                raise AcceptanceError(f"Create sub role failed: {cr.status_code} {cr.text}")

            cu = self.api_request(
                "POST",
                "/user",
                tenant_auth,
                json={
                    "name": user_sub,
                    "active": True,
                    "roles": [role_sub],
                    "auth": {"password": pass_sub},
                },
            )
            if cu.status_code not in (200, 201):
                raise AcceptanceError(f"Create sub user failed: {cu.status_code} {cu.text}")

            t0 = time.time()
            allowed_ip = [self._public_ipv4()]
            ok = self.api_request("PUT", f"/rr/{branch}", (user_sub, pass_sub), json={"a": allowed_ip})
            if ok.status_code not in (200, 201):
                raise AcceptanceError(f"Allowed update unexpectedly failed: {ok.status_code} {ok.text}")
            self.summary["auth_checks"]["allow_ok"] += 1
            self.measure_and_enforce_lag("auth_updates", branch, "A", allowed_ip, t0)

            denied = self.api_request("PUT", f"/rr/{deny}", (user_sub, pass_sub), json={"a": [self._public_ipv4()]})
            if denied.status_code not in (401, 403):
                raise AcceptanceError(f"Denied update unexpectedly succeeded: {denied.status_code} {denied.text}")
            self.summary["auth_checks"]["deny_ok"] += 1

            role_exact = f"exact-role-{ix}-{self.seed}"
            user_exact = f"exact-user-{ix}@{tenant.tenant_name}.test"
            pass_exact = self._random_secret(20)
            exact_role_payload = {
                "name": role_exact,
                "permissions": ["USE_API", "UPDATE_RR", "READ_RR"],
                "filter": {"fqdn": exact, "recursive": False},
            }
            cr2 = self.api_request("POST", "/role", tenant_auth, json=exact_role_payload)
            if cr2.status_code not in (200, 201):
                raise AcceptanceError(f"Create exact role failed: {cr2.status_code} {cr2.text}")

            cu2 = self.api_request(
                "POST",
                "/user",
                tenant_auth,
                json={"name": user_exact, "active": True, "roles": [role_exact], "auth": {"password": pass_exact}},
            )
            if cu2.status_code not in (200, 201):
                raise AcceptanceError(f"Create exact user failed: {cu2.status_code} {cu2.text}")

            t1 = time.time()
            exact_ip = [self._public_ipv4()]
            ok2 = self.api_request("PUT", f"/rr/{exact}", (user_exact, pass_exact), json={"a": exact_ip})
            if ok2.status_code not in (200, 201):
                raise AcceptanceError(f"Exact allowed update failed: {ok2.status_code} {ok2.text}")
            self.summary["auth_checks"]["allow_ok"] += 1
            self.measure_and_enforce_lag("auth_updates", exact, "A", exact_ip, t1)

            denied2 = self.api_request("PUT", f"/rr/{offscope}", (user_exact, pass_exact), json={"a": [self._public_ipv4()]})
            if denied2.status_code not in (401, 403):
                raise AcceptanceError(f"Offscope update unexpectedly succeeded: {denied2.status_code} {denied2.text}")
            self.summary["auth_checks"]["deny_ok"] += 1

    def create_dynip_realm_zone(self, realm: str) -> None:
        payload = {
            "ttl": 300,
            "soa": {
                "refresh": 120,
                "retry": 60,
                "expire": 3600,
                "minimum": 60,
                "mname": f"ns1.{realm}",
                "rname": f"hostmaster.{realm}",
            },
            "ns": [f"ns1.{realm}", f"ns2.{realm}"],
        }

        r = self.api_request("POST", f"/zone/{realm}", self.auth_admin, json=payload)
        if r.status_code not in (200, 201, 409):
            raise AcceptanceError(f"DynIP realm zone creation failed: {r.status_code} {r.text}")

    def provision_dynip(self, tenants: List[TenantInfo], realm: str) -> List[DynIpHostInfo]:
        self.log("Provisioning DynIP tenants/roots/hosts")
        self.create_dynip_realm_zone(realm)

        candidates = [t for t in tenants if t.zones]
        if not candidates:
            return []
        chosen = self.rng.sample(candidates, min(self.args.dynip_tenants, len(candidates)))

        hosts: List[DynIpHostInfo] = []
        for idx, tenant in enumerate(chosen):
            auth = (tenant.admin_user, tenant.admin_pass)
            root = f"r{idx}-{self.seed}"

            rr = self.api_request("POST", f"/dynip/{root}", auth, json={"host_limit": 8})
            if rr.status_code not in (200, 201):
                raise AcceptanceError(f"DynIP root creation failed: {rr.status_code} {rr.text}")

            n_hosts = self.rng.randint(1, 5)
            for hix in range(n_hosts):
                host = f"h{hix}"
                ttl = self.rng.randint(60, 600)
                rh = self.api_request("POST", f"/dynip/{root}/{host}", auth, json={"ttl": ttl})
                if rh.status_code not in (200, 201):
                    raise AcceptanceError(f"DynIP host creation failed: {rh.status_code} {rh.text}")
                body = rh.json()
                hosts.append(
                    DynIpHostInfo(
                        tenant_id=tenant.tenant_id,
                        tenant_user=tenant.admin_user,
                        tenant_pass=tenant.admin_pass,
                        root=root,
                        host=host,
                        fqdn=body["fqdn"],
                        token=body["token"],
                    )
                )

        self.summary["dynip_hosts"] = len(hosts)
        return hosts

    def dynip_update_once(self, host: DynIpHostInfo, ip: str) -> float:
        t0 = time.time()
        headers = {"Authorization": f"Bearer {host.token}"}
        r = requests.post(
            f"{self.primary_api}/dynip/update",
            headers=headers,
            json={"fqdn": host.fqdn, "ip": ip},
            timeout=60,
        )
        if r.status_code != 200:
            raise AcceptanceError(f"DynIP update failed for {host.fqdn}: {r.status_code} {r.text}")

        body = r.json()
        status = body.get("status", "")
        if status not in ("good", "nochg"):
            raise AcceptanceError(f"DynIP update returned unexpected status for {host.fqdn}: {body}")

        self.measure_and_enforce_lag("dynip_updates", host.fqdn, "A", [ip], t0)
        return time.time() - t0

    def run_dynip_waves(self, hosts: List[DynIpHostInfo]) -> None:
        if not hosts:
            self.log("No DynIP hosts provisioned; skipping waves")
            return

        self.log("Running DynIP update waves")
        total_duration = max(0, int(self.args.duration_min * 60))
        if total_duration == 0:
            return

        start = time.time()
        wave = 0
        while time.time() - start < total_duration:
            wave += 1
            wave_start = time.time()
            self.summary["waves"] = wave

            updates: List[Tuple[DynIpHostInfo, str]] = []
            for h in hosts:
                ip = f"203.0.{wave % 255}.{self.rng.randint(1, 254)}"
                updates.append((h, ip))

            with concurrent.futures.ThreadPoolExecutor(max_workers=max(2, min(16, self.args.parallelism))) as pool:
                futs = [pool.submit(self.dynip_update_once, h, ip) for h, ip in updates]
                for fut in concurrent.futures.as_completed(futs):
                    fut.result()

            self.summary["dynip_updates"] += len(updates)

            elapsed = time.time() - wave_start
            sleep_for = self.args.wave_interval_sec - elapsed
            if sleep_for > 0:
                time.sleep(sleep_for)

    def get_metric(self, url: str, metric_name: str) -> float:
        try:
            r = requests.get(url, timeout=3)
        except Exception:
            return 0.0
        if r.status_code != 200:
            return 0.0

        total = 0.0
        pattern = re.compile(rf"^{re.escape(metric_name)}(?:\{{[^}}]*\}})?\s+(-?[0-9]+(?:\.[0-9]+)?)$")
        for line in r.text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = pattern.match(line)
            if m:
                total += float(m.group(1))
        return total

    def collect_diagnostics(self) -> None:
        self.artifact_root.mkdir(parents=True, exist_ok=True)

        (self.artifact_root / "request_samples.json").write_text(json.dumps(self.request_samples, indent=2), encoding="utf-8")

        metrics_dump = {
            "primary": self._safe_fetch_text(self.primary_metrics),
            FOLLOWER1_NAME: self._safe_fetch_text(self.follower_metrics[0]),
            FOLLOWER2_NAME: self._safe_fetch_text(self.follower_metrics[1]),
        }
        (self.artifact_root / "metrics.txt.json").write_text(json.dumps(metrics_dump, indent=2), encoding="utf-8")
        self.summary["node_metrics"] = {
            PRIMARY_NAME: {
                "errors": self.get_metric(self.primary_metrics, "nsblast_logged_errors"),
                "warnings": self.get_metric(self.primary_metrics, "nsblast_logged_warnings"),
            },
            FOLLOWER1_NAME: {
                "errors": self.get_metric(self.follower_metrics[0], "nsblast_logged_errors"),
                "warnings": self.get_metric(self.follower_metrics[0], "nsblast_logged_warnings"),
            },
            FOLLOWER2_NAME: {
                "errors": self.get_metric(self.follower_metrics[1], "nsblast_logged_errors"),
                "warnings": self.get_metric(self.follower_metrics[1], "nsblast_logged_warnings"),
            },
        }

        logs_dir = self.artifact_root / "docker-logs"
        logs_dir.mkdir(parents=True, exist_ok=True)
        for name in self.containers:
            out = subprocess.run(["docker", "logs", name], text=True, capture_output=True)
            (logs_dir / f"{name}.stdout.log").write_text(out.stdout or "", encoding="utf-8")
            (logs_dir / f"{name}.stderr.log").write_text(out.stderr or "", encoding="utf-8")

        report = {
            "run_id": self.run_id,
            "utc": dt.datetime.utcnow().isoformat() + "Z",
            "summary": self.summary,
            "latency_stats": self._latency_stats(),
            "failures": self.failures,
        }
        (self.artifact_root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    def _safe_fetch_text(self, url: str) -> str:
        try:
            r = requests.get(url, timeout=3)
            return r.text
        except Exception as ex:
            return f"ERROR: {ex}"

    def _latency_stats(self) -> dict:
        out = {}
        for key, values in self.latencies.items():
            if not values:
                out[key] = {"count": 0}
                continue
            arr = sorted(values)
            out[key] = {
                "count": len(arr),
                "min": arr[0],
                "p50": arr[int(len(arr) * 0.50)],
                "p95": arr[int(max(0, len(arr) * 0.95 - 1))],
                "p99": arr[int(max(0, len(arr) * 0.99 - 1))],
                "max": arr[-1],
                "mean": statistics.fmean(arr),
            }
        return out

    def stop_cluster(self) -> None:
        for name in self.containers:
            subprocess.run(["docker", "rm", "-f", name], capture_output=True)
        self.remove_network()

    def cleanup_runtime(self) -> None:
        if self.runtime_root.exists():
            shutil.rmtree(self.runtime_root, ignore_errors=True)

    def run(self) -> None:
        self.log(f"Starting acceptance test run_id={self.run_id}")
        self.log(f"Seed={self.seed} image={self.image}")

        tenants: List[TenantInfo] = []
        dynip_hosts: List[DynIpHostInfo] = []

        try:
            self.prepare_dirs()
            self.create_certs()
            self.ensure_clean_containers()
            self.create_network()

            self.bootstrap_node(PRIMARY_NAME, "primary")
            self.bootstrap_node(FOLLOWER1_NAME, "follower")
            self.bootstrap_node(FOLLOWER2_NAME, "follower")

            self.start_containers()
            self.wait_cluster_ready()

            dynip_realm = f"dynip.acc-{self.seed}.test"
            self.configure_dynip_vars(dynip_realm)

            tenants = self.create_tenants()
            self.generate_and_apply_dataset(tenants)
            self.run_auth_scenarios(tenants)

            dynip_hosts = self.provision_dynip(tenants, dynip_realm)
            self.run_dynip_waves(dynip_hosts)

            self.log("Acceptance test completed successfully")

        except Exception as ex:
            self.failures.append(str(ex))
            self.failures.append(traceback.format_exc())
            self.log(f"Acceptance run failed: {ex}")
            raise

        finally:
            self.collect_diagnostics()
            if self.failures and self.args.keep_on_fail:
                self.log(f"Keeping cluster/runtime for debugging: {self.runtime_root}")
            else:
                self.stop_cluster()
                self.cleanup_runtime()



def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="nsblast full acceptance test")

    p.add_argument("--image-tag", default="", help="Use ghcr.io/jgaa/nsblast:<tag>")
    p.add_argument("--image", default="", help="Use a full image reference")

    p.add_argument("--seed", type=int, default=None)
    p.add_argument("--zones", type=int, default=3000)
    p.add_argument("--tenants", type=int, default=300)
    p.add_argument("--dynip-tenants", type=int, default=10)
    p.add_argument("--auth-tenants", type=int, default=10)

    p.add_argument("--duration-min", type=float, default=60.0)
    p.add_argument("--wave-interval-sec", type=float, default=30.0)
    p.add_argument("--max-repl-lag-sec", type=float, default=2.0)
    p.add_argument("--parallelism", type=int, default=8)
    p.add_argument("--node-log-level", choices=["info", "debug", "trace"], default="info")

    p.add_argument("--keep-on-fail", action="store_true")

    args = p.parse_args()

    if args.zones < 1:
        raise SystemExit("--zones must be > 0")
    if args.tenants < 1:
        raise SystemExit("--tenants must be > 0")
    if args.dynip_tenants < 1:
        raise SystemExit("--dynip-tenants must be > 0")
    if args.auth_tenants < 1:
        raise SystemExit("--auth-tenants must be > 0")
    if args.parallelism < 1:
        raise SystemExit("--parallelism must be > 0")
    if args.max_repl_lag_sec <= 0:
        raise SystemExit("--max-repl-lag-sec must be > 0")

    return args


if __name__ == "__main__":
    ns = parse_args()
    runner = Runner(ns)

    try:
        runner.run()
    except Exception:
        print(f"Artifacts written to: {runner.artifact_root}", file=sys.stderr)
        sys.exit(1)

    print(json.dumps({
        "result": "ok",
        "run_id": runner.run_id,
        "artifacts": str(runner.artifact_root),
        "summary": runner.summary,
        "latency": runner._latency_stats(),
    }, indent=2))
