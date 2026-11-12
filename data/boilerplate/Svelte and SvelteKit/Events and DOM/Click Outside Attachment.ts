---
order: 10
description: An attachment that calls a function on a press outside the element or on Escape (dropdowns, popovers)
file: src/lib/attachments/click-outside.ts
whole-file: yes
---
// Usage: <div {@attach clickOutside(() => (open = false))}>…</div>
import type { Attachment } from 'svelte/attachments';
import { on } from 'svelte/events';

export function clickOutside(callback: () => void): Attachment<HTMLElement> {
    return (element) => {
        // Capture phase, so it still runs when something inside the page stops the event.
        const offPointer = on(
            document,
            'pointerdown',
            (event) => {
                if (event.target instanceof Node && !element.contains(event.target)) {
                    callback();
                }
            },
            { capture: true }
        );
        const offKey = on(document, 'keydown', (event) => {
            if (event.key === 'Escape') {
                callback();
            }
        });
        return () => {
            offPointer();
            offKey();
        };
    };
}
