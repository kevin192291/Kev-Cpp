---
order: 50
description: A modal <dialog> opened with bind:open that calls its onclose callback prop when it closes
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = Modal
---
<script lang="ts">
    import type { Snippet } from 'svelte';

    interface Props {
        open?: boolean;
        title: string;
        // Callback props replace Svelte 4's createEventDispatcher.
        onclose?: () => void;
        children: Snippet;
    }

    let { open = $bindable(false), title, onclose, children }: Props = $props();

    const titleId = $props.id();
</script>

<dialog
    aria-labelledby={titleId}
    {@attach (dialog) => {
        // Runs again whenever `open` changes.
        if (open && !dialog.open) {
            dialog.showModal();
        } else if (!open && dialog.open) {
            dialog.close();
        }
    }}
    onclose={() => {
        // Escape, the close button and the backdrop all end up here.
        open = false;
        onclose?.();
    }}
    onclick={(event) => {
        if (event.target === event.currentTarget) {
            open = false;
        }
    }}
>
    <div class="content">
        <header>
            <h2 id={titleId}>{title}</h2>
            <button type="button" aria-label="Close" onclick={() => (open = false)}>✕</button>
        </header>
        {@render children()}
    </div>
</dialog>

<style>
    dialog {
        padding: 0;
        border: none;
        border-radius: 0.5rem;
        max-width: min(32rem, 90vw);
    }

    dialog::backdrop {
        background: rgb(0 0 0 / 0.4);
    }

    .content {
        padding: 1rem 1.25rem;
    }

    header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 1rem;
    }

    h2 {
        margin: 0;
        font-size: 1.25rem;
    }

    header button {
        border: none;
        background: none;
        font-size: 1.25rem;
        cursor: pointer;
    }
</style>
