---
order: 20
description: One listener on a container for events on matching children, including ones added later
---
// const off = delegate(todoList, 'click', 'button.delete', (event, button) => {
//     button.closest('li')?.remove();
// });
export function delegate<K extends keyof HTMLElementEventMap, E extends Element = HTMLElement>(
    container: HTMLElement,
    type: K,
    selector: string,
    handler: (event: HTMLElementEventMap[K], matched: E) => void,
): () => void {
    const listener = (event: HTMLElementEventMap[K]): void => {
        const matched = event.target instanceof Element ? event.target.closest<E>(selector) : null;
        if (matched && container.contains(matched)) {
            handler(event, matched);
        }
    };
    container.addEventListener(type, listener);
    return () => container.removeEventListener(type, listener);
}
