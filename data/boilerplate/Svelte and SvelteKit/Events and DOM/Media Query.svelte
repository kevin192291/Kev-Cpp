---
order: 50
description: MediaQuery from svelte/reactivity: .current follows a CSS media query live (in <script lang="ts">)
---
import { MediaQuery } from 'svelte/reactivity';

// The second argument is what the server assumes, since it can't know the screen.
const wide = new MediaQuery('min-width: 48rem', true);
const prefersDark = new MediaQuery('prefers-color-scheme: dark');
// Markup: {#if wide.current}…{/if}
