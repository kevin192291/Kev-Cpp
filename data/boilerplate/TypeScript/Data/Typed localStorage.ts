---
order: 80
description: A localStorage wrapper with typed keys and JSON values that never throws
---
// The keys this app stores and the type of each value.
export interface StoredValues {
    theme: 'light' | 'dark';
    recentFiles: string[];
    volume: number;
}

// Pages on the same origin (every project on localhost:5173) share one storage, hence the prefix.
const prefix = '{{project:kebab}}:';

// storage.get('volume') is number | undefined; storage.set('theme', 'blue') doesn't compile.
// Values are not validated when read back: users and other code can edit storage.
export const storage = {
    get<K extends keyof StoredValues>(key: K): StoredValues[K] | undefined {
        try {
            const text = localStorage.getItem(prefix + key);
            return text === null ? undefined : (JSON.parse(text) as StoredValues[K]);
        } catch {
            return undefined; // storage blocked, or not valid JSON
        }
    },

    set<K extends keyof StoredValues>(key: K, value: StoredValues[K]): void {
        try {
            localStorage.setItem(prefix + key, JSON.stringify(value));
        } catch {
            // Quota exceeded or storage blocked: carry on without saving.
        }
    },

    remove(key: keyof StoredValues): void {
        try {
            localStorage.removeItem(prefix + key);
        } catch {
            // Storage blocked: nothing to remove.
        }
    },
};
