---
order: 81
section: environment
description: Declares the environment variables that $app/env/private and $app/env/public provide (SvelteKit 3)
file: src/env.ts
whole-file: yes
---
// Values come from .env, .env.local or the environment; the app won't start (or build) if one is invalid.
// Server code: import { DATABASE_URL } from '$app/env/private';
// Any code:    import { SITE_NAME } from '$app/env/public';
import { defineEnvVars } from '@sveltejs/kit/env';

export const variables = defineEnvVars({
    DATABASE_URL: {
        description: 'Connection string for the database'
    },
    SMTP_PORT: {
        description: 'Mail server port (587 when not set)',
        schema: (value) => {
            const port = Number(value ?? 587);
            if (!Number.isInteger(port) || port <= 0) {
                throw new Error('SMTP_PORT must be a port number');
            }
            return port;
        }
    },
    SITE_NAME: {
        public: true,
        description: 'Shown in page titles'
    }
});
