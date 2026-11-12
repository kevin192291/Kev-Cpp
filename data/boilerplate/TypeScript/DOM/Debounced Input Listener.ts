---
order: 40
description: Search as you type: waits for a pause in typing and aborts the previous request
---
// onSearchInput(searchBox, async (query, signal) => {
//     const response = await fetch(`/api/search?q=${encodeURIComponent(query)}`, { signal });
//     showResults(await response.json());
// });
// Aborting the older request means a slow, stale answer can't overwrite a newer one.
export function onSearchInput(
    input: HTMLInputElement,
    search: (query: string, signal: AbortSignal) => Promise<void>,
    waitMs = 300,
): () => void {
    let timer: ReturnType<typeof setTimeout> | undefined;
    let controller: AbortController | undefined;

    const onInput = (): void => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            controller?.abort();
            controller = new AbortController();
            search(input.value.trim(), controller.signal).catch((error: unknown) => {
                if (!(error instanceof DOMException && error.name === 'AbortError')) {
                    console.error(error);
                }
            });
        }, waitMs);
    };

    input.addEventListener('input', onInput);
    return () => {
        input.removeEventListener('input', onInput);
        clearTimeout(timer);
        controller?.abort();
    };
}
