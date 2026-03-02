import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';
import { AppStateContext } from '../modules/AppState';
import { ApiClientError } from '../modules/apiClient';
import DynIp, { formatDynIpTimestamp } from './DynIp';

function renderDynIp(request: (target: string, options?: Record<string, unknown>) => Promise<unknown>) {
  return render(
    <AppStateContext.Provider value={{ api: { request } }}>
      <DynIp />
    </AppStateContext.Provider>
  );
}

describe('DynIp', () => {
  it('formats the last change timestamp for display', () => {
    expect(formatDynIpTimestamp('2026-03-02T18:20:30Z')).not.toBe('2026-03-02T18:20:30Z');
    expect(formatDynIpTimestamp('')).toBe('Never');
  });

  it('keeps root provisioning disabled when the root label is outside the supported length bounds', async () => {
    const request = vi.fn(async (target: string, options?: Record<string, unknown>) => {
      if (target === '/dynip' && options?.method === 'GET') {
        return null;
      }
      if (target === '/dynip') {
        return { items: [] };
      }
      if (target.startsWith('/dynip/nsblast_ui_probe')) {
        throw new ApiClientError('probe', 400, 'http://localhost/test', 'DELETE');
      }
      throw new Error(`unexpected request: ${target}`);
    });

    renderDynIp(request);

    expect(await screen.findByText('Provision DynIP Root')).toBeInTheDocument();

    const input = screen.getByPlaceholderText('home');
    const button = screen.getByRole('button', { name: 'Provision Root' });

    await userEvent.type(input, 'ab');

    await waitFor(() => {
      expect(screen.getByText('Root labels must be 3-24 chars of a-z, 0-9, or hyphen.')).toBeInTheDocument();
    });
    expect(button).toBeDisabled();

    await userEvent.clear(input);
    await userEvent.type(input, 'abcdefghijklmnopqrstuvwxy');

    await waitFor(() => {
      expect(screen.getByText('Root labels must be 3-24 chars of a-z, 0-9, or hyphen.')).toBeInTheDocument();
    });
    expect(button).toBeDisabled();
    expect(request.mock.calls.some(([target]) => target === '/dynip/ab/hosts')).toBe(false);
    expect(request.mock.calls.some(([target]) => target === '/dynip/abcdefghijklmnopqrstuvwxy/hosts')).toBe(false);
  });

  it('hides the disabled column and shows last change for DynIP hosts', async () => {
    const request = vi.fn(async (target: string, options?: Record<string, unknown>) => {
      if (target === '/dynip' && options?.method === 'GET') {
        return null;
      }
      if (target === '/dynip') {
        return {
          items: [
            {
              root: 'home',
              fqdn: 'home.dyn.example.com',
              host_limit: 8
            }
          ]
        };
      }
      if (target === '/dynip/home/hosts') {
        return {
          items: [
            {
              host: 'router',
              fqdn: 'router.home.dyn.example.com',
              ttl: 300,
              update_count: 0,
              disabled: false,
              last_update: '2026-03-02T18:20:30Z'
            }
          ]
        };
      }
      if (target === '/rr/router.home.dyn.example.com') {
        return { value: { a: ['203.0.113.10'] } };
      }
      if (target.startsWith('/dynip/nsblast_ui_probe')) {
        throw new ApiClientError('probe', 400, 'http://localhost/test', 'DELETE');
      }
      throw new Error(`unexpected request: ${target}`);
    });

    renderDynIp(request);

    expect(await screen.findByText('router.home.dyn.example.com')).toBeInTheDocument();
    expect(screen.getByText('Recorded Updates')).toBeInTheDocument();
    expect(screen.getByText('Last Change')).toBeInTheDocument();
    expect(screen.queryByText('Disabled')).not.toBeInTheDocument();
    expect(screen.queryByText('Never')).not.toBeInTheDocument();
  });
});
