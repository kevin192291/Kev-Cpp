---
order: 20
description: A union of string literals, and a union of types narrowed with typeof/instanceof
ask: Name = Size
---
// Only these values type-check: let size: {{Name}} = 'medium';
export type {{Name}} = 'small' | 'medium' | 'large';

// A union of types: narrow it (typeof, instanceof, `in`) before using what only some members have.
export type DateInput = string | number | Date;

export function toDate(input: DateInput): Date {
    if (input instanceof Date) {
        return input;
    }
    return new Date(input); // input is string | number here
}
