---
label: +page.server.ts Load
order: 30
description: A server load function: runs only on the server, so databases, secrets and cookies are safe to use
file: src/routes/{{Name:kebab}}/+page.server.ts
whole-file: yes
ask: Name = items
---
import { error } from '@sveltejs/kit';
import type { PageServerLoad } from './$types';

interface Item {
    id: number;
    title: string;
}

// What it returns goes to the browser, so it must be serializable (JSON plus Date, Map, Set, BigInt…).
export const load: PageServerLoad = async ({ fetch, url, setHeaders }) => {
    const page = Math.max(1, Number(url.searchParams.get('page')) || 1);

    const response = await fetch(`/api/{{Name:kebab}}?page=${page}`);
    if (!response.ok) {
        error(response.status, 'Could not load the list');
    }

    // Browsers may reuse this page's data for a minute.
    setHeaders({ 'cache-control': 'max-age=60' });
    return { items: (await response.json()) as Item[], page };
};
