---
order: 30
description: fetch that gives up after a time limit (AbortSignal.timeout), still cancellable by the caller
---
// const response = await fetchWithTimeout('/api/report', 5000);
// When time runs out it rejects with a DOMException named 'TimeoutError'; when the caller's
// own signal aborts, with 'AbortError'.
export function fetchWithTimeout(url: string | URL, ms: number, init: RequestInit = {}): Promise<Response> {
    const timeout = AbortSignal.timeout(ms);
    const signal = init.signal ? AbortSignal.any([init.signal, timeout]) : timeout;
    return fetch(url, { ...init, signal });
}

export function isTimeout(error: unknown): boolean {
    return error instanceof DOMException && error.name === 'TimeoutError';
}
