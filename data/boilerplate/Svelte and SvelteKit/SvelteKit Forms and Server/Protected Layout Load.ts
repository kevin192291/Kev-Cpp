---
label: Sign-in Guard (+layout.server.ts)
order: 70
section: server
description: Sends visitors without a session to /login for every page in a route group (uses event.locals.user)
file: src/routes/(app)/+layout.server.ts
whole-file: yes
---
import { redirect } from '@sveltejs/kit';
import type { LayoutServerLoad } from './$types';

// Pages under src/routes/(app)/ need a signed-in user (event.locals.user comes from src/hooks.server.ts).
// A layout load doesn't guard form actions or +server.ts endpoints: check locals.user there as well.
export const load: LayoutServerLoad = ({ locals, url }) => {
    if (!locals.user) {
        redirect(303, `/login?redirectTo=${encodeURIComponent(url.pathname + url.search)}`);
    }
    return { user: locals.user };
};
