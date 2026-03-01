import type { ApiClientError } from './apiClient';

type ApiClientLike = {
  request: <T = unknown>(target: string, options?: Record<string, unknown>) => Promise<T>;
};

export type DynIpAccess = {
  canList: boolean;
  canProvision: boolean;
  canDeleteHosts: boolean;
};

const INVALID_PROBE_LABEL = 'nsblast_ui_probe';

function isApiStatus(error: unknown, status: number): boolean {
  return typeof error === 'object' && error !== null && (error as ApiClientError).status === status;
}

async function probeDeletePath(api: ApiClientLike, target: string): Promise<boolean> {
  try {
    await api.request(target, {
      method: 'DELETE',
      parse: 'none',
      retry: false,
      retries: 0
    });
    return false;
  } catch (error) {
    if (isApiStatus(error, 400)) {
      return true;
    }
    if (isApiStatus(error, 403)) {
      return false;
    }
    return false;
  }
}

export async function detectDynIpAccess(api: ApiClientLike): Promise<DynIpAccess> {
  let canList = false;

  try {
    await api.request('/dynip', {
      method: 'GET',
      parse: 'none',
      retry: false,
      retries: 0
    });
    canList = true;
  } catch (error) {
    if (!isApiStatus(error, 403)) {
      throw error;
    }
  }

  const [canProvision, canDeleteHosts] = await Promise.all([
    probeDeletePath(api, `/dynip/${INVALID_PROBE_LABEL}`),
    probeDeletePath(api, `/dynip/${INVALID_PROBE_LABEL}/${INVALID_PROBE_LABEL}`)
  ]);

  return { canList, canProvision, canDeleteHosts };
}
