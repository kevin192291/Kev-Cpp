---
order: 40
description: A const object plus a union type of its values: enum-like names that are plain JavaScript
ask: Name = Direction
---
// Unlike `enum`, this is ordinary JavaScript, so it also works with Node's type stripping
// and "erasableSyntaxOnly". Use it as {{Name}}.Up; the type {{Name}} is 'up' | 'down' | ...
export const {{Name}} = {
    Up: 'up',
    Down: 'down',
    Left: 'left',
    Right: 'right',
} as const;

export type {{Name}} = (typeof {{Name}})[keyof typeof {{Name}}];

// For values from outside (JSON, URLs, forms), checked at run time.
export function is{{Name}}(value: unknown): value is {{Name}} {
    return (Object.values({{Name}}) as unknown[]).includes(value);
}
