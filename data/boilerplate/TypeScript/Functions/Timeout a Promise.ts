---
order: 110
section: async
description: withTimeout(promise, ms): rejects with a TimeoutError if the promise takes too long
---
export class TimeoutError extends Error {
    override name = 'TimeoutError';
}

// const data = await withTimeout(loadData(), 5000). The timer is cleared either way, so it can't
// keep Node running. The work itself goes on: to stop it too, give the API that does it a signal,
// e.g. fetch(url, { signal: AbortSignal.timeout(5000) }).
export function withTimeout<T>(promise: Promise<T>, ms: number, message = `Timed out after ${ms} ms`): Promise<T> {
    let timer: ReturnType<typeof setTimeout> | undefined;
    const timeout = new Promise<never>((_, reject) => {
        timer = setTimeout(() => reject(new TimeoutError(message)), ms);
    });
    return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}
