---
order: 40
description: createLimiter(n): an async queue that runs at most n tasks at a time
---
// const limit = createLimiter(4);
// const pages = await Promise.all(urls.map((url) => limit(() => fetch(url).then((r) => r.text()))));
// Tasks start in the order they were queued; each call resolves or rejects with its own task.
export function createLimiter(concurrency: number) {
    let active = 0;
    const waiting: (() => void)[] = [];

    return async function limit<T>(task: () => Promise<T>): Promise<T> {
        if (active < concurrency) {
            active++;
        } else {
            // Wait for a finishing task to hand over its slot.
            await new Promise<void>((resolve) => waiting.push(resolve));
        }
        try {
            return await task();
        } finally {
            const next = waiting.shift();
            if (next) {
                next();
            } else {
                active--;
            }
        }
    };
}
