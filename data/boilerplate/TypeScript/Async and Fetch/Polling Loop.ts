---
order: 50
description: poll(check): calls an async check at an interval until it returns a value, times out or is aborted
---
export interface PollOptions {
    intervalMs?: number;
    timeoutMs?: number;
    signal?: AbortSignal;
}

// Waits for a background job to finish:
// const job = await poll(async () => {
//     const job = await fetchJson<Job>(`/api/jobs/${id}`);
//     return job.state === 'done' ? job : undefined;
// }, { intervalMs: 2000 });
export async function poll<T>(
    check: () => Promise<T | undefined>,
    { intervalMs = 1000, timeoutMs = 60_000, signal }: PollOptions = {},
): Promise<T> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
        signal?.throwIfAborted();
        const result = await check();
        if (result !== undefined) {
            return result;
        }
        if (Date.now() + intervalMs > deadline) {
            throw new Error(`Gave up polling after ${timeoutMs} ms`);
        }
        await new Promise((resolve) => setTimeout(resolve, intervalMs));
    }
}
