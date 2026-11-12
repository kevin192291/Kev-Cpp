---
label: <svelte:boundary> Error Boundary
order: 80
description: Shows a fallback with a Try again button when the selected markup throws while rendering
---
<svelte:boundary onerror={(error) => console.error(error)}>
    {{selection}}{{cursor}}

    {#snippet failed(error, reset)}
        <p>Something went wrong: {error instanceof Error ? error.message : String(error)}</p>
        <button onclick={reset}>Try again</button>
    {/snippet}
</svelte:boundary>
