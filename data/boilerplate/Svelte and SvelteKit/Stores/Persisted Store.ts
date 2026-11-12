---
label: Custom Store (localStorage)
order: 20
description: A custom store saved to localStorage and kept in step across browser tabs
file: src/lib/stores/persisted.ts
whole-file: yes
---
// const theme = persisted('theme', 'light'); then $theme = 'dark' in a component saves it.
import { get, writable, type Writable } from 'svelte/store';

export function persisted<T>(key: string, initial: T): Writable<T> {
    const store = writable(initial, (set) => {
        // Runs when the first subscriber arrives; on the server there's no localStorage.
        if (typeof localStorage === 'undefined') {
            return;
        }
        const saved = localStorage.getItem(key);
        if (saved !== null) {
            set(JSON.parse(saved) as T);
        }
        const onStorage = (event: StorageEvent) => {
            if (event.key === key && event.newValue !== null) {
                set(JSON.parse(event.newValue) as T);
            }
        };
        window.addEventListener('storage', onStorage);
        return () => window.removeEventListener('storage', onStorage);
    });

    function save(value: T) {
        if (typeof localStorage !== 'undefined') {
            localStorage.setItem(key, JSON.stringify(value));
        }
        store.set(value);
    }

    return {
        subscribe: store.subscribe,
        set: save,
        update: (updater) => save(updater(get(store)))
    };
}
