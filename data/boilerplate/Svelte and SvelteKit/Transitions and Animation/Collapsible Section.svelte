---
order: 10
description: A section whose body slides open and closed (transition:slide); the parent can bind:open
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = Collapsible
---
<script lang="ts">
    import type { Snippet } from 'svelte';
    import { slide } from 'svelte/transition';

    interface Props {
        title: string;
        open?: boolean;
        children: Snippet;
    }

    let { title, open = $bindable(false), children }: Props = $props();
    const bodyId = $props.id();
</script>

<section>
    <button type="button" aria-expanded={open} aria-controls={bodyId} onclick={() => (open = !open)}>
        <span class={['arrow', { open }]} aria-hidden="true">▸</span>
        {title}
    </button>
    {#if open}
        <div id={bodyId} transition:slide={{ duration: 200 }}>
            {@render children()}
        </div>
    {/if}
</section>

<style>
    button {
        display: flex;
        align-items: center;
        gap: 0.5rem;
        width: 100%;
        padding: 0.5rem 0;
        border: none;
        background: none;
        font: inherit;
        cursor: pointer;
    }

    .arrow {
        transition: rotate 0.2s;
    }

    .arrow.open {
        rotate: 90deg;
    }
</style>
