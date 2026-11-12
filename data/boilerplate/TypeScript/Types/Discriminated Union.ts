---
order: 30
description: A union told apart by a 'status' field, with a switch the compiler checks covers every case
---
export type RequestState<T> =
    | { status: 'idle' }
    | { status: 'loading' }
    | { status: 'success'; data: T }
    | { status: 'error'; error: Error };

export function describeState<T>(state: RequestState<T>): string {
    switch (state.status) {
        case 'idle':
            return 'Not started';
        case 'loading':
            return 'Loading...';
        case 'success':
            return `Loaded ${JSON.stringify(state.data)}`; // only 'success' has data
        case 'error':
            return `Failed: ${state.error.message}`;
        default:
            return assertNever(state);
    }
}

// Adding a member to the union turns this call into a compile error until the switch handles it.
export function assertNever(value: never): never {
    throw new Error(`Unhandled case: ${JSON.stringify(value)}`);
}
