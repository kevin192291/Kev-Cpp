---
order: 40
section: functions
description: A Result type (Ok/Err) for errors the caller must handle, instead of throwing
---
export type Result<T, E = Error> = { ok: true; value: T } | { ok: false; error: E };

export function Ok<T>(value: T): Result<T, never> {
    return { ok: true, value };
}

export function Err<E>(error: E): Result<never, E> {
    return { ok: false, error };
}

// Turns a call that throws or rejects into a Result.
export async function attempt<T>(fn: () => T | Promise<T>): Promise<Result<T>> {
    try {
        return Ok(await fn());
    } catch (error) {
        return Err(error instanceof Error ? error : new Error(String(error)));
    }
}

// Example. The caller can't reach .value without checking .ok first:
// const port = parsePort(text);
// if (port.ok) listen(port.value); else console.error(port.error);
export function parsePort(text: string): Result<number, string> {
    const port = Number(text);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        return Err(`Not a port number: ${text}`);
    }
    return Ok(port);
}
