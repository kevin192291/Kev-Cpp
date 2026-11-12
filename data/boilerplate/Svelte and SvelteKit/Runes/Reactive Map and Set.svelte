---
order: 60
description: SvelteMap and SvelteSet from svelte/reactivity: a Map and a Set whose changes update the page
---
import { SvelteMap, SvelteSet } from 'svelte/reactivity';

// A plain Map or Set in $state doesn't notice set/add/delete; these do.
const selected = new SvelteSet<string>();
const counts = new SvelteMap<string, number>();

function toggleSelected(id: string) {
    if (selected.has(id)) {
        selected.delete(id);
    } else {
        selected.add(id);
    }
}

function countOne(key: string) {
    counts.set(key, (counts.get(key) ?? 0) + 1);
}
