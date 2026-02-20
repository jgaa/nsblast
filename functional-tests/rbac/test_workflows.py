import os
import uuid

import pytest
import requests


def _zone_payload(fqdn: str) -> dict:
    return {
        "soa": {
            "refresh": 2000,
            "retry": 3000,
            "expire": 4000,
            "minimum": 5000,
            "mname": "master",
            "rname": f"hostmaster.{fqdn}",
        },
        "ns": ["master", "ns2"],
        "a": ["127.0.0.1", "127.0.0.2"],
        "mx": [{"priority": 10, "host": f"mail.{fqdn}"}],
    }


@pytest.fixture(scope="module")
def workflow_ctx():
    password = os.environ["NSBLAST_ADMIN_PASSWORD"]
    base_url = os.getenv("NSBLAST_URL", "http://127.0.0.1:8080/api/v1")
    return {
        "base_url": base_url.rstrip("/"),
        "wait": 0,
        "admin_auth": ("admin", password),
    }


def _request(ctx, method: str, path: str, auth=None, payload=None):
    return requests.request(
        method=method,
        url=f"{ctx['base_url']}{path}",
        auth=auth,
        params={"wait": ctx["wait"]},
        json=payload,
    )


def test_workflow_login_and_list_zones(workflow_ctx):
    version = _request(workflow_ctx, "GET", "/version", auth=workflow_ctx["admin_auth"])
    assert version.status_code == 200

    zones = _request(workflow_ctx, "GET", "/zone", auth=workflow_ctx["admin_auth"])
    assert zones.status_code == 200
    body = zones.json()
    assert body["error"] is False
    assert isinstance(body["value"], list)


def test_workflow_zone_create_update_delete(workflow_ctx):
    zone_name = f"phase5-zone-{uuid.uuid4().hex[:8]}.example.com"

    created = _request(
        workflow_ctx,
        "POST",
        f"/zone/{zone_name}",
        auth=workflow_ctx["admin_auth"],
        payload=_zone_payload(zone_name),
    )
    assert created.status_code in (200, 201)

    # "Update zone" via apex RR update workflow.
    updated = _request(
        workflow_ctx,
        "PATCH",
        f"/rr/{zone_name}",
        auth=workflow_ctx["admin_auth"],
        payload={"txt": ["phase5-zone-update"]},
    )
    assert updated.status_code == 200

    fetched = _request(
        workflow_ctx,
        "GET",
        f"/rr/{zone_name}",
        auth=workflow_ctx["admin_auth"],
    )
    assert fetched.status_code == 200
    assert "txt" in fetched.json()["value"]

    deleted = _request(
        workflow_ctx,
        "DELETE",
        f"/zone/{zone_name}",
        auth=workflow_ctx["admin_auth"],
    )
    assert deleted.status_code == 200


def test_workflow_rr_crud(workflow_ctx):
    zone_name = f"phase5-rr-{uuid.uuid4().hex[:8]}.example.com"
    fqdn = f"www.{zone_name}"

    created_zone = _request(
        workflow_ctx,
        "POST",
        f"/zone/{zone_name}",
        auth=workflow_ctx["admin_auth"],
        payload=_zone_payload(zone_name),
    )
    assert created_zone.status_code in (200, 201)

    try:
        create_rr = _request(
            workflow_ctx,
            "POST",
            f"/rr/{fqdn}",
            auth=workflow_ctx["admin_auth"],
            payload={"a": ["127.0.0.10"]},
        )
        assert create_rr.status_code in (200, 201)

        get_rr = _request(workflow_ctx, "GET", f"/rr/{fqdn}", auth=workflow_ctx["admin_auth"])
        assert get_rr.status_code == 200
        assert get_rr.json()["value"]["a"] == ["127.0.0.10"]

        put_rr = _request(
            workflow_ctx,
            "PUT",
            f"/rr/{fqdn}",
            auth=workflow_ctx["admin_auth"],
            payload={"a": ["127.0.0.11"]},
        )
        assert put_rr.status_code == 200

        patch_rr = _request(
            workflow_ctx,
            "PATCH",
            f"/rr/{fqdn}",
            auth=workflow_ctx["admin_auth"],
            payload={"txt": ["phase5-rr-patch"]},
        )
        assert patch_rr.status_code == 200

        delete_rr_type = _request(
            workflow_ctx,
            "DELETE",
            f"/rr/{fqdn}/txt",
            auth=workflow_ctx["admin_auth"],
        )
        assert delete_rr_type.status_code == 200

        delete_rr = _request(
            workflow_ctx,
            "DELETE",
            f"/rr/{fqdn}",
            auth=workflow_ctx["admin_auth"],
        )
        assert delete_rr.status_code == 200
    finally:
        _request(workflow_ctx, "DELETE", f"/zone/{zone_name}", auth=workflow_ctx["admin_auth"])


