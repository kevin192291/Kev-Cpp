---
order: 80
description: Template literal types: string patterns, combinations, and route parameters inferred from a path
---
type Size = 'sm' | 'md' | 'lg';
type Variant = 'primary' | 'danger';

// Every combination: 'btn-sm-primary' | 'btn-sm-danger' | 'btn-md-primary' | ...
export type ButtonClass = `btn-${Size}-${Variant}`;

// Only strings that start with '/'.
export type Path = `/${string}`;

// The parameter names in a route: RouteParams<'/users/:id/posts/:postId'> is 'id' | 'postId'.
export type RouteParams<P extends string> = P extends `${string}:${infer Param}/${infer Rest}`
    ? Param | RouteParams<`/${Rest}`>
    : P extends `${string}:${infer Param}`
        ? Param
        : never;

// buildPath('/users/:id', { id: '42' }) is checked: a missing or misspelled parameter won't compile.
export function buildPath<P extends Path>(path: P, params: Record<RouteParams<P>, string | number>): string {
    const values: Record<string, string | number> = params;
    return path.replace(/:(\w+)/g, (_, name: string) => encodeURIComponent(String(values[name])));
}
