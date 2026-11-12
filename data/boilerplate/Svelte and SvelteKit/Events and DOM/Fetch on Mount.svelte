---
order: 40
description: Fetches JSON once the component is in the page, with loading and error state (in SvelteKit prefer a load)
---
import { onMount } from 'svelte';

interface Item {
    id: number;
    name: string;
}

let items = $state.raw<Item[]>([]);
let loading = $state(true);
let error = $state<string | null>(null);

// onMount runs only in the browser; aborting stops a late answer from landing after the component is gone.
onMount(() => {
    const controller = new AbortController();
    fetch('/api/items', { signal: controller.signal })
        .then((response) => {
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            return response.json() as Promise<Item[]>;
        })
        .then((data) => {
            items = data;
        })
        .catch((reason: unknown) => {
            if (!controller.signal.aborted) {
                error = reason instanceof Error ? reason.message : String(reason);
            }
        })
        .finally(() => {
            loading = false;
        });
    return () => controller.abort();
});
