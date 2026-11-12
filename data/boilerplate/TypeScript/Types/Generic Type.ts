---
order: 50
description: Generic types: a type parameter with a default, a constrained one, and a generic union
---
// Page<User> is a page of users; Page alone means Page<unknown>.
export interface Page<T = unknown> {
    items: T[];
    total: number;
    nextCursor?: string;
}

// K must be one of T's keys: Lookup<User, 'id'> maps each id to its user.
export type Lookup<T, K extends keyof T> = Map<T[K], T>;

// Either the data or an error, with the error type defaulting to string.
export type ApiResult<T, E = string> = { ok: true; data: T } | { ok: false; error: E };