def test_workflow_tenant_role_user_management(workflow_ctx):
    tenant_id = str(uuid.uuid4())
    role_name = f"phase5role{uuid.uuid4().hex[:6]}"
    user_name = f"phase5user{uuid.uuid4().hex[:6]}"
    tenant_name = f"phase5-tenant-{uuid.uuid4().hex[:6]}"
    tenant_admin_user = f"admin@{tenant_name}"
    tenant_admin_pass = "phase5-tenant-admin"

    permissions_resp = _request(
        workflow_ctx,
        "GET",
        "/permissions",
        auth=workflow_ctx["admin_auth"],
    )
    assert permissions_resp.status_code == 200
    all_permissions = permissions_resp.json()["value"]
    assert isinstance(all_permissions, list)
    assert "USE_API" in all_permissions

    tenant_payload = {
        "id": tenant_id,
        "name": tenant_name,
        "active": True,
        "allowedPermissions": all_permissions,
        "roles": [{"name": "tenant-admin", "permissions": all_permissions}],
        "users": [
            {
                "name": tenant_admin_user,
                "active": True,
                "roles": ["tenant-admin"],
                "auth": {"password": tenant_admin_pass},
            }
        ],
    }

    created_tenant = _request(
        workflow_ctx,
        "POST",
        "/tenant",
        auth=workflow_ctx["admin_auth"],
        payload=tenant_payload,
    )
    assert created_tenant.status_code in (200, 201)

    tenant_auth = (tenant_admin_user, tenant_admin_pass)
    role_permissions = ["USE_API", "LIST_USERS"]

    try:
        create_role = _request(
            workflow_ctx,
            "POST",
            "/role",
            auth=tenant_auth,
            payload={"name": role_name, "permissions": role_permissions},
        )
        assert create_role.status_code == 201

        list_roles = _request(workflow_ctx, "GET", "/role", auth=tenant_auth)
        assert list_roles.status_code == 200
        assert any(r.get("name") == role_name for r in list_roles.json()["value"])

        update_role = _request(
            workflow_ctx,
            "PUT",
            f"/role/{role_name}",
            auth=tenant_auth,
            payload={"name": role_name, "permissions": ["USE_API", "LIST_USERS", "GET_USER"]},
        )
        assert update_role.status_code in (200, 201)

        create_user = _request(
            workflow_ctx,
            "POST",
            "/user",
            auth=tenant_auth,
            payload={
                "name": user_name,
                "active": True,
                "roles": [role_name],
                "auth": {"password": "phase5-user-pass"},
            },
        )
        assert create_user.status_code == 201

        list_users = _request(workflow_ctx, "GET", "/user", auth=tenant_auth)
        assert list_users.status_code == 200
        assert any(u.get("name") == user_name for u in list_users.json()["value"])

        update_user = _request(
            workflow_ctx,
            "PUT",
            f"/user/{user_name}",
            auth=tenant_auth,
            payload={
                "name": user_name,
                "active": False,
                "roles": [role_name],
                "auth": {"password": "phase5-user-pass"},
            },
        )
        assert update_user.status_code in (200, 201)

        delete_user = _request(workflow_ctx, "DELETE", f"/user/{user_name}", auth=tenant_auth)
        assert delete_user.status_code == 200

        delete_role = _request(workflow_ctx, "DELETE", f"/role/{role_name}", auth=tenant_auth)
        assert delete_role.status_code == 200
    finally:
        _request(workflow_ctx, "DELETE", f"/tenant/{tenant_id}", auth=workflow_ctx["admin_auth"])
