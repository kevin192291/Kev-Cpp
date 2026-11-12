---
label: +page.ts Load
order: 20
description: A universal load function: runs on the server for the first visit and in the browser after that
file: src/routes/{{Name:kebab}}/+page.ts
whole-file: yes
ask: Name = items
---
import { error } from '@sveltejs/kit';
import type { PageLoad } from './$types';

interface Item {
    id: number;
    title: string;
}

// Use the fetch it's given: it passes cookies along on the server, can call your own +server.ts
// endpoints directly, and doesn't fetch the same thing again when the page hydrates.
export const load: PageLoad = async ({ fetch, url }) => {
    const response = await fetch(`/api/{{Name:kebab}}?${url.searchParams}`);
    if (!response.ok) {
        error(response.status, 'Could not load the list');
    }
    const items: Item[] = await response.json();
    return { items };
};
