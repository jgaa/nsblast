import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';
import './branding.css';
import App from './App';
import { applyBrandingVariables } from './config';

async function bootstrap() {
  const runtimeConfigUrl = `${import.meta.env.BASE_URL}ui-config.js`;
  await import(/* @vite-ignore */ runtimeConfigUrl).catch(() => undefined);
  applyBrandingVariables();

  ReactDOM.createRoot(document.getElementById('root') as HTMLElement).render(
    <React.StrictMode>
      <App />
    </React.StrictMode>
  );
}

void bootstrap();
