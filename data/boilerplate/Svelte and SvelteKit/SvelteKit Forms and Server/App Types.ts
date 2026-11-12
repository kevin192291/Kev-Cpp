---
label: app.d.ts (Locals and Error)
order: 40
section: server
description: Types for event.locals (the signed-in user) and page.error (an error id), used by the hooks snippets
file: src/app.d.ts
whole-file: yes
---
// Types SvelteKit uses across the app. See https://svelte.dev/docs/kit/types#app.d.ts
declare global {
    namespace App {
        interface Locals {
            // Set in src/hooks.server.ts; null when nobody is signed in.
            user: { id: string; name: string; email: string } | null;
        }

        interface Error {
            // From handleError, for people to quote when they report a problem.
            errorId?: string;
        }

        // interface PageData {}
        // interface PageState {}
        // interface Platform {}
    }
}

export {};
