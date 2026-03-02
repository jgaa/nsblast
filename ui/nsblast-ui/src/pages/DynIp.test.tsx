import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';
import { AppStateContext } from '../modules/AppState';
import { ApiClientError } from '../modules/apiClient';
import DynIp from './DynIp';

function renderDynIp(request: (target: string, options?: Record<string, unknown>) => Promise<unknown>) {
  return render(
    <AppStateContext.Provider value={{ api: { request } }}>
      <DynIp />
    </AppStateContext.Provider>
  );
}

describe('DynIp', () => {
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
});
