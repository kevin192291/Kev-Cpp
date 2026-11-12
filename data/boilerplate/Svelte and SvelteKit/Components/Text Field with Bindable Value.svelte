---
order: 30
description: A labelled input whose value the parent binds with bind:value; shows an error message when given one
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = TextField
---
<script lang="ts">
    import type { HTMLInputAttributes } from 'svelte/elements';

    interface Props extends Omit<HTMLInputAttributes, 'value'> {
        label: string;
        value?: string;
        error?: string;
    }

    // $bindable: the parent can write <{{ComponentName}} label="Name" bind:value={name} />.
    let { label, value = $bindable(''), error, ...rest }: Props = $props();

    // Unique per instance and the same on the server and in the browser.
    const id = $props.id();
</script>

<div class="field">
    <label for={id}>{label}</label>
    <input
        {id}
        bind:value
        aria-invalid={error ? true : undefined}
        aria-describedby={error ? `${id}-error` : undefined}
        {...rest}
    />
    {#if error}
        <p id="{id}-error" class="error">{error}</p>
    {/if}
</div>

<style>
    .field {
        display: grid;
        gap: 0.25rem;
    }

    input[aria-invalid='true'] {
        border-color: #dc2626;
    }

    .error {
        margin: 0;
        color: #dc2626;
        font-size: 0.875rem;
    }
</style>
