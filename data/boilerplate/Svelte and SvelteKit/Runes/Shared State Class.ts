---
label: Shared State Class (.svelte.ts)
order: 70
section: modules
description: A class with $state and $derived fields that any component can use; runes need the .svelte.ts name
file: src/lib/state/{{ClassName:kebab}}.svelte.ts
whole-file: yes
ask: ClassName = Cart
---
// Reactive state in a class. Make one per user (in a component, shared down through context): a single
// instance at module level would be shared by every request during server-side rendering.
export interface {{ClassName}}Item {
    id: string;
    name: string;
    price: number;
    quantity: number;
}

export class {{ClassName}} {
    items = $state<{{ClassName}}Item[]>([]);

    count = $derived(this.items.reduce((sum, item) => sum + item.quantity, 0));
    total = $derived(this.items.reduce((sum, item) => sum + item.price * item.quantity, 0));

    add(item: Omit<{{ClassName}}Item, 'quantity'>, quantity = 1) {
        const existing = this.items.find((entry) => entry.id === item.id);
        if (existing) {
            existing.quantity += quantity;
        } else {
            this.items.push({ ...item, quantity });
        }
    }

    remove(id: string) {
        this.items = this.items.filter((item) => item.id !== id);
    }

    clear() {
        this.items = [];
    }
}
