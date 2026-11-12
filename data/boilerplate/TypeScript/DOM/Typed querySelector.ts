---
order: 10
description: getElement(selector, Type): finds an element and checks its type, so a bad selector fails early
---
// const canvas = getElement('#game', HTMLCanvasElement);  → HTMLCanvasElement, never null.
// Throws a clear error when nothing matches or the element is of another type, instead of a
// "cannot read properties of null" somewhere later.
export function getElement<T extends Element>(
    selector: string,
    type: { new (): T; prototype: T },
    root: ParentNode = document,
): T {
    const element = root.querySelector(selector);
    if (!(element instanceof type)) {
        const found = element ? `a <${element.tagName.toLowerCase()}>` : 'nothing';
        throw new Error(`Expected ${selector} to be a ${type.name}, found ${found}`);
    }
    return element;
}

// Every match as a real array. Tag names are typed by TypeScript itself:
// document.querySelectorAll('li') is NodeListOf<HTMLLIElement>.
export function getElements<T extends Element = HTMLElement>(selector: string, root: ParentNode = document): T[] {
    return Array.from(root.querySelectorAll<T>(selector));
}
