---
label: {#snippet} and {@render}
order: 60
description: A reusable piece of markup with typed parameters, rendered twice with {@render}
---
{#snippet badge(text: string, tone: 'info' | 'warning')}
    <span class="badge {tone}">{text}</span>
{/snippet}

{@render badge('New', 'info')}
{@render badge('Expired', 'warning')}
