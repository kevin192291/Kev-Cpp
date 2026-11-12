---
order: 10
section: functions
description: Generic functions: an inferred type parameter, a constrained one, and a key of another
---
// T is inferred from the argument: first([1, 2]) is number | undefined.
export function first<T>(items: readonly T[]): T | undefined {
    return items[0];
}

// T must have a length; the result keeps the caller's exact type.
export function longest<T extends { length: number }>(a: T, b: T): T {
    return b.length > a.length ? b : a;
}

// K must be a key of T, and the result is that property's type: getProperty(user, 'name') is string.
export function getProperty<T, K extends keyof T>(object: T, key: K): T[K] {
    return object[key];
}
