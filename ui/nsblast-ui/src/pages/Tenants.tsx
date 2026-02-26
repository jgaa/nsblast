import { useEffect, useState } from 'react';
import { useAppState } from '../modules/AppState';
import { encodePathSegment } from '../modules/url';

type Tenant = {
  id: string;
  name: string;
  active?: boolean;
  root?: string;
  users?: string[];
  roles?: string[];
};

type TenantForm = {
  id: string;
  name: string;
  active: boolean;
  root: string;
};

const emptyForm: TenantForm = {
  id: '',
  name: '',
  active: true,
  root: ''
};

export default function Tenants() {
  const { api } = useAppState();
  const [tenants, setTenants] = useState<Tenant[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [form, setForm] = useState<TenantForm>(emptyForm);
  const [editingId, setEditingId] = useState('');

  const loadTenants = async () => {
    setLoading(true);
    setError('');
    try {
      const response = (await api.request('/tenant')) as { value?: Tenant[] };
      setTenants(response.value ?? []);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load tenants');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void loadTenants();
  }, []);

  const resetForm = () => {
    setForm(emptyForm);
    setEditingId('');
  };

  const onCreate = async () => {
    const payload: Record<string, unknown> = {
      name: form.name.trim(),
      active: form.active,
      root: form.root.trim()
    };
    if (form.id.trim()) {
      payload.id = form.id.trim();
    }

    setError('');
    try {
      await api.request('/tenant', { method: 'POST', body: payload });
      await loadTenants();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to create tenant');
    }
  };

  const onSave = async () => {
    const id = editingId || form.id.trim();
    if (!id) {
      setError('Select a tenant first.');
      return;
    }

    setError('');
    try {
      await api.request(`/tenant/${encodePathSegment(id)}`, {
        method: 'PUT',
        body: {
          id,
          name: form.name.trim(),
          active: form.active,
          root: form.root.trim()
        }
      });
      await loadTenants();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to update tenant');
    }
  };

  const onDelete = async () => {
    const id = editingId || form.id.trim();
    if (!id) {
      setError('Select a tenant first.');
      return;
    }

    if (!window.confirm(`Delete tenant "${id}"?`)) {
      return;
    }

    setError('');
    try {
      await api.request(`/tenant/${encodePathSegment(id)}`, { method: 'DELETE' });
      await loadTenants();
      resetForm();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete tenant');
    }
  };

  const selectTenant = (tenant: Tenant) => {
    setEditingId(tenant.id);
    setForm({
      id: tenant.id,
      name: tenant.name ?? '',
      active: tenant.active ?? true,
      root: tenant.root ?? ''
    });
    setError('');
  };

  if (loading) {
    return <div className="w3-panel">Loading tenants...</div>;
  }

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <h1>Tenants</h1>
      {error ? <div className="w3-panel w3-red">{error}</div> : null}

      <div className="w3-row-padding">
        <div className="w3-half">
          <table className="w3-table-all">
            <thead>
              <tr>
                <th>ID</th>
                <th>Name</th>
                <th>Active</th>
                <th>Users</th>
                <th>Roles</th>
              </tr>
            </thead>
            <tbody>
              {tenants.map((tenant) => (
                <tr
                  key={tenant.id}
                  onClick={() => selectTenant(tenant)}
                  style={{ cursor: 'pointer' }}
                  className={tenant.id === editingId ? 'w3-pale-green' : ''}
                >
                  <td>{tenant.id}</td>
                  <td>{tenant.name}</td>
                  <td>{tenant.active ? 'yes' : 'no'}</td>
                  <td>{tenant.users?.length ?? 0}</td>
                  <td>{tenant.roles?.length ?? 0}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <div className="w3-half">
          <div className="w3-card w3-white" style={{ padding: '1rem' }}>
            <h3>{editingId ? 'Edit Tenant' : 'Create Tenant'}</h3>
            <label>ID (UUID, optional on create)</label>
            <input
              className="w3-input w3-border"
              value={form.id}
              onChange={(event) => setForm({ ...form, id: event.target.value })}
              disabled={Boolean(editingId)}
            />
            <label>Name</label>
            <input
              className="w3-input w3-border"
              value={form.name}
              onChange={(event) => setForm({ ...form, name: event.target.value })}
            />
            <label>Root</label>
            <input
              className="w3-input w3-border"
              value={form.root}
              onChange={(event) => setForm({ ...form, root: event.target.value })}
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

            <div className="w3-bar">
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
