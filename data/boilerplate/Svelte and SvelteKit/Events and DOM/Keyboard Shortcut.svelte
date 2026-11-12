---
order: 30
description: A window-wide Ctrl+K (⌘K on a Mac) shortcut; <svelte:window> goes at the top level of the component
---
<svelte:window
    onkeydown={(event) => {
        if (event.key.toLowerCase() === 'k' && (event.ctrlKey || event.metaKey)) {
            event.preventDefault();
            {{cursor}}
        }
    }}
/>
