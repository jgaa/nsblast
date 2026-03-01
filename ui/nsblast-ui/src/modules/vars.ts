import type { ApiClientError } from './apiClient';

type ApiClientLike = {
  request: <T = unknown>(target: string, options?: Record<string, unknown>) => Promise<T>;
};

export type VarsAccess = {
  canList: boolean;
  canRead: boolean;
  canSet: boolean;
  canUnset: boolean;
};

const PROBE_VAR_NAME = '__nsblast_ui_probe__';
const KNOWN_VAR_NAME = 'cluster_role';

function isApiStatus(error: unknown, status: number): boolean {
  return typeof error === 'object' && error !== null && (error as ApiClientError).status === status;
}

async function probeCanRead(api: ApiClientLike): Promise<boolean> {
  try {
    await api.request(`/admin/vars/${KNOWN_VAR_NAME}`, {
      method: 'GET',
      parse: 'none',
      retry: false,
      retries: 0
    });
    return true;
  } catch (error) {
    if (isApiStatus(error, 403)) {
      return false;
    }
    if (isApiStatus(error, 404)) {
      return true;
    }
    throw error;
  }
}

async function probeCanSet(api: ApiClientLike): Promise<boolean> {
  try {
    await api.request(`/admin/vars/${PROBE_VAR_NAME}`, {
      method: 'PUT',
      body: { value: true },
      parse: 'none',
      retry: false,
      retries: 0
    });
    return true;
  } catch (error) {
    if (isApiStatus(error, 403)) {
      return false;
    }
    if (isApiStatus(error, 404)) {
      return true;
    }
    throw error;
  }
}

async function probeCanUnset(api: ApiClientLike): Promise<boolean> {
  try {
    await api.request(`/admin/vars/${PROBE_VAR_NAME}`, {
      method: 'DELETE',
      parse: 'none',
      retry: false,
      retries: 0
    });
    return true;
  } catch (error) {
    if (isApiStatus(error, 403)) {
      return false;
    }
    if (isApiStatus(error, 404)) {
      return true;
    }
    throw error;
  }
}

export async function detectVarsAccess(api: ApiClientLike): Promise<VarsAccess> {
  let canList = false;

  try {
    await api.request('/admin/vars', {
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

  const [canRead, canSet, canUnset] = await Promise.all([
    probeCanRead(api),
    probeCanSet(api),
    probeCanUnset(api)
  ]);

  return { canList, canRead, canSet, canUnset };
}
