---
order: 50
description: pick(object, keys) and omit(object, keys), typed as Pick<T, K> and Omit<T, K>
---
// pick(user, ['id', 'name']) → { id, name }.
export function pick<T extends object, K extends keyof T>(object: T, keys: readonly K[]): Pick<T, K> {
    const result = {} as Pick<T, K>;
    for (const key of keys) {
        if (key in object) {
            result[key] = object[key];
        }
    }
    return result;
}

// omit(user, ['password']) → everything else.
export function omit<T extends object, K extends keyof T>(object: T, keys: readonly K[]): Omit<T, K> {
    const result = { ...object };
    for (const key of keys) {
        delete result[key];
    }
    return result;
}
