type RouterMode = 'browser' | 'hash';

type RuntimeConfig = {
  UI_BASE_PATH?: string;
  API_BASE_URL?: string;
  UI_ROUTER_MODE?: string;
  BRAND_LOGO_URL?: string;
  BRAND_LOGO_HEIGHT?: string;
};

const DEFAULT_UI_BASE_PATH = '/ui/';
const DEFAULT_API_BASE_URL = '/api/v1';
const DEFAULT_ROUTER_MODE: RouterMode = 'browser';
const DEFAULT_BRAND_LOGO_URL = `${import.meta.env.BASE_URL}brand-logo.svg`;
const DEFAULT_BRAND_LOGO_HEIGHT = '48px';

function normalizeBasePath(value: string | undefined): string {
  const raw = (value ?? '').trim();
  if (!raw) {
    return DEFAULT_UI_BASE_PATH;
  }

  let normalized = raw.startsWith('/') ? raw : `/${raw}`;
  if (!normalized.endsWith('/')) {
    normalized = `${normalized}/`;
  }

  return normalized;
}

function normalizeRouterMode(value: string | undefined): RouterMode {
  return value?.toLowerCase() === 'hash' ? 'hash' : DEFAULT_ROUTER_MODE;
}

function readRuntimeConfig(): RuntimeConfig {
  const globalWithConfig = globalThis as typeof globalThis & {
    __NSBLAST_UI_CONFIG__?: RuntimeConfig;
  };
  return globalWithConfig.__NSBLAST_UI_CONFIG__ ?? {};
}

const runtimeConfig = readRuntimeConfig();

export const UI_BASE_PATH = normalizeBasePath(
  runtimeConfig.UI_BASE_PATH ?? import.meta.env.VITE_UI_BASE_PATH
);

export const API_BASE_URL = (
  runtimeConfig.API_BASE_URL ?? import.meta.env.VITE_API_BASE_URL ?? DEFAULT_API_BASE_URL
).trim() || DEFAULT_API_BASE_URL;

export const UI_ROUTER_MODE = normalizeRouterMode(
  runtimeConfig.UI_ROUTER_MODE ?? import.meta.env.VITE_UI_ROUTER_MODE
);

export const UI_ROUTER_BASENAME =
  UI_BASE_PATH === '/' ? '/' : UI_BASE_PATH.replace(/\/$/, '');

export const BRAND_LOGO_URL = (
  runtimeConfig.BRAND_LOGO_URL ?? import.meta.env.VITE_BRAND_LOGO_URL ?? DEFAULT_BRAND_LOGO_URL
).trim() || DEFAULT_BRAND_LOGO_URL;

export const BRAND_LOGO_HEIGHT = (
  runtimeConfig.BRAND_LOGO_HEIGHT ?? import.meta.env.VITE_BRAND_LOGO_HEIGHT ?? DEFAULT_BRAND_LOGO_HEIGHT
).trim() || DEFAULT_BRAND_LOGO_HEIGHT;

export function applyBrandingVariables(): void {
  if (typeof document === 'undefined') {
    return;
  }

  const root = document.documentElement;
  root.style.setProperty('--brand-logo-url', `url("${BRAND_LOGO_URL}")`);
  root.style.setProperty('--brand-logo-height', BRAND_LOGO_HEIGHT);
}
