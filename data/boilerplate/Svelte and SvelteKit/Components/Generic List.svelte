---
order: 60
description: A list generic over its item type (generics="T"): the parent passes items, a key function and a row snippet
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = SelectList
---
<script lang="ts" generics="T">
    import type { Snippet } from 'svelte';

    interface Props {
        items: T[];
        key: (item: T) => string | number;
        // The parent writes {#snippet row(item)}…{/snippet} inside the component tag.
        row: Snippet<[T]>;
        selected?: T;
        onselect?: (item: T) => void;
        empty?: Snippet;
    }

    let { items, key, row, selected = $bindable(), onselect, empty }: Props = $props();

    function choose(item: T) {
        selected = item;
        onselect?.(item);
    }
</script>

<ul>
    {#each items as item (key(item))}
        <li>
            <button
                type="button"
                aria-current={selected !== undefined && key(selected) === key(item) ? 'true' : undefined}
                onclick={() => choose(item)}
            >
                {@render row(item)}
            </button>
        </li>
    {:else}
        <li class="empty">
            {#if empty}
                {@render empty()}
            {:else}
                Nothing here.
            {/if}
        </li>
    {/each}
</ul>

<style>
    ul {
        margin: 0;
        padding: 0;
        list-style: none;
    }

    button {
        width: 100%;
        padding: 0.5rem 0.75rem;
        border: none;
        background: none;
        font: inherit;
        text-align: left;
        cursor: pointer;
    }

    button:hover,
    button[aria-current='true'] {
        background: #e0e7ff;
    }

    .empty {
        padding: 0.5rem 0.75rem;
        color: #71717a;
    }
</style>
