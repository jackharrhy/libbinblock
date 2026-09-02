import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  publicDir: '.build/browser-public',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: 'es2022',
  },
  server: {
    host: '0.0.0.0',
    allowedHosts: ['newport.hedgehog-python.ts.net'],
    port: 8888,
    strictPort: true,
  },
  preview: {
    host: '0.0.0.0',
    allowedHosts: ['newport.hedgehog-python.ts.net'],
    port: 8888,
    strictPort: true,
  },
});
