---
order: 40
description: A Tween from svelte/motion: set .target and .current glides there (progress bars, counters)
---
import { cubicOut } from 'svelte/easing';
import { Tween } from 'svelte/motion';

// progress.target = 0.75 (or await progress.set(0.75)) animates progress.current to it.
const progress = new Tween(0, { duration: 400, easing: cubicOut });
// To follow another value instead: const shown = Tween.of(() => value, { duration: 400 });
// Markup: <progress value={progress.current}></progress>
