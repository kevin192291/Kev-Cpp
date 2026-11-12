---
label: bind:this Element Reference
order: 30
description: A reference to a DOM element for calling its methods from a handler (in <script lang="ts">)
---
// Markup: <input bind:this={input} /> <button onclick={startEditing}>Edit</button>
let input = $state<HTMLInputElement>();

function startEditing() {
    input?.focus();
    input?.select();
}
// To run code when an element appears (draw on a canvas, start a library), {@attach …} is simpler.
