import { useEffect, useMemo, useState } from 'react';
import { FaArrowsRotate, FaFloppyDisk, FaRotateLeft, FaSliders, FaTrashCan } from 'react-icons/fa6';
import { useAppState } from '../modules/AppState';
import { ApiClientError } from '../modules/apiClient';
import { encodePathSegment } from '../modules/url';
import { detectVarsAccess, type VarsAccess } from '../modules/vars';

type VarItem = {
  name: string;
  value: unknown;
  default: unknown;
  non_mutable?: boolean;
  requires_restart?: boolean;
  description?: string;
};

type VarsReply = {
  items?: VarItem[];
};

const initialAccess: VarsAccess = {
  canList: false,
  canRead: false,
  canSet: false,
  canUnset: false
};

function formatValue(value: unknown): string {
  return JSON.stringify(value) ?? 'null';
}

function formatEditorValue(value: unknown): string {
  return JSON.stringify(value, null, 2) ?? 'null';
}

function formatApiError(error: unknown, fallback: string): string {
  if (error instanceof ApiClientError) {
    let detailCode: number | null = null;
    let detailMessage: string | null = null;

    if (error.details && typeof error.details === 'object' && !Array.isArray(error.details)) {
      const details = error.details as Record<string, unknown>;
      if (typeof details.code === 'number') {
        detailCode = details.code;
      }
      if (typeof details.message === 'string' && details.message.trim().length) {
        detailMessage = details.message;
      }
    }

    const message = detailMessage ?? error.message ?? fallback;
    if (detailCode !== null) {
      return `HTTP ${error.status}, code ${detailCode}: ${message}`;
    }
    return `HTTP ${error.status}: ${message}`;
  }

  return error instanceof Error ? error.message : fallback;
}

