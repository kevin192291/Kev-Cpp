---
label: handleError (SvelteKit 2)
order: 60
section: server
description: For src/hooks.server.ts: logs unexpected errors with an id and shows people a safe message (SvelteKit 2)
---
import type { HandleServerError } from '@sveltejs/kit';

// Called for unexpected errors (not for error(…) calls), including 404s for unknown URLs.
export const handleError: HandleServerError = ({ error, event, status, message }) => {
    if (status < 500) {
        return { message };
    }
    const errorId = crypto.randomUUID();
    console.error(`[${errorId}] ${event.request.method} ${event.url.pathname}`, error);
    return { message: 'Something went wrong.', errorId };
};
