---
order: 20
description: Toast messages that fly in, fade out and slide into place (in:fly, out:fade, animate:flip)
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = Toasts
---
<script module lang="ts">
    export interface Toast {
        id: number;
        message: string;
        kind?: 'info' | 'success' | 'error';
    }
</script>

<script lang="ts">
    import { flip } from 'svelte/animate';
    import { fade, fly } from 'svelte/transition';

    interface Props {
        toasts: Toast[];
        ondismiss: (id: number) => void;
    }

    let { toasts, ondismiss }: Props = $props();
</script>

<ul aria-live="polite">
    {#each toasts as toast (toast.id)}
        <li
            class={toast.kind ?? 'info'}
            animate:flip={{ duration: 200 }}
            in:fly={{ y: 24, duration: 250 }}
            out:fade={{ duration: 150 }}
        >
            <span>{toast.message}</span>
            <button type="button" aria-label="Dismiss" onclick={() => ondismiss(toast.id)}>✕</button>
        </li>
    {/each}
</ul>

<style>
    ul {
        position: fixed;
        right: 1rem;
        bottom: 1rem;
        display: grid;
        gap: 0.5rem;
        margin: 0;
        padding: 0;
        list-style: none;
    }

    li {
        display: flex;
        align-items: center;
        gap: 1rem;
        padding: 0.75rem 1rem;
        border-radius: 0.375rem;
        color: white;
        background: #2563eb;
    }

    .success {
        background: #16a34a;
    }

    .error {
        background: #dc2626;
    }

    button {
        border: none;
        background: none;
        color: inherit;
        cursor: pointer;
    }
</style>
