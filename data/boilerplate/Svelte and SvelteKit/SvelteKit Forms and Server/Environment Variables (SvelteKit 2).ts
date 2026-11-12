---
order: 80
section: environment
description: Server settings from .env through $env/static and $env/dynamic, in a server-only module (SvelteKit 2)
file: src/lib/server/config.ts
whole-file: yes
---
// Server-only settings: SvelteKit refuses to bundle anything under src/lib/server/ into browser code.
// .env (keep it out of git):
//     DATABASE_URL=postgres://user:password@localhost:5432/app
//     PUBLIC_SITE_NAME=My Site
import { env } from '$env/dynamic/private';
import { DATABASE_URL } from '$env/static/private';
import { PUBLIC_SITE_NAME } from '$env/static/public';

export const config = {
    // static: inlined when building, so the variable must be set by then.
    databaseUrl: DATABASE_URL,
    // dynamic: read when the server runs, so one build can run with different settings.
    smtpHost: env.SMTP_HOST ?? 'localhost',
    smtpPort: Number(env.SMTP_PORT ?? 587),
    // PUBLIC_ variables can also be imported in browser code.
    siteName: PUBLIC_SITE_NAME
};
