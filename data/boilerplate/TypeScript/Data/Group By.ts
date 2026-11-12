---
order: 10
description: groupBy(items, keyOf): an object of arrays keyed by whatever keyOf returns
---
// groupBy(users, (user) => user.role) → { admin: [...], member: [...] }.
// ES2024 has this built in: Object.groupBy (and Map.groupBy for non-string keys), in Node 21+
// and current browsers, typed once "lib" includes "ES2024".
export function groupBy<T, K extends PropertyKey>(items: Iterable<T>, keyOf: (item: T) => K): Partial<Record<K, T[]>> {
    const groups: Partial<Record<K, T[]>> = {};
    for (const item of items) {
        const key = keyOf(item);
        const group = groups[key];
        if (group) {
            group.push(item);
        } else {
            groups[key] = [item];
        }
    }
    return groups;
}
