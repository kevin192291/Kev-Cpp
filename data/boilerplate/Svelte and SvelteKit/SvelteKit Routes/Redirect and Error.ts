---
order: 90
section: helpers
description: redirect() and error() inside a load, action or endpoint; import { error, redirect } from '@sveltejs/kit'
---
// Both throw, so nothing after them runs.
if (!locals.user) {
    // 303 sends a GET to the new URL (right after a form POST too); 307 and 308 keep the method.
    redirect(303, `/login?redirectTo=${encodeURIComponent(url.pathname)}`);
}
if (!item) {
    // Shows the nearest +error.svelte, with page.status 404 and page.error.message.
    error(404, 'Not found');
}
