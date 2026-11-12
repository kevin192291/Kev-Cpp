---
order: 20
description: chunk(items, size): splits an array into arrays of at most `size` items
---
// chunk([1, 2, 3, 4, 5], 2) → [[1, 2], [3, 4], [5]]. Handy for batching requests.
export function chunk<T>(items: readonly T[], size: number): T[][] {
    if (!Number.isInteger(size) || size < 1) {
        throw new RangeError(`chunk size must be a positive integer, got ${size}`);
    }
    const chunks: T[][] = [];
    for (let i = 0; i < items.length; i += size) {
        chunks.push(items.slice(i, i + size));
    }
    return chunks;
}
