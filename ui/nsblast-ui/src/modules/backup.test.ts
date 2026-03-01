import { describe, expect, it, vi } from 'vitest';
import { ApiClientError } from './apiClient';
import { detectBackupAccess } from './backup';

function makeError(status: number, message = 'request failed') {
  return new ApiClientError(message, status, 'http://localhost/test', 'GET');
}

describe('detectBackupAccess', () => {
  it('detects full backup access when probes pass validation', async () => {
    const request = vi.fn(async (target: string, options?: Record<string, unknown>) => {
      if (target === '/backup') {
        if (options?.method === 'POST') {
          expect(options.body).toEqual({ dryRun: true });
        }
        return null;
      }
      throw makeError(400);
    });

    const access = await detectBackupAccess({ request: request as never });

    expect(access).toEqual({
      canList: true,
      canCreate: true
    });
  });

  it('returns false when backup permissions are denied', async () => {
    const request = vi.fn(async () => {
      throw makeError(403);
    });

    const access = await detectBackupAccess({ request: request as never });

    expect(access).toEqual({
      canList: false,
      canCreate: false
    });
  });

  it('propagates unexpected list probe failures', async () => {
    const request = vi.fn(async (target: string) => {
      if (target === '/backup') {
        throw makeError(500, 'boom');
      }
      throw makeError(403);
    });

    await expect(detectBackupAccess({ request: request as never })).rejects.toThrow('boom');
  });
});
