---
order: 10
description: A writable store with methods beside it and a store derived from it, for state shared outside components
file: src/lib/stores/messages.ts
whole-file: yes
---
// In components $messages reads the store and stays up to date; elsewhere use get(messages) or subscribe.
// New component code can usually use $state instead; stores still suit plain .ts modules and streams.
import { derived, writable } from 'svelte/store';

export interface Message {
    id: number;
    text: string;
    read: boolean;
}

export const messages = writable<Message[]>([]);

export const unreadCount = derived(messages, ($messages) => $messages.filter((message) => !message.read).length);

let nextId = 1;

export function addMessage(text: string) {
    messages.update((list) => [...list, { id: nextId++, text, read: false }]);
}

export function markRead(id: number) {
    messages.update((list) => list.map((message) => (message.id === id ? { ...message, read: true } : message)));
}
