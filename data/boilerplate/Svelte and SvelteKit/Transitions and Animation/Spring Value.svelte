---
order: 50
description: A Spring from svelte/motion that follows the pointer with a little bounce
---
import { Spring } from 'svelte/motion';

// Set coords.target; coords.current springs toward it. Works with numbers and arrays or objects of numbers.
const coords = new Spring({ x: 0, y: 0 }, { stiffness: 0.1, damping: 0.25 });
// Markup:
// <svelte:window onpointermove={(event) => (coords.target = { x: event.clientX, y: event.clientY })} />
// <div class="dot" style:translate="{coords.current.x}px {coords.current.y}px"></div>
