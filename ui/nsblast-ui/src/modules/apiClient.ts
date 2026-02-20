export class ApiClientError extends Error {
  readonly status: number;
  readonly url: string;
  readonly method: string;
  readonly details: unknown;

  constructor(message: string, status: number, url: string, method: string, details: unknown = null) {
    super(message);
    this.name = 'ApiClientError';
    this.status = status;
    this.url = url;
    this.method = method;
    this.details = details;
  }
}

type ApiClientConfig = {
  baseUrl: string;
  getToken: () => string;
};

type RequestOptions = {
  method?: string;
  headers?: HeadersInit;
  body?: unknown;
  auth?: boolean;
  authorization?: string;
  parse?: 'auto' | 'json' | 'text' | 'none';
  retry?: boolean;
  retries?: number;
};

const RETRYABLE_METHODS = new Set(['GET', 'HEAD', 'OPTIONS', 'PUT', 'DELETE']);

const trimTrailingSlash = (value: string) => value.replace(/\/+$/, '');
const trimLeadingSlash = (value: string) => value.replace(/^\/+/, '');

function joinUrl(base: string, target: string): string {
  const normalizedBase = trimTrailingSlash(base ?? '');
  const normalizedTarget = trimLeadingSlash(target ?? '');

  if (!normalizedBase) {
    return normalizedTarget ? `/${normalizedTarget}` : '/';
  }

  if (!normalizedTarget) {
    return normalizedBase;
  }

  return `${normalizedBase}/${normalizedTarget}`;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function shouldRetry(method: string, status: number): boolean {
  if (!RETRYABLE_METHODS.has(method)) {
    return false;
  }

  return status === 429 || status >= 500;
}

async function parseBody(response: Response, mode: RequestOptions['parse']): Promise<unknown> {
  if (mode === 'none') {
    return null;
  }

  if (mode === 'text') {
    return response.text();
  }

  if (mode === 'json') {
    return response.json();
  }

  const contentType = response.headers.get('content-type')?.toLowerCase() ?? '';
  if (contentType.includes('application/json')) {
    return response.json();
  }

  const bodyText = await response.text();
  if (!bodyText.trim().length) {
    return null;
  }

  return bodyText;
}

function normalizeBody(body: unknown): BodyInit | null {
  if (body === undefined || body === null) {
    return null;
  }

  if (
    typeof body === 'string' ||
    body instanceof URLSearchParams ||
    body instanceof FormData ||
    body instanceof Blob ||
    body instanceof ArrayBuffer
  ) {
    return body as BodyInit;
  }

  return JSON.stringify(body);
}

function makeErrorMessage(status: number, parsedBody: unknown, fallback: string): string {
  if (typeof parsedBody === 'string' && parsedBody.trim().length) {
    return parsedBody;
  }

  if (parsedBody && typeof parsedBody === 'object') {
    const obj = parsedBody as Record<string, unknown>;
    if (typeof obj.reason === 'string' && obj.reason.trim().length) {
      return obj.reason;
    }
    if (typeof obj.message === 'string' && obj.message.trim().length) {
      return obj.message;
    }
  }

  return fallback || `Request failed with status ${status}`;
}

export function createApiClient(config: ApiClientConfig) {
  return {
    async request<T = unknown>(target: string, options: RequestOptions = {}): Promise<T> {
      const method = (options.method ?? 'GET').toUpperCase();
      const parseMode = options.parse ?? 'auto';
      const retryEnabled = options.retry ?? true;
      const maxRetries = options.retries ?? 2;
      const url = joinUrl(config.baseUrl, target);

      const headers = new Headers(options.headers ?? {});

      if (options.auth !== false) {
        if (options.authorization) {
          headers.set('Authorization', options.authorization);
        } else {
          const token = config.getToken();
          if (token) {
            headers.set('Authorization', `Basic ${token}`);
          }
        }
      }

      const body = normalizeBody(options.body);
      if (body && typeof options.body === 'object' && !(options.body instanceof FormData)) {
        if (!headers.has('Content-Type')) {
          headers.set('Content-Type', 'application/json');
        }
      }

      for (let attempt = 0; attempt <= maxRetries; attempt += 1) {
        try {
          const response = await fetch(url, { method, headers, body });
          const parsedBody = await parseBody(response, parseMode);

          if (!response.ok) {
            if (retryEnabled && shouldRetry(method, response.status) && attempt < maxRetries) {
              await sleep(120 * (attempt + 1));
              continue;
            }

            throw new ApiClientError(
              makeErrorMessage(response.status, parsedBody, response.statusText),
              response.status,
              url,
              method,
              parsedBody
            );
          }

          return parsedBody as T;
        } catch (error) {
          if (error instanceof ApiClientError) {
            throw error;
          }

          if (retryEnabled && RETRYABLE_METHODS.has(method) && attempt < maxRetries) {
            await sleep(120 * (attempt + 1));
            continue;
          }

          throw new ApiClientError(
            error instanceof Error ? error.message : 'Network request failed',
            0,
            url,
            method,
            null
          );
        }
      }

      throw new ApiClientError('Request failed after retries', 0, url, method, null);
    }
  };
}
