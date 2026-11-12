---
label: +page.svelte with Data
order: 10
description: A page that shows what its load function returned (data.items from +page.ts or +page.server.ts)
file: src/routes/{{Name:kebab}}/+page.svelte
whole-file: yes
ask: Name = items
---
<script lang="ts">
    import type { PageProps } from './$types';

    // data is what the load function in +page.ts or +page.server.ts beside this file returned.
    let { data }: PageProps = $props();
</script>

<svelte:head>
    <title>{{Name:pascal}}</title>
</svelte:head>

<h1>{{Name:pascal}}</h1>

<ul>
    {#each data.items as item (item.id)}
        <li>{item.title}</li>
    {:else}
        <li>Nothing here yet.</li>
    {/each}
</ul>
