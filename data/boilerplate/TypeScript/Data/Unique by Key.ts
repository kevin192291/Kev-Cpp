---
order: 30
description: uniqueBy(items, keyOf): drops later items whose key was already seen
---
// uniqueBy(users, (user) => user.email.toLowerCase()) keeps the first user per address.
// For plain strings or numbers, [...new Set(values)] is enough.
export function uniqueBy<T, K>(items: Iterable<T>, keyOf: (item: T) => K): T[] {
    const seen = new Set<K>();
    const result: T[] = [];
    for (const item of items) {
        const key = keyOf(item);
        if (!seen.has(key)) {
            seen.add(key);
            result.push(item);
        }
    }
    return result;
}
