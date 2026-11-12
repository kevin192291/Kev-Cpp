---
order: 80
description: A tree view that renders its children by importing itself (Svelte 5's replacement for <svelte:self>)
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = TreeView
---
<script module lang="ts">
    export interface TreeNode {
        id: string;
        label: string;
        children?: TreeNode[];
    }
</script>

<script lang="ts">
    import Self from './{{ComponentName}}.svelte';

    interface Props {
        nodes: TreeNode[];
        onselect?: (node: TreeNode) => void;
    }

    let { nodes, onselect }: Props = $props();
</script>

<ul>
    {#each nodes as node (node.id)}
        <li>
            {#if node.children?.length}
                <details open>
                    <summary>{node.label}</summary>
                    <Self nodes={node.children} {onselect} />
                </details>
            {:else}
                <button type="button" onclick={() => onselect?.(node)}>{node.label}</button>
            {/if}
        </li>
    {/each}
</ul>

<style>
    ul {
        margin: 0;
        padding-left: 1.25rem;
        list-style: none;
    }

    summary,
    button {
        cursor: pointer;
    }

    button {
        border: none;
        background: none;
        font: inherit;
        padding: 0.125rem 0;
    }
</style>
