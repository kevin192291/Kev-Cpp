---
label: In View Attachment (IntersectionObserver)
order: 20
description: An attachment that reports when the element scrolls into or out of view (lazy loading, reveal effects)
file: src/lib/attachments/in-view.ts
whole-file: yes
---
// Usage: <section {@attach inView((visible) => (shown = visible), { rootMargin: '100px' })}>…</section>
import type { Attachment } from 'svelte/attachments';

export function inView(
    callback: (visible: boolean, entry: IntersectionObserverEntry) => void,
    options: IntersectionObserverInit = {}
): Attachment {
    return (element) => {
        const observer = new IntersectionObserver((entries) => {
            for (const entry of entries) {
                callback(entry.isIntersecting, entry);
            }
        }, options);
        observer.observe(element);
        return () => observer.disconnect();
    };
}
