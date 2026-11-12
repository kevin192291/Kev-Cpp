---
order: 50
description: whenVisible(elements, callback): runs once per element as it scrolls near the viewport
---
// For entrance animations, widgets that load when reached, "was seen" analytics. (Images don't
// need it: <img loading="lazy"> is built in.)
// whenVisible(document.querySelectorAll('.reveal'), (element) => element.classList.add('shown'));
export function whenVisible<T extends Element>(
    elements: Iterable<T>,
    onVisible: (element: T) => void,
    rootMargin = '200px', // start this far before the element is on screen
): () => void {
    const observer = new IntersectionObserver(
        (entries) => {
            for (const entry of entries) {
                if (entry.isIntersecting) {
                    observer.unobserve(entry.target);
                    onVisible(entry.target as T);
                }
            }
        },
        { rootMargin },
    );
    for (const element of elements) {
        observer.observe(element);
    }
    return () => observer.disconnect();
}
