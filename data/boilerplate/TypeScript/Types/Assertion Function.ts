---
order: 100
description: Assertion functions (asserts x / asserts x is T): throw when wrong, narrow the type when right
---
// After assert(user.age >= 18), TypeScript treats the condition as true.
export function assert(condition: unknown, message = 'Assertion failed'): asserts condition {
    if (!condition) {
        throw new Error(message);
    }
}

// After assertDefined(canvas, 'canvas'), canvas is no longer null or undefined.
export function assertDefined<T>(value: T, name = 'value'): asserts value is NonNullable<T> {
    if (value === null || value === undefined) {
        throw new Error(`Expected ${name} to be defined`);
    }
}
