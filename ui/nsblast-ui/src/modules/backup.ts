import type { ApiClientError } from './apiClient';

type ApiClientLike = {
  request: <T = unknown>(target: string, options?: Record<string, unknown>) => Promise<T>;
};

export type BackupAccess = {
  canList: boolean;
  canCreate: boolean;
};

function isApiStatus(error: unknown, status: number): boolean {
  return typeof error === 'object' && error !== null && (error as ApiClientError).status === status;
}

async function probeBackupCreate(api: ApiClientLike): Promise<boolean> {
  try {
    await api.request('/backup', {
      method: 'POST',
      body: [],
      parse: 'none',
      retry: false,
      retries: 0
    });
    return true;
  } catch (error) {
    if (isApiStatus(error, 400)) {
      return true;
    }
    if (isApiStatus(error, 403)) {
      return false;
    }
    throw error;
  }
}

export async function detectBackupAccess(api: ApiClientLike): Promise<BackupAccess> {
  let canList = false;

  try {
    await api.request('/backup', {
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

  const canCreate = await probeBackupCreate(api);

  return { canList, canCreate };
}
