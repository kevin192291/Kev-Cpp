---
order: 90
description: A type guard (value is T) that checks unknown data, e.g. parsed JSON, before it is used
ask: Name = User
---
export interface {{Name}} {
    id: number;
    name: string;
}

// if (is{{Name}}(data)) { /* data is a {{Name}} here */ }
// const valid = items.filter(is{{Name}}); // {{Name}}[]
export function is{{Name}}(value: unknown): value is {{Name}} {
    return (
        typeof value === 'object' &&
        value !== null &&
        'id' in value &&
        typeof value.id === 'number' &&
        'name' in value &&
        typeof value.name === 'string'
    );
}
