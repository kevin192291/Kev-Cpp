---
order: 40
description: sortBy(items, keyOf, 'asc' | 'desc'): a sorted copy, by a string, number or date key
---
// sortBy(users, (user) => user.name) or sortBy(posts, (post) => post.date, 'desc').
// Strings are compared with localeCompare, so accents and case sort the way people expect.
// To sort by several keys, chain them in one comparator: a.last.localeCompare(b.last) || a.age - b.age.
export function sortBy<T>(
    items: readonly T[],
    keyOf: (item: T) => string | number | Date,
    order: 'asc' | 'desc' = 'asc',
): T[] {
    const direction = order === 'asc' ? 1 : -1;
    return [...items].sort((a, b) => {
        const x = keyOf(a);
        const y = keyOf(b);
        const result = typeof x === 'string' && typeof y === 'string' ? x.localeCompare(y) : Number(x) - Number(y);
        return result * direction;
    });
}
