---
order: 100
section: async
description: await sleep(ms): a promise that resolves after a delay, optionally cancelled by an AbortSignal
---
// await sleep(500). With a signal the wait can be cut short; the promise then rejects with the
// signal's reason. (In Node, `import { setTimeout as sleep } from 'node:timers/promises'` is built in.)
export function sleep(ms: number, signal?: AbortSignal): Promise<void> {
    return new Promise((resolve, reject) => {
        if (signal?.aborted) {
            reject(signal.reason);
            return;
        }
        const onAbort = (): void => {
            clearTimeout(timer);
            reject(signal?.reason);
        };
        const timer = setTimeout(() => {
            signal?.removeEventListener('abort', onAbort);
            resolve();
        }, ms);
        signal?.addEventListener('abort', onAbort, { once: true });
    });
}
