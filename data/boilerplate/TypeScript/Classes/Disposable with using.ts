---
order: 50
description: A resource cleaned up by `using` (TypeScript 5.2+): [Symbol.dispose] runs when the scope ends
---
// `using x = ...` calls x[Symbol.dispose]() when the block ends, even if it throws; `await using`
// calls [Symbol.asyncDispose]. Needs "ESNext.Disposable" (or "ESNext") in tsconfig's "lib";
// @types/node adds it. Node 24 runs `using` as it is. For older targets tsc rewrites it into
// try/finally, but Symbol.dispose must still exist at run time (Node 20+, recent browsers).
//
// function exportReport() {
//     using timer = new Timer('export');
//     ...  // "export: 12.3 ms" is logged here, however the function exits
// }
export class Timer implements Disposable {
    readonly #label: string;
    readonly #start = performance.now();

    constructor(label: string) {
        this.#label = label;
    }

    [Symbol.dispose](): void {
        console.log(`${this.#label}: ${(performance.now() - this.#start).toFixed(1)} ms`);
    }
}

// Any object with a [Symbol.dispose] method works, so a function can hand out cleanup:
// using _listener = listen(window, 'resize', onResize);
export function listen(target: EventTarget, type: string, handler: (event: Event) => void): Disposable {
    target.addEventListener(type, handler);
    return { [Symbol.dispose]: () => target.removeEventListener(type, handler) };
}
