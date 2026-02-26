import { useEffect, useMemo, useState } from 'react';
import { useAppState } from '../modules/AppState';

type VersionResponse = Record<string, unknown>;

type VersionRow = {
  key: string;
  value: string;
};

function stringifyValue(value: unknown): string {
  if (value === null || value === undefined) {
    return '';
  }

  if (typeof value === 'string') {
    return value;
  }

  if (typeof value === 'number' || typeof value === 'boolean') {
    return String(value);
  }

  try {
    return JSON.stringify(value);
  } catch {
    return String(value);
  }
}

function formatUptimeFromSeconds(totalSeconds: number): string {
  const secondsSafe = Math.max(0, Math.floor(totalSeconds));
  const days = Math.floor(secondsSafe / 86400);
  const hours = Math.floor((secondsSafe % 86400) / 3600);
  const minutes = Math.floor((secondsSafe % 3600) / 60);
  const seconds = secondsSafe % 60;

  const parts: string[] = [];
  if (days > 0) {
    parts.push(`${days}d`);
  }
  if (hours > 0 || days > 0) {
    parts.push(`${hours}h`);
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    parts.push(`${minutes}m`);
  }
  parts.push(`${seconds}s`);
  return parts.join(' ');
}

function getVersionPayload(response: VersionResponse): VersionResponse {
  const payload = response.value;
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) {
    return response;
  }

  const normalizedPayload: VersionResponse = { ...(payload as VersionResponse) };
  const uptimeSeconds = normalizedPayload.uptime_seconds;
  if (typeof uptimeSeconds === 'number' && Number.isFinite(uptimeSeconds)) {
    normalizedPayload.uptime = formatUptimeFromSeconds(uptimeSeconds);
  }

  return normalizedPayload;
}

function formatFieldName(key: string): string {
  const trimmed = key.startsWith('value.') ? key.slice('value.'.length) : key;
  if (!trimmed.length) {
    return 'Value';
  }
  return `${trimmed.charAt(0).toUpperCase()}${trimmed.slice(1)}`;
}

function flattenVersionData(data: VersionResponse): VersionRow[] {
  const rows: VersionRow[] = [];

  const walk = (value: unknown, prefix = '') => {
    if (Array.isArray(value)) {
      value.forEach((item, index) => {
        walk(item, `${prefix}[${index}]`);
      });
      return;
    }

    if (value && typeof value === 'object') {
      const obj = value as Record<string, unknown>;
      Object.entries(obj).forEach(([key, nestedValue]) => {
        const fullKey = prefix ? `${prefix}.${key}` : key;
        walk(nestedValue, fullKey);
      });
      return;
    }

    rows.push({
      key: formatFieldName(prefix || 'value'),
      value: stringifyValue(value)
    });
  };

  walk(data);
  return rows;
}

export default function About() {
  const { api } = useAppState();
  const [version, setVersion] = useState<VersionResponse | null>(null);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;

    const loadVersion = async () => {
      try {
        const response = (await api.request('/version', {
          method: 'GET'
        })) as VersionResponse;
        if (active) {
          setVersion(getVersionPayload(response));
        }
      } catch (err) {
        if (active) {
          setError(err instanceof Error ? err.message : 'Failed to load version details');
        }
      } finally {
        if (active) {
          setLoading(false);
        }
      }
    };

    loadVersion();

    return () => {
      active = false;
    };
  }, [api]);

  const rows = useMemo(() => {
    if (!version) {
      return [];
    }
    return flattenVersionData(version);
  }, [version]);

  return (
    <div className="w3-panel w3-card w3-white" style={{ margin: '1rem' }}>
      <h2>About nsBLAST</h2>

      {loading && <p>Loading version details...</p>}
      {!loading && error && <p className="w3-text-red">{error}</p>}

      {!loading && !error && (
        <div className="w3-responsive">
          <table className="w3-table-all w3-small">
            <thead>
              <tr>
                <th>Field</th>
                <th>Value</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => (
                <tr key={row.key}>
                  <td>{row.key}</td>
                  <td>{row.value}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      <div style={{ marginTop: '1rem' }}>
        <a href="https://www.nsblast.com" target="_blank" rel="noreferrer">www.nsblast.com</a>
        <span> | </span>
        <a href="https://github.com/jgaa/nsblast" target="_blank" rel="noreferrer">github.com/jgaa/nsblast</a>
      </div>
    </div>
  );
}
