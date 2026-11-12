---
order: 20
description: A button component that passes every other attribute (disabled, onclick, aria-*) on to its <button>
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = Button
---
<script lang="ts">
    import type { HTMLButtonAttributes } from 'svelte/elements';

    interface Props extends HTMLButtonAttributes {
        variant?: 'primary' | 'secondary' | 'danger';
    }

    // rest holds everything else the parent passes: onclick, disabled, aria-label, data-*…
    let { variant = 'primary', type = 'button', class: className, children, ...rest }: Props = $props();
</script>

<button {type} class={['button', variant, className]} {...rest}>
    {@render children?.()}
</button>

<style>
    .button {
        padding: 0.5rem 1rem;
        border: 1px solid transparent;
        border-radius: 0.375rem;
        font: inherit;
        cursor: pointer;
    }

    .button:disabled {
        opacity: 0.5;
        cursor: not-allowed;
    }

    .primary {
        background: #2563eb;
        color: white;
    }

    .secondary {
        background: transparent;
        border-color: currentColor;
    }

    .danger {
        background: #dc2626;
        color: white;
    }
</style>
