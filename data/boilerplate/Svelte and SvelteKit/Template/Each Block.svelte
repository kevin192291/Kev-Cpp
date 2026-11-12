---
label: {#each} with Key and Index
order: 20
description: A keyed {#each} list with the index and an {:else} for an empty list
---
<ul>
    {#each items as item, index (item.id)}
        <li>{index + 1}. {item.name}</li>
    {:else}
        <li>No items yet.</li>
    {/each}
</ul>
