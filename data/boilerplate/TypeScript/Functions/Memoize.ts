---
order: 50
section: wrappers
description: Caches a function's results by its arguments (plain-value arguments; failed promises aren't kept)
---
// const fastFib = memoize(fib). The cache key is JSON.stringify(args), so pass plain values:
// numbers, strings, booleans, arrays and plain objects.
export function memoize<Args extends unknown[], R>(fn: (...args: Args) => R): (...args: Args) => R {
    const cache = new Map<string, R>();
    return (...args: Args): R => {
        const key = JSON.stringify(args);
        if (cache.has(key)) {
            return cache.get(key) as R;
        }
        const result = fn(...args);
        cache.set(key, result);
        if (result instanceof Promise) {
            // Forget a rejected promise so the next call tries again.
            result.catch(() => cache.delete(key));
        }
        return result;
    };
}
