---
label: vite.config.ts
description: A Vite config with an @ alias for src/, a fixed dev server port and source maps in builds
file: vite.config.ts
whole-file: yes
order: 32
section: web
---
// https://vite.dev/config/
import { fileURLToPath, URL } from 'node:url';
import { defineConfig } from 'vite';

export default defineConfig({
    resolve: {
        // import x from '@/lib/x' rather than '../../lib/x'; tsconfig.json needs the same in "paths".
        alias: {
            '@': fileURLToPath(new URL('./src', import.meta.url)),
        },
    },
    server: {
        port: 5173,
        strictPort: true, // fail rather than quietly move to another port
    },
    build: {
        sourcemap: true,
    },
});
