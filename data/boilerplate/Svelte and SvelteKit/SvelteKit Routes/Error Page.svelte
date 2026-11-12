---
label: +error.svelte
order: 70
description: The page shown when a load function fails or calls error(): the status and a safe message
file: src/routes/+error.svelte
whole-file: yes
---
<script lang="ts">
    import { page } from '$app/state';
</script>

<svelte:head>
    <title>{page.status} {page.error?.message ?? 'Error'}</title>
</svelte:head>

<h1>{page.status}</h1>
<p>{page.error?.message ?? 'Something went wrong.'}</p>

{#if page.status === 404}
    <p><a href="/">Go to the home page</a></p>
{/if}
