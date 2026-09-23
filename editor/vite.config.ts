import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  root: __dirname,
  plugins: [react()],
  server: {
    port: 5180,
    proxy: {
      // 127.0.0.1, not localhost: the API server binds IPv4 loopback only, and
      // Node on Windows can resolve localhost to ::1 first.
      '/api': 'http://127.0.0.1:5181',
      '/assets': 'http://127.0.0.1:5181',
    },
  },
});
