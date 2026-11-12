---
order: 70
description: parseJson(text, isValid): undefined instead of an exception, and only data that passes a check
---
// const settings = parseJson(text, isSettings); → Settings | undefined.
export function parseJson<T>(text: string, isValid: (value: unknown) => value is T): T | undefined {
    let value: unknown;
    try {
        value = JSON.parse(text);
    } catch {
        return undefined;
    }
    return isValid(value) ? value : undefined;
}

// An example check; write one for each shape you accept.
export interface Settings {
    theme: 'light' | 'dark';
    fontSize: number;
}

export function isSettings(value: unknown): value is Settings {
    return (
        typeof value === 'object' &&
        value !== null &&
        'theme' in value &&
        (value.theme === 'light' || value.theme === 'dark') &&
        'fontSize' in value &&
        typeof value.fontSize === 'number'
    );
}
