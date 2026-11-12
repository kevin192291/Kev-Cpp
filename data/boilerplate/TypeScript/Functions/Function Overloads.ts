---
order: 20
section: functions
description: Overloads: each signature pairs an argument type with its own return type
---
// Callers see only the first two signatures: parseNumbers('1.5') is number,
// parseNumbers(['1', '2']) is number[]. The last signature is the implementation.
export function parseNumbers(input: string): number;
export function parseNumbers(input: readonly string[]): number[];
export function parseNumbers(input: string | readonly string[]): number | number[] {
    if (typeof input === 'string') {
        return Number.parseFloat(input);
    }
    return input.map((item) => Number.parseFloat(item));
}
