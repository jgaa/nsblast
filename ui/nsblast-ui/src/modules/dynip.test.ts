import { describe, expect, it, vi } from 'vitest';
import { ApiClientError } from './apiClient';
import { detectDynIpAccess } from './dynip';

function makeError(status: number, message = 'request failed') {
  return new ApiClientError(message, status, 'http://localhost/test', 'GET');
}

describe('detectDynIpAccess', () => {
  it('grants DynIP navigation only when list and provision probes pass', async () => {
    const request = vi.fn(async (target: string) => {
      if (target === '/dynip') {
        return null;
      }
      throw makeError(400);
    });

    const access = await detectDynIpAccess({ request: request as never });

    expect(access).toEqual({
      canList: true,
      canProvision: true,
      canDeleteHosts: true
    });
  });

  it('treats DynIP provision access as sufficient for host deletion', async () => {
    const request = vi.fn(async (target: string) => {
      if (target === '/dynip') {
        return null;
      }
      if (target === '/dynip/nsblast_ui_probe') {
        throw makeError(400);
      }
      if (target === '/dynip/nsblast_ui_probe/nsblast_ui_probe') {
        throw makeError(403);
      }
      throw makeError(500, 'unexpected');
    });

    const access = await detectDynIpAccess({ request: request as never });

    expect(access).toEqual({
      canList: true,
      canProvision: true,
      canDeleteHosts: true
    });
  });

  it('returns false when DynIP permissions are denied', async () => {
    const request = vi.fn(async (target: string) => {
      throw makeError(target === '/dynip' ? 403 : 403);
    });

    const access = await detectDynIpAccess({ request: request as never });

    expect(access).toEqual({
      canList: false,
      canProvision: false,
      canDeleteHosts: false
    });
  });

  it('propagates unexpected DynIP list probe failures', async () => {
    const request = vi.fn(async (target: string) => {
      if (target === '/dynip') {
        throw makeError(500, 'boom');
      }
      throw makeError(403);
    });

    await expect(detectDynIpAccess({ request: request as never })).rejects.toThrow('boom');
  });
});
