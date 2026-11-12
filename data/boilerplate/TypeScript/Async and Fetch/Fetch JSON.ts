---
order: 10
description: fetchJson<T>(url): fetch, throw an HttpError on non-2xx statuses, parse JSON; cancellable with a signal
---
export class HttpError extends Error {
    override name = 'HttpError';
    readonly status: number;
    readonly body: string;

    constructor(response: Response, body: string) {
        super(`${response.status} ${response.statusText} from ${response.url}`);
        this.status = response.status;
        this.body = body;
    }
}

// const user = await fetchJson<User>(`/api/users/${id}`);
// <T> is a promise to the compiler, not a check: validate data from servers you don't control.
// To cancel: const controller = new AbortController(); fetchJson(url, { signal: controller.signal });
// then controller.abort() makes it reject with an AbortError.
export async function fetchJson<T>(url: string | URL, init: RequestInit = {}): Promise<T> {
    const headers = new Headers(init.headers);
    if (!headers.has('accept')) {
        headers.set('accept', 'application/json');
    }
    const response = await fetch(url, { ...init, headers });
    if (!response.ok) {
        throw new HttpError(response, await response.text());
    }
    // 204 No Content has no body to parse.
    return (response.status === 204 ? undefined : await response.json()) as T;
}
