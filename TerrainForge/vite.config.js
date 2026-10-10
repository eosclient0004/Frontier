import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('.', import.meta.url));
export default defineConfig({
  root,
  base: './',
  server: { host: '0.0.0.0', port: 5175, allowedHosts: ['.e2b.app'] },
});
