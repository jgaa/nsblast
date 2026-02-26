import { useEffect, useMemo, useState } from 'react';
import { useAppState } from '../modules/AppState';
import { encodePathSegment } from '../modules/url';

type UserAuth = {
  hash?: string;
  seed?: string;
  password?: string;
};

type User = {
  id: string;
  name: string;
  active?: boolean;
  roles?: string[];
  auth?: UserAuth;
};

type Role = {
  name: string;
};

type UserForm = {
  id: string;
  name: string;
  active: boolean;
  roles: string[];
  password: string;
};

const emptyForm: UserForm = {
  id: '',
  name: '',
  active: true,
  roles: [],
  password: ''
};

function queryWithTenant(path: string, tenant: string): string {
  const tenantId = tenant.trim();
  if (!tenantId) {
    return path;
  }
  const separator = path.includes('?') ? '&' : '?';
  return `${path}${separator}tenant=${encodeURIComponent(tenantId)}`;
}

export default function Users() {
  const { api } = useAppState();
  const [tenantId, setTenantId] = useState('');
  const [users, setUsers] = useState<User[]>([]);
  const [roles, setRoles] = useState<Role[]>([]);
  const [form, setForm] = useState<UserForm>(emptyForm);
  const [editingName, setEditingName] = useState('');
  const [existingAuth, setExistingAuth] = useState<UserAuth | undefined>(undefined);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const roleOptions = useMemo(
    () => [...roles.map((role) => role.name)].sort((left, right) => left.localeCompare(right)),
    [roles]
  );

  const loadData = async () => {
    setLoading(true);
    setError('');
    try {
      const [usersResponse, rolesResponse] = await Promise.all([
        api.request(queryWithTenant('/user', tenantId)),
        api.request(queryWithTenant('/role', tenantId))
      ]);
      setUsers(((usersResponse as { value?: User[] }).value) ?? []);
      setRoles(((rolesResponse as { value?: Role[] }).value) ?? []);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load users');
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
    setExistingAuth(undefined);
    setError('');
  };

  const toggleRole = (roleName: string) => {
    const hasRole = form.roles.includes(roleName);
    setForm({
      ...form,
      roles: hasRole ? form.roles.filter((name) => name !== roleName) : [...form.roles, roleName]
    });
  };

  const selectUser = (user: User) => {
    setEditingName(user.name);
    setExistingAuth(user.auth);
    setForm({
      id: user.id ?? '',
      name: user.name,
      active: user.active ?? true,
      roles: user.roles ?? [],
      password: ''
    });
    setError('');
  };

  const onCreate = async () => {
    const name = form.name.trim().toLowerCase();
    if (!name) {
      setError('User name is required.');
      return;
    }
    if (!form.password.trim()) {
      setError('Password is required when creating a user.');
      return;
    }

    setError('');
    try {
      await api.request(queryWithTenant('/user', tenantId), {
        method: 'POST',
        body: {
          name,
          active: form.active,
          roles: form.roles,
          auth: {
            password: form.password
          }
        }
      });
      await loadData();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to create user');
    }
  };

  const onSave = async () => {
    const name = (editingName || form.name).trim().toLowerCase();
    if (!name) {
      setError('Select a user first.');
      return;
    }

    const payload: Record<string, unknown> = {
      id: form.id.trim() || undefined,
      name,
      active: form.active,
      roles: form.roles
    };

    if (form.password.trim()) {
      payload.auth = { password: form.password };
    } else if (existingAuth) {
      payload.auth = existingAuth;
    }

    setError('');
    try {
      await api.request(queryWithTenant(`/user/${encodePathSegment(name)}`, tenantId), {
        method: 'PUT',
        body: payload
      });
      await loadData();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to update user');
    }
  };

  const onDelete = async () => {
    const name = (editingName || form.name).trim().toLowerCase();
    if (!name) {
      setError('Select a user first.');
      return;
    }
    if (!window.confirm(`Delete user "${name}"?`)) {
      return;
    }

    setError('');
    try {
      await api.request(queryWithTenant(`/user/${encodePathSegment(name)}`, tenantId), { method: 'DELETE' });
      await loadData();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete user');
    }
  };

  if (loading) {
    return <div className="w3-panel">Loading users...</div>;
  }

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <h1>Users</h1>
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
                <th>ID</th>
                <th>Active</th>
                <th>Roles</th>
              </tr>
            </thead>
            <tbody>
              {users.map((user) => (
                <tr
                  key={user.name}
                  onClick={() => selectUser(user)}
                  style={{ cursor: 'pointer' }}
                  className={user.name === editingName ? 'w3-pale-green' : ''}
                >
                  <td>{user.name}</td>
                  <td>{user.id}</td>
                  <td>{user.active ? 'yes' : 'no'}</td>
                  <td>{(user.roles ?? []).join(', ')}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <div className="w3-half">
          <div className="w3-card w3-white" style={{ padding: '1rem' }}>
            <h3>{editingName ? 'Edit User' : 'Create User'}</h3>
            <label>ID</label>
            <input
              className="w3-input w3-border"
              value={form.id}
              onChange={(event) => setForm({ ...form, id: event.target.value })}
            />
            <label>Name</label>
            <input
              className="w3-input w3-border"
              value={form.name}
              onChange={(event) => setForm({ ...form, name: event.target.value })}
              disabled={Boolean(editingName)}
            />
            <p>
              <label>
                <input
                  className="w3-check"
                  type="checkbox"
                  checked={form.active}
                  onChange={(event) => setForm({ ...form, active: event.target.checked })}
                />
                <span style={{ marginLeft: '0.5rem' }}>Active</span>
              </label>
            </p>

            <label>Password {editingName ? '(leave empty to keep current)' : ''}</label>
            <input
              className="w3-input w3-border"
              type="password"
              value={form.password}
              onChange={(event) => setForm({ ...form, password: event.target.value })}
            />

            <p>Roles</p>
            <div className="w3-border" style={{ padding: '0.5rem', maxHeight: '240px', overflowY: 'auto' }}>
              {roleOptions.map((roleName) => (
                <p key={roleName} style={{ margin: 0 }}>
                  <label>
                    <input
                      className="w3-check"
                      type="checkbox"
                      checked={form.roles.includes(roleName)}
                      onChange={() => toggleRole(roleName)}
                    />
                    <span style={{ marginLeft: '0.5rem' }}>{roleName}</span>
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
