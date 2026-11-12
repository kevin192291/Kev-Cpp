---
label: Promise.all and allSettled
order: 130
section: async
description: Promise.all for a typed tuple that fails fast; allSettled to collect every success and failure
---
// Promise.all starts nothing itself; it waits for promises already running side by side.
// It resolves to a typed tuple and rejects as soon as one of them rejects:
//     const [user, orders] = await Promise.all([getUser(id), getOrders(id)]);
// Promise.any resolves with the first success; Promise.race settles with whichever settles first.

// Waits for every promise (never rejects) and splits the outcomes:
// const { values, errors } = await settle(urls.map((url) => fetch(url)));
export async function settle<T>(promises: Iterable<T | PromiseLike<T>>): Promise<{ values: T[]; errors: unknown[] }> {
    const values: T[] = [];
    const errors: unknown[] = [];
    for (const result of await Promise.allSettled(promises)) {
        if (result.status === 'fulfilled') {
            values.push(result.value);
        } else {
            errors.push(result.reason);
        }
    }
    return { values, errors };
}
