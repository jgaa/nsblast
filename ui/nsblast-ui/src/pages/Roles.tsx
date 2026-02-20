import { useEffect, useMemo, useState } from 'react';
import { useAppState } from '../modules/AppState';

type Role = {
  name: string;
  permissions?: string[];
};

type RoleForm = {
  name: string;
  permissions: string[];
};

const emptyForm: RoleForm = {
  name: '',
  permissions: []
};

function queryWithTenant(path: string, tenant: string): string {
  const tenantId = tenant.trim();
  if (!tenantId) {
    return path;
  }
  const separator = path.includes('?') ? '&' : '?';
  return `${path}${separator}tenant=${encodeURIComponent(tenantId)}`;
}

export default function Roles() {
  const { api } = useAppState();
  const [tenantId, setTenantId] = useState('');
  const [roles, setRoles] = useState<Role[]>([]);
  const [allowedPermissions, setAllowedPermissions] = useState<string[]>([]);
  const [form, setForm] = useState<RoleForm>(emptyForm);
  const [editingName, setEditingName] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const sortedPermissions = useMemo(
    () => [...allowedPermissions].sort((left, right) => left.localeCompare(right)),
    [allowedPermissions]
  );

  const loadData = async () => {
    setLoading(true);
    setError('');
    try {
      const [rolesResponse, permissionsResponse] = await Promise.all([
        api.request(queryWithTenant('/role', tenantId)),
        api.request(queryWithTenant('/permissions', tenantId))
      ]);

      setRoles(((rolesResponse as { value?: Role[] }).value) ?? []);
      setAllowedPermissions(((permissionsResponse as { value?: string[] }).value) ?? []);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load roles');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void loadData();
  }, []);

  const resetForm = () => {
    setForm(emptyForm);
    setEditingName('');
    setError('');
  };

  const togglePermission = (permission: string) => {
    const hasPermission = form.permissions.includes(permission);
    setForm({
      ...form,
      permissions: hasPermission
        ? form.permissions.filter((value) => value !== permission)
        : [...form.permissions, permission]
    });
  };

  const onCreate = async () => {
    const name = form.name.trim().toLowerCase();
    if (!name) {
      setError('Role name is required.');
      return;
    }

    setError('');
    try {
      await api.request(queryWithTenant('/role', tenantId), {
        method: 'POST',
        body: {
          name,
          permissions: form.permissions
        }
      });
      await loadData();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to create role');
    }
  };

  const onSave = async () => {
    const name = (editingName || form.name).trim().toLowerCase();
    if (!name) {
      setError('Select a role first.');
      return;
    }

    setError('');
    try {
      await api.request(queryWithTenant(`/role/${name}`, tenantId), {
        method: 'PUT',
        body: {
          name,
          permissions: form.permissions
        }
      });
      await loadData();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to update role');
    }
  };

  const onDelete = async () => {
    const name = (editingName || form.name).trim().toLowerCase();
    if (!name) {
      setError('Select a role first.');
      return;
    }

    if (!window.confirm(`Delete role "${name}"?`)) {
      return;
    }

    setError('');
    try {
      await api.request(queryWithTenant(`/role/${name}`, tenantId), { method: 'DELETE' });
      await loadData();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete role');
    }
  };

  const selectRole = (role: Role) => {
    setEditingName(role.name);
    setForm({
      name: role.name,
      permissions: role.permissions ?? []
    });
    setError('');
  };

  if (loading) {
    return <div className="w3-panel">Loading roles...</div>;
  }

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <h1>Roles</h1>
      {error ? <div className="w3-panel w3-red">{error}</div> : null}

      <div className="w3-panel w3-pale-blue">
        <label>Tenant impersonation (optional, requires permission)</label>
        <input
          className="w3-input w3-border"
          value={tenantId}
          onChange={(event) => setTenantId(event.target.value)}
          placeholder="tenant UUID"
        />
        <button className="w3-button w3-blue" style={{ marginTop: '0.5rem' }} onClick={() => void loadData()}>
          Reload
        </button>
      </div>

      <div className="w3-row-padding">
        <div className="w3-half">
          <table className="w3-table-all">
            <thead>
              <tr>
                <th>Name</th>
                <th>Permissions</th>
              </tr>
            </thead>
            <tbody>
              {roles.map((role) => (
                <tr
                  key={role.name}
                  onClick={() => selectRole(role)}
                  style={{ cursor: 'pointer' }}
                  className={role.name === editingName ? 'w3-pale-green' : ''}
                >
                  <td>{role.name}</td>
                  <td>{(role.permissions ?? []).join(', ')}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <div className="w3-half">
          <div className="w3-card w3-white" style={{ padding: '1rem' }}>
            <h3>{editingName ? 'Edit Role' : 'Create Role'}</h3>
            <label>Name</label>
            <input
              className="w3-input w3-border"
              value={form.name}
              onChange={(event) => setForm({ ...form, name: event.target.value })}
              disabled={Boolean(editingName)}
            />

            <p>Permissions</p>
            <div className="w3-border" style={{ padding: '0.5rem', maxHeight: '280px', overflowY: 'auto' }}>
              {sortedPermissions.map((permission) => (
                <p key={permission} style={{ margin: 0 }}>
                  <label>
                    <input
                      className="w3-check"
                      type="checkbox"
                      checked={form.permissions.includes(permission)}
                      onChange={() => togglePermission(permission)}
                    />
                    <span style={{ marginLeft: '0.5rem' }}>{permission}</span>
                  </label>
                </p>
              ))}
            </div>

            <div className="w3-bar" style={{ marginTop: '0.5rem' }}>
              <button className="w3-button w3-green" onClick={onCreate}>Create</button>
              <button className="w3-button w3-blue" style={{ marginLeft: '0.5rem' }} onClick={onSave}>Save</button>
              <button className="w3-button w3-red" style={{ marginLeft: '0.5rem' }} onClick={onDelete}>Delete</button>
              <button className="w3-button w3-gray" style={{ marginLeft: '0.5rem' }} onClick={resetForm}>Clear</button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
