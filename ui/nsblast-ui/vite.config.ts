import { defineConfig, loadEnv } from 'vite';
import react from '@vitejs/plugin-react';

function normalizeBasePath(value: string | undefined): string {
  const raw = (value ?? '').trim();
  if (!raw) {
    return '/ui/';
  }

  let normalized = raw.startsWith('/') ? raw : `/${raw}`;
  if (!normalized.endsWith('/')) {
    normalized = `${normalized}/`;
  }

  return normalized;
}

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const uiBasePath = normalizeBasePath(env.UI_BASE_PATH ?? env.VITE_UI_BASE_PATH);

  return {
    base: uiBasePath,
    plugins: [react()],
    test: {
      environment: 'jsdom',
      setupFiles: './src/setupTests.ts'
    }
  };
});
