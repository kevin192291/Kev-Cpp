---
order: 10
description: A Vitest test file for the open file's module: equality, errors, async code and table-driven cases
file: {{fileStem}}.test.ts
whole-file: yes
ask: Function = add
---
// Tests for ./{{fileStem}}. Run with `npx vitest` (re-runs on save) or `npx vitest run` (once).
import { afterEach, describe, expect, it, vi } from 'vitest';
import { {{Function}} } from './{{fileStem}}';

afterEach(() => {
    vi.restoreAllMocks(); // undo vi.spyOn() replacements after every test
});

describe('{{Function}}', () => {
    it('returns the expected result', () => {
        expect({{Function}}(2, 3)).toBe(5);{{cursor}}
    });

    it.each([
        [0, 0, 0],
        [-1, 1, 0],
        [1.5, 2.5, 4],
    ])('{{Function}}(%d, %d) is %d', (a, b, expected) => {
        expect({{Function}}(a, b)).toBe(expected);
    });
});

// The common matchers, for reference; delete what you don't need.
describe('matchers', () => {
    it('compares values', () => {
        expect({ id: 1, tags: ['a'] }).toEqual({ id: 1, tags: ['a'] }); // deep equality
        expect({ id: 1, name: 'Ada' }).toMatchObject({ name: 'Ada' }); // these properties match
        expect(0.1 + 0.2).toBeCloseTo(0.3); // floating point
        expect(['a', 'b']).toContain('b');
        expect('Hello, world').toMatch(/world/);
        expect(undefined).toBeUndefined();
    });

    it('checks thrown errors', () => {
        expect(() => JSON.parse('{')).toThrow(SyntaxError);
    });

    it('awaits promises', async () => {
        await expect(Promise.resolve(42)).resolves.toBe(42);
        await expect(Promise.reject(new Error('boom'))).rejects.toThrow('boom');
    });
});
