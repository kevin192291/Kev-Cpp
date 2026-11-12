---
order: 30
description: h(tag, props, ...children): builds typed DOM elements without innerHTML
---
type Props<K extends keyof HTMLElementTagNameMap> = Partial<Omit<HTMLElementTagNameMap[K], 'style' | 'dataset'>> & {
    style?: Partial<CSSStyleDeclaration>;
    dataset?: Record<string, string>;
};

// const button = h('button', { className: 'primary', type: 'submit', onclick: save }, 'Save');
// list.append(h('li', { dataset: { id: '7' } }, h('strong', {}, name), ' joined'));
// Strings become text nodes, never parsed as HTML, so user input can't inject markup.
export function h<K extends keyof HTMLElementTagNameMap>(
    tag: K,
    props: Props<K> = {},
    ...children: (Node | string)[]
): HTMLElementTagNameMap[K] {
    const { style, dataset, ...rest } = props;
    const element = Object.assign(document.createElement(tag), rest);
    if (style) {
        Object.assign(element.style, style);
    }
    if (dataset) {
        Object.assign(element.dataset, dataset);
    }
    element.append(...children);
    return element;
}
