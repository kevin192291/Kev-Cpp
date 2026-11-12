---
label: $derived.by for Longer Computations
order: 40
description: A value computed by a function with $derived.by, for computations that need statements
---
let values = $state<number[]>([]);

let stats = $derived.by(() => {
    if (values.length === 0) {
        return { min: 0, max: 0, average: 0 };
    }
    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    for (const value of values) {
        min = Math.min(min, value);
        max = Math.max(max, value);
        sum += value;
    }
    return { min, max, average: sum / values.length };
});
