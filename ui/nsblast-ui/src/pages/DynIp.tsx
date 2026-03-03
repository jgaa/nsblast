import { useEffect, useMemo, useState } from 'react';
import { FaArrowsRotate, FaCopy, FaKey, FaNetworkWired, FaTrashCan } from 'react-icons/fa6';
import { useAppState } from '../modules/AppState';
import { ApiClientError } from '../modules/apiClient';
import { detectDynIpAccess, type DynIpAccess } from '../modules/dynip';
import PopupDialog from '../modules/PopupDialog';
import { encodePathSegment } from '../modules/url';

type DynIpRoot = {
  root: string;
  fqdn: string;
  host_limit: number;
};

type DynIpHost = {
  host: string;
  fqdn: string;
  ttl: number;
  update_count: number;
  last_update?: string | null;
  disabled: boolean;
};

type DynIpHostView = DynIpHost & {
  ips: string[];
  ipState: 'ready' | 'empty' | 'restricted' | 'missing';
};

type DynIpRootView = DynIpRoot & {
  hosts: DynIpHostView[];
};

type NewHostForm = {
  host: string;
  ttl: string;
};

type DynIpTokenPayload = {
  fqdn?: string;
  token?: string;
  update_url?: string;
  ttl?: number;
};

type AvailabilityState =
  | { kind: 'idle'; message: string }
  | { kind: 'checking'; message: string }
  | { kind: 'invalid'; message: string }
  | { kind: 'available'; message: string }
  | { kind: 'taken'; message: string };

const initialAccess: DynIpAccess = {
  canList: false,
  canProvision: false,
  canDeleteHosts: false
};

const initialAvailability: AvailabilityState = {
  kind: 'idle',
  message: 'Enter a root label to check availability.'
};

const DYNIP_MIN_ROOT_LEN = 3;
const DYNIP_MAX_ROOT_LEN = 24;

function parseItems<T>(payload: unknown): T[] {
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) {
    return [];
  }
  const items = (payload as { items?: unknown }).items;
  return Array.isArray(items) ? (items as T[]) : [];
}

function normalizeRootLabel(value: string): string {
  return value.trim().toLowerCase();
}

function isValidDnsLabel(value: string): boolean {
  if (!value.length || value.length > 63) {
    return false;
  }
  if (value.startsWith('-') || value.endsWith('-')) {
    return false;
  }
  return /^[a-z0-9-]+$/.test(value);
}

function isValidDynIpRootLabel(value: string): boolean {
  return value.length >= DYNIP_MIN_ROOT_LEN
    && value.length <= DYNIP_MAX_ROOT_LEN
    && isValidDnsLabel(value);
}

function formatDynIpError(err: unknown, fallback: string): string {
  return err instanceof Error ? err.message : fallback;
}

function extractIps(payload: unknown): string[] {
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) {
    return [];
  }
  const value = (payload as { value?: Record<string, unknown> }).value;
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return [];
  }

  const ips: string[] = [];
  for (const key of ['a', 'aaaa', 'A', 'AAAA']) {
    const entry = value[key];
    if (Array.isArray(entry)) {
      for (const item of entry) {
        if (typeof item === 'string' && item.trim().length) {
          ips.push(item);
        }
      }
    }
  }

  return ips;
}

export function formatDynIpTimestamp(value?: string | null): string {
  if (!value) {
    return 'Never';
  }

  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }

  return new Intl.DateTimeFormat(undefined, {
    year: 'numeric',
    month: 'short',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  }).format(date);
}

