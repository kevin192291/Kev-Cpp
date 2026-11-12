---
order: 30
description: A custom transition (scale and fade) to use as transition:pop, in:pop or out:pop
file: src/lib/transitions/pop.ts
whole-file: yes
---
// Usage: import { pop } from this file, then <div transition:pop> or <div in:pop={{ scale: 0.5 }}>.
import { cubicOut } from 'svelte/easing';
import type { TransitionConfig } from 'svelte/transition';

interface PopParams {
    delay?: number;
    duration?: number;
    scale?: number;
}

// css runs ahead of time as a CSS animation, so it stays smooth while the main thread is busy.
// t goes from 0 to 1 when entering and from 1 to 0 when leaving.
export function pop(node: Element, { delay = 0, duration = 250, scale = 0.8 }: PopParams = {}): TransitionConfig {
    const opacity = Number(getComputedStyle(node).opacity);
    return {
        delay,
        duration,
        easing: cubicOut,
        css: (t) => `opacity: ${t * opacity}; scale: ${scale + (1 - scale) * t};`
    };
}
