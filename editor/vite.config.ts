import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  root: __dirname,
  plugins: [react()],
  server: {
    port: 5180,
    proxy: {
      '/api': 'http://localhost:5181',
      '/assets': 'http://localhost:5181',
    },
  },
});