export default function DynIp() {
  const { api } = useAppState();
  const [access, setAccess] = useState<DynIpAccess>(initialAccess);
  const [accessLoaded, setAccessLoaded] = useState(false);
  const [roots, setRoots] = useState<DynIpRootView[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [rootName, setRootName] = useState('');
  const [hostLimit, setHostLimit] = useState('');
  const [creating, setCreating] = useState(false);
  const [creatingHostFor, setCreatingHostFor] = useState('');
  const [hostForms, setHostForms] = useState<Record<string, NewHostForm>>({});
  const [tokenPopup, setTokenPopup] = useState<DynIpTokenPayload | null>(null);
  const [copyNotice, setCopyNotice] = useState('');
  const [availability, setAvailability] = useState<AvailabilityState>(initialAvailability);

  const canUseDynIp = access.canList && access.canProvision;
  const canManageHosts = access.canProvision || access.canDeleteHosts;

  const loadData = async () => {
    setLoading(true);
    setError('');

    try {
      const nextAccess = await detectDynIpAccess(api);
      setAccess(nextAccess);
      setAccessLoaded(true);

      if (!nextAccess.canList) {
        setRoots([]);
        return;
      }

      const rootsResponse = await api.request('/dynip');
      const rootItems = parseItems<DynIpRoot>(rootsResponse);

      const rootViews = await Promise.all(
        rootItems.map(async (root) => {
          const hostsResponse = await api.request(`/dynip/${encodePathSegment(root.root)}/hosts`);
          const hostItems = parseItems<DynIpHost>(hostsResponse);

          const hosts = await Promise.all(
            hostItems.map(async (host) => {
              try {
                const rr = await api.request(`/rr/${encodePathSegment(host.fqdn)}`);
                const ips = extractIps(rr);
                return {
                  ...host,
                  ips,
                  ipState: ips.length ? 'ready' : 'empty'
                } satisfies DynIpHostView;
              } catch (err) {
                if (err instanceof ApiClientError && err.status === 403) {
                  return {
                    ...host,
                    ips: [],
                    ipState: 'restricted'
                  } satisfies DynIpHostView;
                }
                if (err instanceof ApiClientError && err.status === 404) {
                  return {
                    ...host,
                    ips: [],
                    ipState: 'missing'
                  } satisfies DynIpHostView;
                }
                throw err;
              }
            })
          );

          return {
            ...root,
            hosts
          } satisfies DynIpRootView;
        })
      );

      setRoots(rootViews);
    } catch (err) {
      setError(formatDynIpError(err, 'Failed to load DynIP data'));
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void loadData();
  }, []);

  useEffect(() => {
    const normalized = normalizeRootLabel(rootName);
    if (!normalized) {
      setAvailability(initialAvailability);
      return;
    }

    if (!isValidDynIpRootLabel(normalized)) {
      setAvailability({
        kind: 'invalid',
        message: `Root labels must be ${DYNIP_MIN_ROOT_LEN}-${DYNIP_MAX_ROOT_LEN} chars of a-z, 0-9, or hyphen.`
      });
      return;
    }

    let active = true;
    setAvailability({
      kind: 'checking',
      message: 'Checking root availability...'
    });

    const timer = window.setTimeout(async () => {
      try {
        await api.request(`/dynip/${encodePathSegment(normalized)}/hosts`, {
          method: 'GET',
          parse: 'none',
          retry: false,
          retries: 0
        });
        if (active) {
          setAvailability({
            kind: 'taken',
            message: 'This DynIP root is already in use.'
          });
        }
      } catch (err) {
        if (!(err instanceof ApiClientError)) {
          if (active) {
            setAvailability({
              kind: 'invalid',
              message: err instanceof Error ? err.message : 'Failed to check availability.'
            });
          }
          return;
        }

        if (!active) {
          return;
        }

        if (err.status === 404) {
          setAvailability({
            kind: 'available',
            message: 'This DynIP root is available.'
          });
          return;
        }

        if (err.status === 400) {
          setAvailability({
            kind: 'taken',
            message: 'This DynIP root is already in use by another tenant.'
          });
          return;
        }

        if (err.status === 403) {
          setAvailability({
            kind: 'invalid',
            message: 'Your account cannot verify DynIP root availability.'
          });
          return;
        }

        setAvailability({
          kind: 'invalid',
          message: err.message || 'Failed to check availability.'
        });
      }
    }, 250);

    return () => {
      active = false;
      window.clearTimeout(timer);
    };
  }, [api, rootName]);

  const availabilityClassName = useMemo(() => {
    switch (availability.kind) {
      case 'available':
        return 'w3-text-green';
      case 'taken':
      case 'invalid':
        return 'w3-text-red';
      default:
        return 'w3-text-blue-grey';
    }
  }, [availability.kind]);

  const onCreateRoot = async () => {
    const normalized = normalizeRootLabel(rootName);
    if (!isValidDynIpRootLabel(normalized)) {
      setError(`DynIP root labels must be ${DYNIP_MIN_ROOT_LEN}-${DYNIP_MAX_ROOT_LEN} chars of a-z, 0-9, or hyphen.`);
      return;
    }
    if (!access.canProvision) {
      setError('Your account cannot provision DynIP roots.');
      return;
    }

    const payload: Record<string, unknown> = {};
    if (hostLimit.trim().length) {
      const parsed = Number.parseInt(hostLimit, 10);
      if (!Number.isFinite(parsed) || parsed <= 0) {
        setError('Host limit must be a positive integer.');
        return;
      }
      payload.host_limit = parsed;
    }

    setCreating(true);
    setError('');
    setNotice('');
    try {
      await api.request(`/dynip/${encodePathSegment(normalized)}`, {
        method: 'POST',
        body: payload
      });
      setRootName('');
      setHostLimit('');
      setAvailability(initialAvailability);
      setNotice(`Provisioned DynIP root ${normalized}.`);
      await loadData();
    } catch (err) {
      setError(formatDynIpError(err, 'Failed to provision DynIP root'));
    } finally {
      setCreating(false);
    }
  };

  const getHostForm = (root: string): NewHostForm => hostForms[root] ?? { host: '', ttl: '' };

  const setHostForm = (root: string, next: NewHostForm) => {
    setHostForms((current) => ({ ...current, [root]: next }));
  };

  const onCreateHost = async (root: DynIpRootView) => {
    const form = getHostForm(root.root);
    const host = normalizeRootLabel(form.host);
    if (!isValidDnsLabel(host)) {
      setError('A valid DynIP host label is required.');
      return;
    }
    if (!canManageHosts) {
      setError('Your account cannot provision DynIP hosts.');
      return;
    }

    const payload: Record<string, unknown> = {};
    if (form.ttl.trim().length) {
      const parsed = Number.parseInt(form.ttl, 10);
      if (!Number.isFinite(parsed) || parsed <= 0) {
        setError('Host TTL must be a positive integer.');
        return;
      }
      payload.ttl = parsed;
    }

    setCreatingHostFor(root.root);
    setError('');
    setNotice('');
    try {
      const created = (await api.request(`/dynip/${encodePathSegment(root.root)}/${encodePathSegment(host)}`, {
        method: 'POST',
        body: payload
      })) as DynIpTokenPayload;
      setHostForm(root.root, { host: '', ttl: '' });
      setTokenPopup(created);
      setCopyNotice('');
      setNotice(`Created DynIP host ${created.fqdn ?? host}.`);
      await loadData();
    } catch (err) {
      setError(formatDynIpError(err, 'Failed to create DynIP host'));
    } finally {
      setCreatingHostFor('');
    }
  };

  const onRotateHostToken = async (root: DynIpRootView, host: DynIpHostView) => {
    if (!canManageHosts) {
      setError('Your account cannot create a new password for DynIP hosts.');
      return;
    }

    if (!window.confirm(`Create a new password for "${host.fqdn}"? The current token will stop working.`)) {
      return;
    }

    setError('');
    setNotice('');
    try {
      const rotated = (await api.request(
        `/dynip/${encodePathSegment(root.root)}/${encodePathSegment(host.host)}`,
        {
          method: 'PUT',
          body: {}
        }
      )) as DynIpTokenPayload;
      setTokenPopup(rotated);
      setCopyNotice('');
      setNotice(`Created a new password for ${host.fqdn}.`);
      await loadData();
    } catch (err) {
      setError(formatDynIpError(err, 'Failed to create a new password for the DynIP host'));
    }
  };

  const onCopyToken = async () => {
    if (!tokenPopup?.token) {
      return;
    }

    try {
      await navigator.clipboard.writeText(tokenPopup.token);
      setCopyNotice('Copied token to clipboard.');
    } catch {
      setCopyNotice('Clipboard copy failed. Copy the token manually.');
    }
  };

  const onDeleteRoot = async (root: DynIpRootView) => {
    if (!access.canProvision) {
      setError('Your account cannot delete DynIP roots.');
      return;
    }
    if (!window.confirm(`Delete DynIP root "${root.fqdn}" and all hosts under it?`)) {
      return;
    }

    setError('');
    setNotice('');
    try {
      await api.request(`/dynip/${encodePathSegment(root.root)}`, {
        method: 'DELETE',
        parse: 'none'
      });
      setNotice(`Deleted DynIP root ${root.fqdn}.`);
      await loadData();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete DynIP root');
    }
  };

  const onDeleteHost = async (root: DynIpRootView, host: DynIpHostView) => {
    if (!canManageHosts) {
      setError('Your account cannot delete DynIP hosts.');
      return;
    }
    if (!window.confirm(`Delete DynIP host "${host.fqdn}"?`)) {
      return;
    }

    setError('');
    setNotice('');
    try {
      await api.request(`/dynip/${encodePathSegment(root.root)}/${encodePathSegment(host.host)}`, {
        method: 'DELETE',
        parse: 'none'
      });
      setNotice(`Deleted DynIP host ${host.fqdn}.`);
      await loadData();
    } catch (err) {
      setError(formatDynIpError(err, 'Failed to delete DynIP host'));
    }
  };

  const renderIps = (host: DynIpHostView) => {
    if (host.ipState === 'restricted') {
      return <span className="w3-text-orange">RR read not granted</span>;
    }
    if (host.ipState === 'missing') {
      return <span className="w3-text-red">No RR currently exists</span>;
    }
    if (!host.ips.length) {
      return <span className="w3-text-grey">No address set</span>;
    }
    return host.ips.join(', ');
  };

  if (loading && !accessLoaded) {
    return <div className="w3-panel">Loading DynIP...</div>;
  }

  if (accessLoaded && !canUseDynIp) {
    return (
      <div className="w3-container" style={{ padding: '1rem' }}>
        <h1>DynIP</h1>
        <div className="w3-panel w3-red">
          DynIP management requires both <code>DYNIP_LIST</code> and <code>DYNIP_PROVISION</code>.
        </div>
      </div>
    );
  }

  return (
    <div className="w3-container" style={{ padding: '1rem' }}>
      <div className="w3-bar">
        <h1 className="w3-bar-item" style={{ margin: 0, paddingLeft: 0 }}>
          DynIP
        </h1>
        <button className="w3-button w3-blue w3-bar-item w3-right" onClick={() => void loadData()}>
          <FaArrowsRotate /> Reload
        </button>
      </div>

      {error ? <div className="w3-panel w3-red">{error}</div> : null}
      {notice ? <div className="w3-panel w3-pale-green">{notice}</div> : null}

      <div className="w3-card w3-white" style={{ padding: '1rem', marginBottom: '1rem' }}>
        <h3 style={{ marginTop: 0 }}>Provision DynIP Root</h3>
        <p style={{ marginTop: 0 }}>
          A <strong>root</strong> is the shared ending for a group of updatable hostnames.
          If your DynIP realm is <code>dyn.example.com</code> and the root is <code>home</code>,
          you can then create names like <code>camera.home.dyn.example.com</code> and
          <code>router.home.dyn.example.com</code>.
        </p>
        <label>Root label</label>
        <input
          className="w3-input w3-border"
          value={rootName}
          onChange={(event) => setRootName(event.target.value)}
          placeholder="home"
        />
        <p className={availabilityClassName} style={{ marginTop: '0.5rem', marginBottom: '0.75rem' }}>
          {availability.message}
        </p>
        <label>Host limit (optional)</label>
        <input
          className="w3-input w3-border"
          value={hostLimit}
          onChange={(event) => setHostLimit(event.target.value)}
          placeholder="Use server default"
        />
        <button
          className="w3-button w3-green"
          style={{ marginTop: '0.75rem' }}
          onClick={onCreateRoot}
          disabled={creating || availability.kind !== 'available'}
        >
          Provision Root
        </button>
      </div>

      {loading ? <div className="w3-panel">Refreshing DynIP roots...</div> : null}

      {!loading && !roots.length ? (
        <div className="w3-panel w3-pale-blue">No DynIP roots provisioned for this tenant.</div>
      ) : null}

      {roots.map((root) => (
        <div key={root.fqdn} className="w3-card w3-white" style={{ marginBottom: '1rem' }}>
          <div className="w3-container w3-blue">
            <div className="w3-bar">
              <h3 className="w3-bar-item" style={{ margin: 0, paddingLeft: 0 }}>
                <FaNetworkWired /> {root.fqdn}
              </h3>
              <button
                className="w3-button w3-red w3-bar-item w3-right"
                onClick={() => void onDeleteRoot(root)}
              >
                <FaTrashCan /> Delete Root
              </button>
            </div>
          </div>
          <div className="w3-container" style={{ paddingTop: '0.75rem', paddingBottom: '0.75rem' }}>
            <p style={{ marginTop: 0, marginBottom: '0.5rem' }}>
              <strong>Root:</strong> {root.root}
            </p>
            <p style={{ marginTop: 0, marginBottom: '0.5rem' }}>
              <strong>FQDN:</strong> {root.fqdn}
            </p>
            <p style={{ marginTop: 0 }}>
              <strong>Host limit:</strong> {root.host_limit}
            </p>

            <div className="w3-card w3-theme-l4" style={{ padding: '1rem', marginBottom: '1rem' }}>
              <h4 style={{ marginTop: 0 }}>Add Host</h4>
              <p style={{ marginTop: 0 }}>
                Create a hostname under <code>{root.fqdn}</code>. For example, host label
                <code> camera</code> becomes <code>camera.{root.fqdn}</code>.
              </p>
              <div className="w3-row-padding">
                <div className="w3-half">
                  <label>Host label</label>
                  <input
                    className="w3-input w3-border"
                    value={getHostForm(root.root).host}
                    onChange={(event) =>
                      setHostForm(root.root, {
                        ...getHostForm(root.root),
                        host: event.target.value
                      })
                    }
                    placeholder="camera"
                  />
                </div>
                <div className="w3-half">
                  <label>TTL (optional)</label>
                  <input
                    className="w3-input w3-border"
                    value={getHostForm(root.root).ttl}
                    onChange={(event) =>
                      setHostForm(root.root, {
                        ...getHostForm(root.root),
                        ttl: event.target.value
                      })
                    }
                    placeholder="Use server default"
                  />
                </div>
              </div>
              <button
                className="w3-button w3-green"
                style={{ marginTop: '0.75rem' }}
                onClick={() => void onCreateHost(root)}
                disabled={creatingHostFor === root.root || !canManageHosts}
              >
                {creatingHostFor === root.root ? 'Creating host...' : 'Create Host'}
              </button>
            </div>

            <div className="w3-responsive">
              <table className="w3-table-all w3-small">
                <thead>
                  <tr>
                    <th>Host</th>
                    <th>Host FQDN</th>
                    <th>IPs</th>
                    <th>TTL</th>
                    <th>Recorded Updates</th>
                    <th>Last Change</th>
                    <th>Action</th>
                  </tr>
                </thead>
                <tbody>
                  {!root.hosts.length ? (
                    <tr>
                      <td colSpan={7} className="w3-center">
                        No DynIP hosts under this root.
                      </td>
                    </tr>
                  ) : null}
                  {root.hosts.map((host) => (
                    <tr key={host.fqdn}>
                      <td>{host.host}</td>
                      <td>{host.fqdn}</td>
                      <td>{renderIps(host)}</td>
                      <td>{host.ttl}</td>
                      <td>{host.update_count}</td>
                      <td>{formatDynIpTimestamp(host.last_update)}</td>
                      <td>
                        <button
                          className="w3-button w3-blue w3-small"
                          style={{ marginRight: '0.5rem' }}
                          onClick={() => void onRotateHostToken(root, host)}
                        >
                          <FaKey /> New Password
                        </button>
                        <button
                          className="w3-button w3-red w3-small"
                          onClick={() => void onDeleteHost(root, host)}
                        >
                          <FaTrashCan /> Delete
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      ))}

      <PopupDialog isOpen={Boolean(tokenPopup)} onClosed={() => setTokenPopup(null)}>
        <h3>DynIP Token</h3>
        <p>
          Save this token now. When you close this popup, the bearer token will not be shown again.
        </p>
        <p>
          You can use it as a <strong>Bearer token</strong> in the <code>Authorization</code> header,
          or as HTTP login credentials with the full hostname as the username and the token as the password.
        </p>
        <p>
          Full hostname username:
          <br />
          <code>{tokenPopup?.fqdn ?? '-'}</code>
        </p>
        <p>
          Token / password:
          <br />
          <code style={{ wordBreak: 'break-all' }}>{tokenPopup?.token ?? '-'}</code>
        </p>
        {copyNotice ? <div className="w3-panel w3-pale-green">{copyNotice}</div> : null}
        <div className="w3-bar">
          <button className="w3-button w3-blue" onClick={() => void onCopyToken()}>
            <FaCopy /> Copy Token
          </button>
          <button
            className="w3-button w3-gray"
            style={{ marginLeft: '0.5rem' }}
            onClick={() => setTokenPopup(null)}
          >
            Close
          </button>
        </div>
      </PopupDialog>
    </div>
  );
}
