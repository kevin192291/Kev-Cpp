---
order: 60
section: wrappers
description: Debounce: runs a function once calls have stopped for `wait` ms, with the last arguments
---
// const save = debounce((text: string) => saveDraft(text), 500); save(a); save(b); → saves b once.
// save.cancel() drops a pending call (e.g. when a component is destroyed).
export function debounce<Args extends unknown[]>(fn: (...args: Args) => void, wait: number) {
    let timer: ReturnType<typeof setTimeout> | undefined;

    const debounced = (...args: Args): void => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            timer = undefined;
            fn(...args);
        }, wait);
    };
    debounced.cancel = (): void => {
        clearTimeout(timer);
        timer = undefined;
    };
    return debounced;
}
