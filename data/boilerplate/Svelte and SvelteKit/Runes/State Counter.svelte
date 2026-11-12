---
label: $state Counter
order: 10
description: A $state counter with a $derived value and an increment function (in <script lang="ts">)
---
let count = $state(0);
// Updates by itself whenever count changes.
let doubled = $derived(count * 2);

function increment() {
    count += 1;
}
// Markup: <button onclick={increment}>Clicked {count} times ({doubled})</button>
