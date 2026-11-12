---
label: $effect with Cleanup
order: 50
description: An $effect that starts a timer while `running` is true and stops it when it re-runs or the component goes
---
let running = $state(true);
let seconds = $state(0);

// Re-runs when `running` changes; the returned function runs first, and when the component is destroyed.
$effect(() => {
    if (!running) {
        return;
    }
    const timer = setInterval(() => {
        seconds += 1;
    }, 1000);
    return () => clearInterval(timer);
});
