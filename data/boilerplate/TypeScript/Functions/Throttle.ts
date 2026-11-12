---
order: 70
section: wrappers
description: Throttle: runs a function at most once per interval; the last call in a burst still runs
---
// window.addEventListener('scroll', throttle(updateHeader, 100));
// The first call runs at once; calls during the wait collapse into one at its end, with the
// latest arguments, so the final state is never missed.
export function throttle<Args extends unknown[]>(fn: (...args: Args) => void, interval: number) {
    let lastRun = 0;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let pendingArgs: Args | undefined;

    const run = (args: Args): void => {
        lastRun = Date.now();
        fn(...args);
    };

    return (...args: Args): void => {
        const wait = lastRun + interval - Date.now();
        if (wait <= 0 && timer === undefined) {
            run(args);
            return;
        }
        pendingArgs = args;
        timer ??= setTimeout(() => {
            timer = undefined;
            const latest = pendingArgs;
            pendingArgs = undefined;
            if (latest !== undefined) {
                run(latest);
            }
        }, wait);
    };
}
