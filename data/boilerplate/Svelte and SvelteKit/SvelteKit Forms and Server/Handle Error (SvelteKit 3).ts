---
label: handleError (SvelteKit 3)
order: 61
section: server
description: For src/hooks.server.ts: logs unexpected errors with an id and shows people a safe message (SvelteKit 3)
---
import type { HandleServerError } from '@sveltejs/kit/hooks';

// Every error comes here. Those from error(…) (kind 'app') and SvelteKit's own 404s and 405s ('framework') are
// safe to show as they are; anything else ('unknown') is a bug: log it and keep its details from the page.
export const handleError: HandleServerError = ({ kind, error, event }) => {
    if (kind !== 'unknown') {
        return error;
    }
    const errorId = crypto.randomUUID();
    console.error(`[${errorId}] ${event.request.method} ${event.url.pathname}`, error);
    return { message: 'Something went wrong.', errorId };
};
