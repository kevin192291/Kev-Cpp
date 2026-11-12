---
label: Async Function with try/catch
order: 80
section: errors
description: An async function whose failures are rethrown with context (the original kept as `cause`)
ask: Name = loadData
---
export async function {{Name}}(): Promise<void> {
    try {
        {{cursor}}
    } catch (error) {
        // In strict mode `error` is unknown: narrow it before reading .message.
        const message = error instanceof Error ? error.message : String(error);
        throw new Error(`{{Name}} failed: ${message}`, { cause: error });
    }
}
