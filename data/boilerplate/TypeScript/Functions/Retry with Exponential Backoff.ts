---
order: 120
section: async
description: Retries an async call with exponential backoff and jitter, up to a number of retries
---
export interface RetryOptions {
    retries?: number; // attempts after the first one
    baseDelayMs?: number;
    maxDelayMs?: number;
    shouldRetry?: (error: unknown, attempt: number) => boolean;
    signal?: AbortSignal; // checked before each attempt
}

// const data = await retry(() => fetchJson('/api/items'), { retries: 5 });
// Waits a random time up to 0.5 s, 1 s, 2 s, 4 s... between attempts; the randomness ("full
// jitter") keeps many clients from retrying in step after an outage.
export async function retry<T>(fn: (attempt: number) => Promise<T>, options: RetryOptions = {}): Promise<T> {
    const { retries = 3, baseDelayMs = 500, maxDelayMs = 30_000, shouldRetry = () => true, signal } = options;
    for (let attempt = 0; ; attempt++) {
        signal?.throwIfAborted();
        try {
            return await fn(attempt);
        } catch (error) {
            if (attempt >= retries || !shouldRetry(error, attempt)) {
                throw error;
            }
            const delay = Math.random() * Math.min(maxDelayMs, baseDelayMs * 2 ** attempt);
            await new Promise((resolve) => setTimeout(resolve, delay));
        }
    }
}
