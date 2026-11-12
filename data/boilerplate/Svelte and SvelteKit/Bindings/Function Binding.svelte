---
label: Function Binding (Get and Set)
order: 50
description: bind:value with a getter and a setter, to transform or validate what the input writes
---
<input
    bind:value={
        () => value,
        (next) => (value = next.toLowerCase())
    }
/>
