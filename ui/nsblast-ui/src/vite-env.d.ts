/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_UI_BASE_PATH?: string;
  readonly VITE_API_BASE_URL?: string;
  readonly VITE_UI_ROUTER_MODE?: string;
  readonly VITE_BRAND_LOGO_URL?: string;
  readonly VITE_BRAND_LOGO_HEIGHT?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
