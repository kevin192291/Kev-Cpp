---
order: 20
description: postJson(url, body): sends JSON, parses the JSON reply, and throws with the server's message on failure
---
// const created = await postJson<Todo>('/api/todos', { title: 'Write tests' });
// Pass { method: 'PUT' } or 'PATCH' in init for updates.
export async function postJson<TResponse>(
    url: string | URL,
    body: unknown,
    init: RequestInit = {},
): Promise<TResponse> {
    const headers = new Headers(init.headers);
    headers.set('content-type', 'application/json');
    headers.set('accept', 'application/json');
    const response = await fetch(url, {
        ...init,
        method: init.method ?? 'POST',
        headers,
        body: JSON.stringify(body),
    });
    const text = await response.text();
    if (!response.ok) {
        throw new Error(`${response.status} ${response.statusText}: ${text.slice(0, 500)}`);
    }
    return (text === '' ? undefined : JSON.parse(text)) as TResponse;
}
