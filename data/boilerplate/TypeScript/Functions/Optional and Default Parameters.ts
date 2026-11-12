---
order: 30
section: functions
description: Optional (?) and default parameters, an options object with defaults, and rest parameters
---
// `title` may be left out (it is undefined then); `repeat` falls back to 1.
export function greet(name: string, title?: string, repeat = 1): string {
    const who = title === undefined ? name : `${title} ${name}`;
    return Array.from({ length: repeat }, () => `Hello, ${who}!`).join(' ');
}

// Past two or three optional parameters, take an options object: callers name what they set.
export interface ConnectOptions {
    port?: number;
    secure?: boolean;
    timeoutMs?: number;
}

export function connectionUrl(
    host: string,
    { port = 443, secure = true, timeoutMs = 5000 }: ConnectOptions = {},
): string {
    return `${secure ? 'wss' : 'ws'}://${host}:${port}/?timeout=${timeoutMs}`;
}

// Rest parameters gather the remaining arguments into an array: sum(1, 2, 3).
export function sum(...numbers: number[]): number {
    return numbers.reduce((total, n) => total + n, 0);
}
