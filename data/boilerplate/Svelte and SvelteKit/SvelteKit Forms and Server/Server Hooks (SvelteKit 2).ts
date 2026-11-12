---
label: hooks.server.ts (SvelteKit 2)
order: 50
section: server
description: A handle hook that reads the session cookie into event.locals.user on every request (SvelteKit 2)
file: src/hooks.server.ts
whole-file: yes
---
import type { Handle } from '@sveltejs/kit';

// Runs for every request before the page or endpoint. What it puts in event.locals reaches load functions,
// form actions and +server.ts handlers (declare the fields in App.Locals, in src/app.d.ts).
export const handle: Handle = async ({ event, resolve }) => {
    const sessionId = event.cookies.get('session');
    event.locals.user = sessionId ? await findUserBySession(sessionId) : null;

    return resolve(event);
};

// Replace with a lookup wherever you keep sessions (a database table, a key-value store…); until then nobody
// is signed in.
async function findUserBySession(sessionId: string): Promise<App.Locals['user']> {
    return null;
}
