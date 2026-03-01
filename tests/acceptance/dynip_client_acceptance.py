#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import os
import pathlib
import random
import shutil
import stat
import subprocess
import sys
import time
import traceback
import uuid
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Set, Tuple

import requests


API_PORT = 18090
HTTP_TIMEOUT = 15
CONTAINER_NAME_PREFIX = "nsb-dynip-client"
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
    limited_user: str
    limited_pass: str


@dataclass
class DynIpHostInfo:
    root: str
    host: str
    fqdn: str
    token: str


class Runner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.seed = args.seed if args.seed is not None else random.randint(1, 2**31 - 1)
        self.rng = random.Random(self.seed)

        self._log_handle = None

        stamp = dt.datetime.now(dt.UTC).strftime("%Y%m%d-%H%M%S")
        self.run_id = f"dynip-client-acceptance-{stamp}-{self.seed}"

        self.acceptance_dir = pathlib.Path(__file__).resolve().parent
        self.artifact_root = pathlib.Path(args.artifact_root) if args.artifact_root else self.acceptance_dir / "artifacts" / self.run_id
        self.runtime_root = self.acceptance_dir / "runtime" / self.run_id
        self.server_root = self.runtime_root / "server"
        self.config_root = self.runtime_root / "configs"
        self.log_path = self.artifact_root / "transcript.log"
        self.request_samples_path = self.artifact_root / "request_samples.json"
        self.report_path = self.artifact_root / "report.json"
        self.scenario_root = self.artifact_root / "scenarios"

        self.network = f"nsb-dynip-cli-{self.seed}-{os.getpid()}"
        self.container_name = f"{CONTAINER_NAME_PREFIX}-{self.seed}"
        self.admin_password = args.admin_password or os.environ.get("NSBLAST_ADMIN_PASSWORD", "VerySecret")
        self.realm = args.realm or "dynip.acceptance.test"
        self.image = self._resolve_image()
        self.cli_bin = self._resolve_cli_bin()
        self.primary_api = f"http://127.0.0.1:{API_PORT}/api/v1"
        self.auth_admin = ("admin", self.admin_password)

        self.request_samples: List[dict] = []
        self.provisioning_events: List[dict] = []
        self.scenario_results: List[dict] = []
        self.summary: Dict[str, object] = {
            "seed": self.seed,
            "image": self.image,
            "cli_bin": str(self.cli_bin),
            "realm": self.realm,
            "result": "running",
            "failures": [],
        }
    def _resolve_image(self) -> str:
        if self.args.image:
            return self.args.image
        if self.args.image_tag:
            return f"ghcr.io/jgaa/nsblast:{self.args.image_tag}"

        for candidate in DEFAULT_IMAGE_CANDIDATES:
            result = subprocess.run(["docker", "image", "inspect", candidate], check=False, capture_output=True, text=True)
            if result.returncode == 0:
                return candidate
        return DEFAULT_IMAGE_CANDIDATES[0]

    def _resolve_cli_bin(self) -> pathlib.Path:
        if self.args.cli_bin:
            path = pathlib.Path(self.args.cli_bin).expanduser().resolve()
            if not path.exists():
                raise AcceptanceError(f"--cli-bin does not exist: {path}")
            return path

        self.log("Building dynip client binary")
        self.run_cmd(
            ["cargo", "build", "-p", "dynip-client"],
            cwd=pathlib.Path(__file__).resolve().parents[2] / "cli",
        )
        return (pathlib.Path(__file__).resolve().parents[2] / "cli" / "target" / "debug" / "nsblast-dynip").resolve()

    def log(self, msg: str) -> None:
        line = f"[{dt.datetime.now(dt.UTC).isoformat()}] {msg}"
        print(line, flush=True)
        if self._log_handle is not None:
            self._log_handle.write(line + "\n")
            self._log_handle.flush()

    def run_cmd(
        self,
        cmd: Sequence[str],
        *,
        cwd: Optional[pathlib.Path] = None,
        check: bool = True,
        capture: bool = True,
    ) -> subprocess.CompletedProcess:
        result = subprocess.run(
            list(cmd),
            check=False,
            text=True,
            cwd=str(cwd) if cwd else None,
            capture_output=capture,
        )
        if check and result.returncode != 0:
            raise AcceptanceError(
                f"Command failed ({result.returncode}): {' '.join(cmd)}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
        return result

    def prepare_dirs(self) -> None:
        self.artifact_root.mkdir(parents=True, exist_ok=True)
        self.runtime_root.mkdir(parents=True, exist_ok=True)
        self.server_root.mkdir(parents=True, exist_ok=True)
        self.server_root.chmod(0o777)
        self.config_root.mkdir(parents=True, exist_ok=True)
        self.scenario_root.mkdir(parents=True, exist_ok=True)
        self._log_handle = self.log_path.open("w", encoding="utf-8")

    def cleanup(self, keep_runtime: bool) -> None:
        subprocess.run(["docker", "rm", "-f", self.container_name], check=False, capture_output=True, text=True)
        subprocess.run(["docker", "network", "rm", self.network], check=False, capture_output=True, text=True)
        if not keep_runtime:
            shutil.rmtree(self.runtime_root, ignore_errors=True)
        if self._log_handle is not None:
            self._log_handle.close()
            self._log_handle = None

    def ensure_clean_container(self) -> None:
        subprocess.run(["docker", "rm", "-f", self.container_name], check=False, capture_output=True, text=True)

    def create_network(self) -> None:
        self.run_cmd(["docker", "network", "create", self.network])

    def bootstrap_server(self) -> None:
        self.log("Bootstrapping standalone server database")
        self.run_cmd(
            [
                "docker",
                "run",
                "--rm",
                "-e",
                f"NSBLAST_ADMIN_PASSWORD={self.admin_password}",
                "-v",
                f"{self.server_root}:/var/lib/nsblast",
                self.image,
                "--db-path",
                "/var/lib/nsblast",
                "bootstrap",
                "--cluster-role",
                "none",
            ]
        )

    def start_server(self) -> None:
        self.log("Starting standalone server container")
        self.run_cmd(
            [
                "docker",
                "run",
                "-d",
                "--name",
                self.container_name,
                "--network",
                self.network,
                "-p",
                f"127.0.0.1:{API_PORT}:80",
                "-e",
                f"NSBLAST_ADMIN_PASSWORD={self.admin_password}",
                "-v",
                f"{self.server_root}:/var/lib/nsblast",
                self.image,
                "--db-path",
                "/var/lib/nsblast",
                "--http-endpoint",
                "0.0.0.0",
                "--http-port",
                "80",
                "--dns-endpoint",
                "0.0.0.0",
                "--disable-metrics-auth",
                "-C",
                self.args.server_log_level,
            ]
        )

    def stop_server(self) -> None:
        self.run_cmd(["docker", "stop", self.container_name], check=False)

    def wait_http_ok(self, path: str, timeout_sec: float = 90.0) -> None:
        deadline = time.time() + timeout_sec
        url = f"{self.primary_api}{path}"
        while time.time() < deadline:
            try:
                response = requests.get(url, auth=self.auth_admin, timeout=2)
                if response.status_code == 200:
                    return
            except Exception:
                pass
            time.sleep(0.5)
        raise AcceptanceError(f"Timed out waiting for endpoint: {url}")

    def api_request(self, method: str, path: str, auth: Optional[Tuple[str, str]], **kwargs) -> requests.Response:
        url = f"{self.primary_api}{path}"
        timeout = kwargs.pop("timeout", HTTP_TIMEOUT)
        params = kwargs.pop("params", {})
        response = requests.request(method, url, auth=auth, params=params, timeout=timeout, **kwargs)
        if len(self.request_samples) < 200:
            self.request_samples.append(
                {
                    "method": method,
                    "path": path,
                    "status": response.status_code,
                    "body": response.text[:1200],
                    "params": params,
                }
            )
        return response

    def parse_enveloped(self, response: requests.Response) -> dict:
        try:
            payload = response.json()
        except Exception as ex:
            raise AcceptanceError(f"Invalid JSON response ({response.status_code}): {response.text[:400]}") from ex

        if isinstance(payload, dict) and "value" in payload:
            return payload["value"]
        return payload

    def set_var(self, name: str, value) -> None:
        response = self.api_request("PUT", f"/admin/vars/{name}", self.auth_admin, json={"value": value})
        if response.status_code != 200:
            raise AcceptanceError(f"Failed setting var {name}: {response.status_code} {response.text}")
        self.provisioning_events.append({"op": "set_var", "name": name, "value": value})

    def configure_dynip_vars(self) -> None:
        self.log("Configuring permanent vars for DynIP")
        self.set_var("dynip_realm", self.realm)
        self.set_var("dynip_min_ttl", 60)
        self.set_var("dynip_default_ttl", 300)
        self.set_var("dynip_max_ttl", 1800)
        self.set_var("dynip_max_hosts_per_root", 8)
        self.set_var("dynip_allow_private_ips", True)
        self.set_var("dynip_enabled", True)

    def create_realm_zone(self) -> None:
        payload = {
            "ttl": 300,
            "soa": {
                "refresh": 120,
                "retry": 60,
                "expire": 3600,
                "minimum": 60,
                "mname": f"ns1.{self.realm}",
                "rname": f"hostmaster.{self.realm}",
            },
            "ns": [f"ns1.{self.realm}", f"ns2.{self.realm}"],
        }
        response = self.api_request("POST", f"/zone/{self.realm}", self.auth_admin, json=payload)
        if response.status_code not in (200, 201, 409):
            raise AcceptanceError(f"Failed creating DynIP realm zone: {response.status_code} {response.text}")
        for host in (f"ns1.{self.realm}", f"ns2.{self.realm}"):
            rr = self.api_request("PUT", f"/rr/{host}", self.auth_admin, json={"a": [self._random_ip()]})
            if rr.status_code not in (200, 201):
                raise AcceptanceError(f"Failed creating NS address for {host}: {rr.status_code} {rr.text}")
        self.provisioning_events.append({"op": "create_realm_zone", "realm": self.realm})

    def create_dynip_tenant(self) -> TenantInfo:
        tenant_id = str(uuid.uuid4())
        tenant_name = "dynip-cli-tenant"
        admin_user = f"admin@{tenant_name}.test"
        admin_pass = self._random_secret(24)
        limited_user = f"limited@{tenant_name}.test"
        limited_pass = self._random_secret(20)
        role_name = "dynip-admin"
        limited_role_name = "dynip-list-only"
        allowed_permissions = ["USE_API", "DYNIP_PROVISION", "DYNIP_CREATE", "DYNIP_DELETE", "DYNIP_LIST"]
        payload = {
            "id": tenant_id,
            "name": tenant_name,
            "active": True,
            "allowedPermissions": allowed_permissions,
            "roles": [
                {
                    "name": role_name,
                    "permissions": allowed_permissions,
                    "filter": {"fqdn": "", "recursive": True},
                },
                {
                    "name": limited_role_name,
                    "permissions": ["USE_API", "DYNIP_LIST"],
                    "filter": {"fqdn": "", "recursive": True},
                }
            ],
            "users": [
                {
                    "name": admin_user,
                    "active": True,
                    "roles": [role_name],
                    "auth": {"password": admin_pass},
                },
                {
                    "name": limited_user,
                    "active": True,
                    "roles": [limited_role_name],
                    "auth": {"password": limited_pass},
                }
            ],
        }
        response = self.api_request("POST", "/tenant", self.auth_admin, json=payload)
        if response.status_code not in (200, 201):
            raise AcceptanceError(f"Failed creating tenant: {response.status_code} {response.text}")
        self.provisioning_events.append({"op": "create_tenant", "tenant_id": tenant_id, "tenant_name": tenant_name})
        return TenantInfo(
            tenant_id=tenant_id,
            tenant_name=tenant_name,
            admin_user=admin_user,
            admin_pass=admin_pass,
            limited_user=limited_user,
            limited_pass=limited_pass,
        )

    def create_root(self, auth: Tuple[str, str], root: str) -> None:
        response = self.api_request("POST", f"/dynip/{root}", auth, json={"host_limit": 8})
        if response.status_code not in (200, 201):
            raise AcceptanceError(f"Failed creating root {root}: {response.status_code} {response.text}")
        self.provisioning_events.append({"op": "create_root", "root": root})

    def create_host(self, auth: Tuple[str, str], root: str, host: str) -> DynIpHostInfo:
        response = self.api_request("POST", f"/dynip/{root}/{host}", auth, json={"ttl": 300})
        if response.status_code not in (200, 201):
            raise AcceptanceError(f"Failed creating host {host}: {response.status_code} {response.text}")
        payload = response.json()
        info = DynIpHostInfo(root=root, host=host, fqdn=payload["fqdn"], token=payload["token"])
        self.provisioning_events.append({"op": "create_host", "root": root, "host": host, "fqdn": info.fqdn})
        return info

    def get_rr(self, fqdn: str) -> dict:
        response = self.api_request("GET", f"/rr/{fqdn}", self.auth_admin)
        if response.status_code != 200:
            raise AcceptanceError(f"Failed reading RR {fqdn}: {response.status_code} {response.text}")
        return self.parse_enveloped(response)

    def list_dynip_hosts(self, auth: Tuple[str, str], root: str) -> List[dict]:
        response = self.api_request("GET", f"/dynip/{root}/hosts", auth)
        if response.status_code != 200:
            raise AcceptanceError(f"Failed listing DynIP hosts: {response.status_code} {response.text}")
        payload = response.json()
        return payload.get("items", [])

    def _random_secret(self, n: int) -> str:
        alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
        return "".join(self.rng.choice(alphabet) for _ in range(n))

    def _random_ip(self) -> str:
        return f"198.51.{self.rng.randint(0, 254)}.{self.rng.randint(1, 254)}"

    def write_config(self, name: str, data: Dict[str, object], mode: int = 0o600) -> pathlib.Path:
        path = self.config_root / f"{name}.yaml"
        lines = []
        for key, value in data.items():
            if isinstance(value, str):
                escaped = value.replace("\\", "\\\\").replace('"', '\\"')
                lines.append(f'{key}: "{escaped}"')
            elif isinstance(value, bool):
                lines.append(f"{key}: {'true' if value else 'false'}")
            else:
                lines.append(f"{key}: {value}")
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        os.chmod(path, mode)
        return path

    def redact_config(self, data: Dict[str, object]) -> Dict[str, object]:
        redacted = dict(data)
        if "token" in redacted:
            redacted["token"] = "<redacted>"
        if "password" in redacted:
            redacted["password"] = "<redacted>"
        return redacted

    def run_cli(self, config_path: pathlib.Path, *, extra_args: Optional[Sequence[str]] = None) -> subprocess.CompletedProcess:
        cmd = [str(self.cli_bin), "--config", str(config_path)]
        if extra_args:
            cmd.extend(extra_args)
        return self.run_cmd(cmd, check=False)

    def probe_update(self, fqdn: str, token: str) -> Tuple[Optional[int], Optional[str]]:
        try:
            response = requests.post(
                f"{self.primary_api}/dynip/update",
                headers={"Authorization": f"Bearer {token}"},
                json={"fqdn": fqdn, "ip": "127.0.0.1"},
                timeout=HTTP_TIMEOUT,
            )
            return response.status_code, response.text[:600]
        except Exception as ex:
            return None, str(ex)

    def assert_exit(self, name: str, actual: int, expected: Set[int]) -> None:
        if actual not in expected:
            raise AcceptanceError(f"Scenario {name} expected exit in {sorted(expected)}, got {actual}")

    def record_scenario(
        self,
        *,
        name: str,
        config: Optional[Dict[str, object]],
        result: subprocess.CompletedProcess,
        expected_exit: Set[int],
        extra: Optional[dict] = None,
    ) -> None:
        scenario_dir = self.scenario_root / name
        scenario_dir.mkdir(parents=True, exist_ok=True)
        if config is not None:
            (scenario_dir / "config.redacted.json").write_text(
                json.dumps(self.redact_config(config), indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        (scenario_dir / "stdout.txt").write_text(result.stdout or "", encoding="utf-8")
        (scenario_dir / "stderr.txt").write_text(result.stderr or "", encoding="utf-8")

        entry = {
            "name": name,
            "exit_code": result.returncode,
            "expected_exit": sorted(expected_exit),
        }
        if extra:
            entry.update(extra)
        self.scenario_results.append(entry)

    def scenario_success_changed(self, host: DynIpHostInfo) -> None:
        update_ip = "203.0.113.10"
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": host.token,
            "fqdn": host.fqdn,
            "timeout_seconds": 10,
        }
        before = self.list_dynip_hosts((self.tenant.admin_user, self.tenant.admin_pass), host.root)
        config_path = self.write_config("success-changed", config)
        result = self.run_cli(config_path, extra_args=["--ip", update_ip])
        self.record_scenario(
            name="success-changed",
            config=config,
            result=result,
            expected_exit={2},
            extra={"requested_ip": update_ip},
        )
        after_hosts = self.list_dynip_hosts((self.tenant.admin_user, self.tenant.admin_pass), host.root)
        rr = self.get_rr(host.fqdn)
        self.scenario_results[-1].update(
            {
                "update_count_before": before[0]["update_count"] if before else None,
                "update_count_after": after_hosts[0]["update_count"] if after_hosts else None,
                "rr": rr,
            }
        )
        self.assert_exit("success-changed", result.returncode, {2})

    def scenario_success_no_change(self, host: DynIpHostInfo) -> None:
        update_ip = "203.0.113.10"
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": host.token,
            "fqdn": host.fqdn,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("success-nochange", config)
        result = self.run_cli(config_path, extra_args=["--ip", update_ip])
        self.record_scenario(
            name="success-nochange",
            config=config,
            result=result,
            expected_exit={0},
            extra={"requested_ip": update_ip},
        )
        rr = self.get_rr(host.fqdn)
        self.scenario_results[-1].update({"rr": rr})
        self.assert_exit("success-nochange", result.returncode, {0})

    def scenario_invalid_token(self, host: DynIpHostInfo) -> None:
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": "invalid-token",
            "fqdn": host.fqdn,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("invalid-token", config)
        result = self.run_cli(config_path)
        probe_status, probe_body = self.probe_update(host.fqdn, "invalid-token")
        self.record_scenario(
            name="invalid-token",
            config=config,
            result=result,
            expected_exit={3},
            extra={"probe_status": probe_status, "probe_body": probe_body},
        )
        self.assert_exit("invalid-token", result.returncode, {3})

    def scenario_scope_mismatch(self, token_host: DynIpHostInfo, target_host: DynIpHostInfo) -> None:
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": token_host.token,
            "fqdn": target_host.fqdn,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("scope-mismatch", config)
        result = self.run_cli(config_path)
        probe_status, probe_body = self.probe_update(target_host.fqdn, token_host.token)
        expected = {3, 5}
        self.record_scenario(
            name="scope-mismatch",
            config=config,
            result=result,
            expected_exit=expected,
            extra={"probe_status": probe_status, "probe_body": probe_body},
        )
        self.assert_exit("scope-mismatch", result.returncode, expected)

    def scenario_missing_token(self, host: DynIpHostInfo) -> None:
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "fqdn": host.fqdn,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("missing-token", config)
        result = self.run_cli(config_path)
        self.record_scenario(name="missing-token", config=config, result=result, expected_exit={5})
        self.assert_exit("missing-token", result.returncode, {5})

    def scenario_missing_fqdn(self, host: DynIpHostInfo) -> None:
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": host.token,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("missing-fqdn", config)
        result = self.run_cli(config_path)
        self.record_scenario(name="missing-fqdn", config=config, result=result, expected_exit={5})
        self.assert_exit("missing-fqdn", result.returncode, {5})

    def scenario_config_mode_too_open(self, host: DynIpHostInfo) -> None:
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": host.token,
            "fqdn": host.fqdn,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("config-mode-too-open", config, mode=0o644)
        result = self.run_cli(config_path)
        self.record_scenario(name="config-mode-too-open", config=config, result=result, expected_exit={5})
        self.assert_exit("config-mode-too-open", result.returncode, {5})

    def scenario_limited_permissions(self) -> None:
        auth = (self.tenant.limited_user, self.tenant.limited_pass)
        response = self.api_request("POST", "/dynip/nope", auth, json={"host_limit": 1})
        if response.status_code != 403:
            raise AcceptanceError(f"Expected 403 for limited permissions, got {response.status_code}: {response.text}")
        self.scenario_results.append(
            {
                "name": "limited-permissions",
                "http_status": response.status_code,
                "expected_http_status": 403,
            }
        )

    def scenario_dynip_disabled(self, host: DynIpHostInfo) -> None:
        self.set_var("dynip_enabled", False)
        try:
            config = {
                "url": f"http://127.0.0.1:{API_PORT}",
                "token": host.token,
                "fqdn": host.fqdn,
                "timeout_seconds": 10,
            }
            config_path = self.write_config("dynip-disabled", config)
            result = self.run_cli(config_path)
            probe_status, probe_body = self.probe_update(host.fqdn, host.token)
            self.record_scenario(
                name="dynip-disabled",
                config=config,
                result=result,
                expected_exit={3},
                extra={"probe_status": probe_status, "probe_body": probe_body},
            )
            self.assert_exit("dynip-disabled", result.returncode, {3})
        finally:
            self.set_var("dynip_enabled", True)

    def scenario_deleted_host(self, host: DynIpHostInfo) -> None:
        response = self.api_request("DELETE", f"/dynip/{host.root}/{host.host}", (self.tenant.admin_user, self.tenant.admin_pass))
        if response.status_code != 200:
            raise AcceptanceError(f"Failed deleting DynIP host: {response.status_code} {response.text}")
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": host.token,
            "fqdn": host.fqdn,
            "timeout_seconds": 10,
        }
        config_path = self.write_config("deleted-host", config)
        result = self.run_cli(config_path)
        probe_status, probe_body = self.probe_update(host.fqdn, host.token)
        expected = {3, 5}
        self.record_scenario(
            name="deleted-host",
            config=config,
            result=result,
            expected_exit=expected,
            extra={"probe_status": probe_status, "probe_body": probe_body},
        )
        self.assert_exit("deleted-host", result.returncode, expected)

    def scenario_server_unavailable(self, host: DynIpHostInfo) -> None:
        self.stop_server()
        config = {
            "url": f"http://127.0.0.1:{API_PORT}",
            "token": host.token,
            "fqdn": host.fqdn,
            "timeout_seconds": 2,
        }
        config_path = self.write_config("server-unavailable", config)
        result = self.run_cli(config_path)
        self.record_scenario(name="server-unavailable", config=config, result=result, expected_exit={4})
        self.assert_exit("server-unavailable", result.returncode, {4})

    def run_checked_scenario(self, name: str, func) -> None:
        try:
            func()
        except Exception as ex:
            self.summary["failures"].append(f"{name}: {ex}")
            self.scenario_results.append({"name": name, "error": str(ex)})
            self.log(f"Scenario failed: {name}: {ex}")

    def save_report(self) -> None:
        self.request_samples_path.write_text(json.dumps(self.request_samples, indent=2) + "\n", encoding="utf-8")
        report = {
            "run_id": self.run_id,
            "summary": self.summary,
            "provisioning": self.provisioning_events,
            "scenarios": self.scenario_results,
        }
        self.report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def capture_container_logs(self) -> None:
        log_dir = self.artifact_root / "docker-logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        for stream in ("stdout", "stderr"):
            result = subprocess.run(
                ["docker", "logs", self.container_name],
                check=False,
                capture_output=True,
                text=True,
            )
            content = result.stdout if stream == "stdout" else result.stderr
            (log_dir / f"{self.container_name}.{stream}.log").write_text(content or "", encoding="utf-8")

    def run(self) -> int:
        keep_runtime = False
        try:
            self.prepare_dirs()
            self.log(f"Starting DynIP client acceptance run_id={self.run_id}")
            self.ensure_clean_container()
            self.create_network()
            self.bootstrap_server()
            self.start_server()
            self.wait_http_ok("/version")
            self.configure_dynip_vars()
            self.create_realm_zone()
            self.tenant = self.create_dynip_tenant()
            tenant_auth = (self.tenant.admin_user, self.tenant.admin_pass)

            self.create_root(tenant_auth, "office")
            primary_host = self.create_host(tenant_auth, "office", "router")
            secondary_host = self.create_host(tenant_auth, "office", "backup")

            self.summary["tenant"] = self.tenant.tenant_name
            self.summary["fqdn"] = primary_host.fqdn
            self.summary["secondary_fqdn"] = secondary_host.fqdn

            self.run_checked_scenario("success-changed", lambda: self.scenario_success_changed(primary_host))
            self.run_checked_scenario("success-nochange", lambda: self.scenario_success_no_change(primary_host))
            self.run_checked_scenario("invalid-token", lambda: self.scenario_invalid_token(primary_host))
            self.run_checked_scenario("scope-mismatch", lambda: self.scenario_scope_mismatch(primary_host, secondary_host))
            self.run_checked_scenario("missing-token", lambda: self.scenario_missing_token(primary_host))
            self.run_checked_scenario("missing-fqdn", lambda: self.scenario_missing_fqdn(primary_host))
            self.run_checked_scenario("config-mode-too-open", lambda: self.scenario_config_mode_too_open(primary_host))
            self.run_checked_scenario("limited-permissions", self.scenario_limited_permissions)
            self.run_checked_scenario("dynip-disabled", lambda: self.scenario_dynip_disabled(primary_host))
            self.run_checked_scenario("deleted-host", lambda: self.scenario_deleted_host(secondary_host))
            self.run_checked_scenario("server-unavailable", lambda: self.scenario_server_unavailable(primary_host))

            if self.summary["failures"]:
                self.summary["result"] = "failed"
                self.log("DynIP client acceptance test completed with failures")
                return 1

            self.summary["result"] = "ok"
            self.log("DynIP client acceptance test completed successfully")
            return 0
        except Exception as ex:
            self.summary["result"] = "failed"
            self.summary["failures"].append(str(ex))
            self.log(f"DynIP client acceptance test failed: {ex}")
            self.log(traceback.format_exc())
            keep_runtime = self.args.keep_on_fail
            return 1
        finally:
            try:
                self.capture_container_logs()
            except Exception as ex:
                self.log(f"Failed capturing container logs: {ex}")
            self.save_report()
            self.cleanup(keep_runtime=keep_runtime)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="nsblast DynIP client acceptance test")
    parser.add_argument("--image")
    parser.add_argument("--image-tag")
    parser.add_argument("--cli-bin")
    parser.add_argument("--keep-on-fail", action="store_true")
    parser.add_argument("--artifact-root")
    parser.add_argument("--server-log-level", default="info")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--admin-password")
    parser.add_argument("--realm")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    runner = Runner(args)
    return runner.run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
