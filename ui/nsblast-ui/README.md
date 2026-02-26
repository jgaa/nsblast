# nsBLAST UI

React + TypeScript UI built with Vite.

## Phase 2 Hosting Config

The UI supports build-time and runtime configuration for base path and API URL.

- `UI_BASE_PATH` (default: `/ui/`)
- `API_BASE_URL` (default: `/api/v1`)
- `UI_ROUTER_MODE` (default: `browser`, optional `hash`)
- `ALLOW_HTTP_LOGIN` (default: `false`, local testing only)

### Build-time

Vite reads `UI_BASE_PATH` (or `VITE_UI_BASE_PATH`) and uses it as build `base`.

Examples:

```bash
UI_BASE_PATH=/ui/ npm run build
UI_BASE_PATH=/admin/nsblast/ npm run build
```

### Runtime

Edit `public/ui-config.js` (or the deployed `ui-config.js`) to override without rebuilding:

```js
window.__NSBLAST_UI_CONFIG__ = {
  UI_BASE_PATH: '/ui/',
  API_BASE_URL: '/api/v1',
  UI_ROUTER_MODE: 'browser', // or 'hash'
  ALLOW_HTTP_LOGIN: false
};
```

`UI_ROUTER_MODE=hash` is useful when the hosting layer cannot provide SPA rewrites.

For local HTTP testing only, you can temporarily allow insecure login checks.
This override is only honored when the UI is loaded from a loopback host (`localhost`, `127.0.0.1`, `::1`):

```js
window.__NSBLAST_UI_CONFIG__ = {
  ALLOW_HTTP_LOGIN: true
};
```

You can also set build-time env `VITE_ALLOW_HTTP_LOGIN=true`.
Do not enable this in production.

## Standalone Hosting

If using `browser` router mode, configure SPA fallback to `index.html` for unknown extensionless routes.
Production deployments should serve both UI and API over HTTPS only.

### Nginx

```nginx
server {
    listen 443 ssl;
    server_name nsblast.example.com;

    # UI
    location /ui/ {
        root /srv/nsblast;
        try_files $uri $uri/ /ui/index.html;
        add_header Content-Security-Policy "default-src 'self'; connect-src 'self'; img-src 'self' data: https:; style-src 'self'; script-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'" always;
        add_header X-Content-Type-Options "nosniff" always;
        add_header X-Frame-Options "DENY" always;
        add_header Referrer-Policy "strict-origin-when-cross-origin" always;
    }

    # API proxy
    location /api/v1/ {
        proxy_pass http://127.0.0.1:8080/api/v1/;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-Proto https;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

        # Strict CORS allow-list for standalone UI origin.
        if ($http_origin = "https://nsblast.example.com") {
            add_header Access-Control-Allow-Origin $http_origin always;
            add_header Access-Control-Allow-Credentials "true" always;
            add_header Access-Control-Allow-Methods "GET,POST,PUT,PATCH,DELETE,OPTIONS" always;
            add_header Access-Control-Allow-Headers "Authorization,Content-Type" always;
            add_header Vary "Origin" always;
        }
        if ($request_method = OPTIONS) {
            return 204;
        }
    }
}
```

### Caddy

```caddy
nsblast.example.com {
    root * /srv/nsblast

    handle_path /api/v1/* {
        @cors_preflight method OPTIONS
        header @cors_preflight Access-Control-Allow-Origin "https://nsblast.example.com"
        header @cors_preflight Access-Control-Allow-Credentials "true"
        header @cors_preflight Access-Control-Allow-Methods "GET,POST,PUT,PATCH,DELETE,OPTIONS"
        header @cors_preflight Access-Control-Allow-Headers "Authorization,Content-Type"
        respond @cors_preflight 204

        header Access-Control-Allow-Origin "https://nsblast.example.com"
        header Access-Control-Allow-Credentials "true"
        header Access-Control-Allow-Methods "GET,POST,PUT,PATCH,DELETE,OPTIONS"
        header Access-Control-Allow-Headers "Authorization,Content-Type"
        header Vary "Origin"
        reverse_proxy 127.0.0.1:8080
    }

    handle /ui/* {
        header Content-Security-Policy "default-src 'self'; connect-src 'self'; img-src 'self' data: https:; style-src 'self'; script-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'"
        header X-Content-Type-Options "nosniff"
        header X-Frame-Options "DENY"
        header Referrer-Policy "strict-origin-when-cross-origin"
        try_files {path} {path}/ /ui/index.html
        file_server
    }
}
```

## Security Baseline

- Use HTTPS in production for both UI and API endpoints.
- Keep `API_BASE_URL` as relative (`/api/v1`) unless there is a strict cross-origin requirement.
- If cross-origin is required, allow-list exact UI origins only, not wildcards.
- Keep `Authorization` headers in memory only. The UI does not persist credentials to `localStorage`.

## Branding Without React Knowledge

The UI theme is controlled by CSS variables in `src/index.css`, with operator overrides in `src/branding.css`.

### Stable branding variables

- `--color-primary`
- `--color-primary-strong`
- `--color-secondary`
- `--color-surface`
- `--color-surface-muted`
- `--color-background`
- `--color-text`
- `--color-text-muted`
- `--color-border`
- `--status-success`
- `--status-warning`
- `--status-error`
- `--status-info`
- `--radius-sm`, `--radius-md`, `--radius-lg`
- `--space-1` .. `--space-6`
- `--font-family-base`
- `--brand-logo-url`
- `--brand-logo-height`

### Copy/paste CSS override example

```css
:root {
  --color-primary: #612010;
  --color-secondary: #1f5770;
  --color-background: #fbf6f1;
  --color-text: #1e2430;
  --status-success: #1b7b48;
  --status-warning: #b68200;
  --status-error: #b23833;
  --brand-logo-url: url("https://example.com/logo.svg");
  --brand-logo-height: 56px;
}
```

Put this in `src/branding.css` for build-time theming.

### Runtime branding override (no rebuild)

`public/ui-config.js` supports logo overrides at runtime:

```js
window.__NSBLAST_UI_CONFIG__ = {
  BRAND_LOGO_URL: "https://example.com/logo.svg",
  BRAND_LOGO_HEIGHT: "56px"
};
```

If no custom logo is configured, the bundled fallback logo (`/brand-logo.svg`) is used.

## Scripts

- `npm run dev`: start development server
- `npm run build`: create production build in `dist/`
- `npm run preview`: preview production build locally
- `npm run test`: run unit tests with Vitest
- `npm run lint`: run ESLint
- `npm run typecheck`: run TypeScript checks

## CI checks

CI runs `npm ci`, `npm run typecheck`, `npm run lint`, `npm run test`, and `npm run build`.
