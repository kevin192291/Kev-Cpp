---
label: {#await} Block
order: 30
description: An {#await} block showing a loading message, the result, or the error
---
{#await promise}
    <p>Loading…</p>
{:then value}
    <p>{value}</p>
{:catch error}
    <p class="error">{error.message}</p>
{/await}
