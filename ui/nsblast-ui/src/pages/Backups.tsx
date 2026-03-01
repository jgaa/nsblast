import { useEffect, useMemo, useState } from 'react';
import { FaArrowsRotate, FaClockRotateLeft, FaDatabase } from 'react-icons/fa6';
import { useAppState } from '../modules/AppState';
import { detectBackupAccess, type BackupAccess } from '../modules/backup';

type BackupItem = {
  id: number;
  uuid?: string;
  timestamp?: number;
  size?: number;
  number_files?: number;
  date?: string;
};

type BackupReply = {
  value?: {
    backups?: BackupItem[];
    num_backups?: number;
  };
};

const initialAccess: BackupAccess = {
  canList: false,
  canCreate: false
};

function formatBytes(value?: number): string {
  if (!Number.isFinite(value)) {
    return '-';
  }

  const size = Number(value);
  if (size < 1024) {
    return `${size} B`;
  }
  if (size < 1024 * 1024) {
    return `${(size / 1024).toFixed(1)} KiB`;
  }
  if (size < 1024 * 1024 * 1024) {
    return `${(size / (1024 * 1024)).toFixed(1)} MiB`;
  }
  return `${(size / (1024 * 1024 * 1024)).toFixed(1)} GiB`;
}

export default function Backups() {
  const { api } = useAppState();
  const [access, setAccess] = useState<BackupAccess>(initialAccess);
  const [loading, setLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [creating, setCreating] = useState(false);
  const [items, setItems] = useState<BackupItem[]>([]);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');

  const canOpenPage = access.canList || access.canCreate;

  const load = async (isRefresh = false) => {
    if (isRefresh) {
      setRefreshing(true);
    } else {
      setLoading(true);
    }
    setError('');

    try {
      const nextAccess = await detectBackupAccess(api);
      setAccess(nextAccess);

      if (!nextAccess.canList) {
        setItems([]);
        return;
      }

      const response = (await api.request('/backup', { method: 'GET' })) as BackupReply;
      const nextItems = response.value?.backups ?? [];
      setItems(nextItems);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load backups');
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

  const onCreate = async () => {
    setCreating(true);
    setError('');
    setNotice('');

    try {
      const response = (await api.request('/backup', {
        method: 'POST',
        body: {}
      })) as { message?: string };
      setNotice(response.message ?? 'Backup operation was started.');
      await load(true);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to start backup');
    } finally {
      setCreating(false);
    }
  };

  const rows = useMemo(() => items, [items]);

  if (loading) {
    return <div className="w3-panel">Loading backups...</div>;
  }

  if (!canOpenPage) {
    return (
      <div className="w3-container" style={{ padding: '1rem' }}>
        <h1>Backups</h1>
        <div className="w3-panel w3-pale-red">Your account does not have backup management access.</div>
      </div>
    );
  }

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <div className="w3-bar">
        <h1 className="w3-bar-item" style={{ margin: 0, paddingLeft: 0 }}>
          <FaDatabase style={{ marginRight: '0.5rem' }} />
          Backups
        </h1>
        <button
          className="w3-button w3-blue w3-bar-item w3-right"
          onClick={() => void load(true)}
          disabled={refreshing || creating}
        >
          <FaArrowsRotate style={{ marginRight: '0.4rem' }} />
          {refreshing ? 'Refreshing...' : 'Refresh'}
        </button>
        {access.canCreate && (
          <button
            className="w3-button w3-green w3-bar-item w3-right"
            onClick={() => void onCreate()}
            disabled={creating || refreshing}
          >
            <FaClockRotateLeft style={{ marginRight: '0.4rem' }} />
            {creating ? 'Starting backup...' : 'Backup now'}
          </button>
        )}
      </div>

      {notice ? <div className="w3-panel w3-pale-green">{notice}</div> : null}
      {error ? <div className="w3-panel w3-pale-red">{error}</div> : null}

      {!access.canList && (
        <div className="w3-panel w3-pale-yellow">
          Your account can manage backups, but it does not have permission to list them.
        </div>
      )}

      <div className="w3-panel w3-pale-yellow">
        Backup restore is intentionally CLI-only. Stop the server before running a restore.
      </div>

      {access.canList && (
        <div className="w3-responsive w3-border">
          <table className="w3-table-all">
            <thead>
              <tr>
                <th>ID</th>
                <th>Date</th>
                <th>UUID</th>
                <th>Size</th>
                <th>Files</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((backup) => (
                <tr key={backup.id}>
                  <td>{backup.id}</td>
                  <td>{backup.date ?? '-'}</td>
                  <td style={{ fontFamily: 'monospace' }}>{backup.uuid ?? '-'}</td>
                  <td>{formatBytes(backup.size)}</td>
                  <td>{backup.number_files ?? '-'}</td>
                </tr>
              ))}
            </tbody>
          </table>

          {!rows.length && <p className="w3-padding">No backups found.</p>}
        </div>
      )}
    </div>
  );
}
