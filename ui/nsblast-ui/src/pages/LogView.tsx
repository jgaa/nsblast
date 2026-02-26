import { useEffect, useMemo, useState } from 'react';
import { useAppState } from '../modules/AppState';

type LogRow = {
  level?: string;
  line?: string;
};

type LogReply = {
  value?: LogRow[];
};

function normalizeLevel(level: string): string {
  const upper = level.toUpperCase();
  if (upper === 'WARNING') {
    return 'WARN';
  }
  if (upper === 'DEBUGGING') {
    return 'DEBUG';
  }
  return upper;
}

function levelColor(level: string): string {
  switch (normalizeLevel(level)) {
    case 'ERROR':
      return '#c62828';
    case 'WARN':
      return '#ef6c00';
    case 'INFO':
      return '#001f54';
    case 'DEBUG':
      return '#00695c';
    case 'TRACE':
      return '#616161';
    default:
      return '#212121';
  }
}

export default function LogView() {
  const { api } = useAppState();
  const [rows, setRows] = useState<LogRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [error, setError] = useState('');

  const load = async (isRefresh = false) => {
    if (isRefresh) {
      setRefreshing(true);
    } else {
      setLoading(true);
    }
    setError('');

    try {
      const response = (await api.request('/log/show', { method: 'GET' })) as LogReply;
      setRows(response.value ?? []);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load logs');
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

  const viewRows = useMemo(() => rows, [rows]);

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <div className="w3-bar">
        <h2 className="w3-bar-item" style={{ margin: 0, paddingLeft: 0 }}>Log</h2>
        <button
          className="w3-button w3-blue w3-bar-item w3-right"
          onClick={() => void load(true)}
          disabled={loading || refreshing}
        >
          {refreshing ? 'Refreshing...' : 'Refresh'}
        </button>
      </div>

      {loading && <p>Loading logs...</p>}
      {!loading && error && <p className="w3-text-red">{error}</p>}

      {!loading && !error && (
        <div className="w3-responsive w3-border" style={{ maxHeight: '70vh', overflowY: 'auto' }}>
          <table className="w3-table-all w3-small">
            <thead>
              <tr>
                <th>Message</th>
              </tr>
            </thead>
            <tbody>
              {viewRows.map((row, index) => {
                const level = normalizeLevel(row.level ?? 'INFO');
                return (
                  <tr key={`${index}-${row.line ?? ''}`}>
                    <td style={{ color: levelColor(level), fontFamily: 'monospace' }}>{row.line ?? ''}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          {!viewRows.length && <p className="w3-padding">No log lines captured.</p>}
        </div>
      )}
    </div>
  );
}
