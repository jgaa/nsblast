import { describe, expect, it, vi } from 'vitest';
import { ApiClientError } from './apiClient';
import { detectVarsAccess } from './vars';

function makeError(status: number, message = 'request failed') {
  return new ApiClientError(message, status, 'http://localhost/test', 'GET');
}

describe('detectVarsAccess', () => {
  it('detects list, read, set, and unset permissions independently', async () => {
    const request = vi.fn(async (target: string, options?: Record<string, unknown>) => {
      if (target === '/admin/vars') {
        return null;
      }
      if (target === '/admin/vars/cluster_role') {
        return null;
      }
      if (target === '/admin/vars/__nsblast_ui_probe__' && options?.method === 'PUT') {
        throw makeError(404);
      }
      if (target === '/admin/vars/__nsblast_ui_probe__' && options?.method === 'DELETE') {
        throw makeError(403);
      }
      throw makeError(500, 'unexpected');
    });

    await expect(detectVarsAccess({ request: request as never })).resolves.toEqual({
      canList: true,
      canRead: true,
      canSet: true,
      canUnset: false
    });
  });

  it('returns false when all vars permissions are denied', async () => {
    const request = vi.fn(async () => {
      throw makeError(403);
    });

    await expect(detectVarsAccess({ request: request as never })).resolves.toEqual({
      canList: false,
      canRead: false,
      canSet: false,
      canUnset: false
    });
  });
});
