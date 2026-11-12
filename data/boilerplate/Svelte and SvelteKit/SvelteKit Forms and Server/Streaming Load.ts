---
label: Streaming Promises in Load
order: 30
description: A server load that awaits what the page needs and streams a slow part, shown later with {#await}
file: src/routes/{{Name:kebab}}/+page.server.ts
whole-file: yes
ask: Name = dashboard
---
import type { PageServerLoad } from './$types';

interface Summary {
    total: number;
}

interface Activity {
    id: number;
    text: string;
}

export const load: PageServerLoad = async ({ fetch }) => {
    // Start the slow request first, without waiting for it…
    const activity = fetch('/api/activity').then((response) => response.json() as Promise<Activity[]>);
    // …so a failure while the next await runs doesn't count as an unhandled rejection (the page still sees it).
    activity.catch(() => {});

    // …then wait for what the page can't render without.
    const summary: Summary = await fetch('/api/summary').then((response) => response.json());

    // A promise in the result streams: the page renders now and fills it in when it settles.
    // In +page.svelte: {#await data.activity}<p>Loading…</p>{:then activity}…{:catch}<p>Failed</p>{/await}
    return { summary, activity };
};