export default function Vars() {
  const { api } = useAppState();
  const [items, setItems] = useState<VarItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [refreshing, setRefreshing] = useState(false);
  const [selectedName, setSelectedName] = useState('');
  const [editorValue, setEditorValue] = useState('');
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [access, setAccess] = useState<VarsAccess>(initialAccess);

  const selectedItem = useMemo(
    () => items.find((item) => item.name === selectedName) ?? null,
    [items, selectedName]
  );

  const load = async (isRefresh = false) => {
    if (isRefresh) {
      setRefreshing(true);
    } else {
      setLoading(true);
    }
    setError('');

    try {
      const nextAccess = await detectVarsAccess(api);
      setAccess(nextAccess);

      if (!nextAccess.canList) {
        setItems([]);
        setSelectedName('');
        setEditorValue('');
        return;
      }

      const response = (await api.request('/admin/vars')) as VarsReply;
      const nextItems = response.items ?? [];
      setItems(nextItems);

      if (!nextItems.length) {
        setSelectedName('');
        setEditorValue('');
        return;
      }

      const firstItem = nextItems[0];
      if (!firstItem) {
        setSelectedName('');
        setEditorValue('');
        return;
      }
      const nextSelected =
        nextItems.find((item) => item.name === selectedName)?.name ?? firstItem.name;
      setSelectedName(nextSelected);
      setEditorValue(formatEditorValue(nextItems.find((item) => item.name === nextSelected)?.value));
    } catch (err) {
      setError(formatApiError(err, 'Failed to load vars'));
    } finally {
      if (isRefresh) {
        setRefreshing(false);
      } else {
        setLoading(false);
      }
    }
  };

  useEffect(() => {
    void load(false);
  }, []);

  useEffect(() => {
    if (!selectedItem) {
      return;
    }
    setEditorValue(formatEditorValue(selectedItem.value));
  }, [selectedItem]);

  const onSelect = (item: VarItem) => {
    setSelectedName(item.name);
    setEditorValue(formatEditorValue(item.value));
    setError('');
    setNotice('');
  };

  const onSave = async () => {
    if (!selectedItem) {
      setError('Select a variable first.');
      return;
    }
    if (!access.canSet) {
      setError('Your account cannot update vars.');
      return;
    }
    if (selectedItem.non_mutable) {
      setError('This variable is read-only at runtime.');
      return;
    }

    let parsedValue: unknown;
    try {
      parsedValue = JSON.parse(editorValue);
    } catch {
      setError('Value must be valid JSON.');
      return;
    }

    setSaving(true);
    setError('');
    setNotice('');
    try {
      const updated = (await api.request(`/admin/vars/${encodePathSegment(selectedItem.name)}`, {
        method: 'PUT',
        body: { value: parsedValue }
      })) as VarItem;

      setItems((current) =>
        current.map((item) => (item.name === updated.name ? updated : item))
      );
      setEditorValue(formatEditorValue(updated.value));
      setNotice(`Updated ${updated.name}.`);
    } catch (err) {
      setError(formatApiError(err, 'Failed to update variable'));
    } finally {
      setSaving(false);
    }
  };

  const onUnset = async () => {
    if (!selectedItem) {
      setError('Select a variable first.');
      return;
    }
    if (!access.canUnset) {
      setError('Your account cannot reset vars.');
      return;
    }
    if (selectedItem.non_mutable) {
      setError('This variable is read-only at runtime.');
      return;
    }

    setSaving(true);
    setError('');
    setNotice('');
    try {
      const updated = (await api.request(`/admin/vars/${encodePathSegment(selectedItem.name)}`, {
        method: 'DELETE'
      })) as VarItem;

      setItems((current) =>
        current.map((item) => (item.name === updated.name ? updated : item))
      );
      setEditorValue(formatEditorValue(updated.value));
      setNotice(`Reset ${updated.name} to its default value.`);
    } catch (err) {
      setError(formatApiError(err, 'Failed to reset variable'));
    } finally {
      setSaving(false);
    }
  };

  const onResetEditor = () => {
    if (!selectedItem) {
      return;
    }
    setEditorValue(formatEditorValue(selectedItem.value));
    setError('');
    setNotice('');
  };

  if (loading) {
    return <div className="w3-panel">Loading vars...</div>;
  }

  if (!access.canList) {
    return (
      <div className="w3-container" style={{ padding: '1rem' }}>
        <h1>Vars</h1>
        <div className="w3-panel w3-pale-red">
          Your account does not have permission to list runtime vars.
        </div>
      </div>
    );
  }

  const isReadOnlyVar = Boolean(selectedItem?.non_mutable);
  const canSaveSelected = Boolean(selectedItem) && access.canSet && !isReadOnlyVar;
  const canUnsetSelected = Boolean(selectedItem) && access.canUnset && !isReadOnlyVar;

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <div className="w3-bar">
        <h1 className="w3-bar-item" style={{ margin: 0, paddingLeft: 0 }}>
          <FaSliders style={{ marginRight: '0.5rem' }} />
          Vars
        </h1>
        <button
          className="w3-button w3-blue w3-bar-item w3-right"
          onClick={() => void load(true)}
          disabled={refreshing || saving}
        >
          <FaArrowsRotate style={{ marginRight: '0.4rem' }} />
          {refreshing ? 'Refreshing...' : 'Refresh'}
        </button>
      </div>

      {notice ? <div className="w3-panel w3-pale-green">{notice}</div> : null}
      {error ? <div className="w3-panel w3-pale-red">{error}</div> : null}

      <div className="w3-row-padding">
        <div className="w3-half">
          <div className="w3-responsive w3-border">
            <table className="w3-table-all">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Value</th>
                  <th>Flags</th>
                </tr>
              </thead>
              <tbody>
                {items.map((item) => {
                  const flags = [
                    item.non_mutable ? 'non-mutable' : '',
                    item.requires_restart ? 'restart' : ''
                  ]
                    .filter(Boolean)
                    .join(', ');

                  return (
                    <tr
                      key={item.name}
                      onClick={() => onSelect(item)}
                      style={{ cursor: 'pointer' }}
                      className={item.name === selectedName ? 'w3-pale-green' : ''}
                    >
                      <td style={{ fontFamily: 'monospace' }}>{item.name}</td>
                      <td style={{ fontFamily: 'monospace' }}>{formatValue(item.value)}</td>
                      <td>{flags || '-'}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
            {!items.length ? <p className="w3-padding">No vars found.</p> : null}
          </div>
        </div>

        <div className="w3-half">
          <div className="w3-card w3-white" style={{ padding: '1rem' }}>
            <h3>{selectedItem ? selectedItem.name : 'Select a variable'}</h3>
            {selectedItem ? (
              <>
                <p style={{ marginTop: 0 }}>{selectedItem.description || 'No description provided.'}</p>
                <p>
                  <strong>Default:</strong>{' '}
                  <span style={{ fontFamily: 'monospace' }}>{formatValue(selectedItem.default)}</span>
                </p>
                {isReadOnlyVar ? (
                  <div className="w3-panel w3-pale-yellow">
                    This variable is marked read-only at runtime and cannot be changed from the UI.
                  </div>
                ) : null}
                {!access.canSet ? (
                  <div className="w3-panel w3-pale-yellow">
                    Your account can view vars, but it does not have permission to update them.
                  </div>
                ) : null}
                <label>Value (JSON)</label>
                <textarea
                  className="w3-input w3-border"
                  rows={12}
                  value={editorValue}
                  onChange={(event) => setEditorValue(event.target.value)}
                  readOnly={!canSaveSelected}
                />
                <div className="w3-bar" style={{ marginTop: '0.75rem' }}>
                  <button className="w3-button w3-green" onClick={() => void onSave()} disabled={saving || !canSaveSelected}>
                    <FaFloppyDisk style={{ marginRight: '0.4rem' }} />
                    {saving ? 'Saving...' : 'Save'}
                  </button>
                  <button
                    className="w3-button w3-gray"
                    style={{ marginLeft: '0.5rem' }}
                    onClick={onResetEditor}
                    disabled={saving}
                  >
                    <FaRotateLeft style={{ marginRight: '0.4rem' }} />
                    Reset editor
                  </button>
                  <button
                    className="w3-button w3-red"
                    style={{ marginLeft: '0.5rem' }}
                    onClick={() => void onUnset()}
                    disabled={saving || !canUnsetSelected}
                  >
                    <FaTrashCan style={{ marginRight: '0.4rem' }} />
                    Use default
                  </button>
                </div>
              </>
            ) : (
              <p>Select a variable from the table to inspect or edit it.</p>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
