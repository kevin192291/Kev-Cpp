---
order: 40
description: A card with a children snippet and optional header and footer snippets (what slots became in Svelte 5)
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = Card
---
<script lang="ts">
    import type { Snippet } from 'svelte';

    interface Props {
        header?: Snippet;
        footer?: Snippet;
        children: Snippet;
    }

    let { header, footer, children }: Props = $props();
</script>

<!--
    <{{ComponentName}}>
        {#snippet header()}<h2>Title</h2>{/snippet}
        Anything not in a snippet is the children snippet.
        {#snippet footer()}<button>OK</button>{/snippet}
    </{{ComponentName}}>
-->
<article class="card">
    {#if header}
        <header>{@render header()}</header>
    {/if}
    <div class="body">
        {@render children()}
    </div>
    {#if footer}
        <footer>{@render footer()}</footer>
    {/if}
</article>

<style>
    .card {
        border: 1px solid #d4d4d8;
        border-radius: 0.5rem;
        overflow: hidden;
    }

    header,
    footer {
        padding: 0.75rem 1rem;
        background: #f4f4f5;
    }

    .body {
        padding: 1rem;
    }
</style>
