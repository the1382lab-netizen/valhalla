import { defineConfig } from 'vite';
import path from 'path';

export default defineConfig({
  resolve: {
    alias: {
      '@valhalla/shared': path.resolve(__dirname, '../shared/src'),
    },
  },
  build: {
    emptyOutDir: true,
  },
  server: {
    port: 3000,
    open: true,
    host: '0.0.0.0', // Listen on all interfaces for LAN access
  },
});
