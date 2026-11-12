---
order: 80
section: modules
description: A typed context made with createContext: a parent sets the value, any component inside it gets it
file: src/lib/context/{{Name:kebab}}.ts
whole-file: yes
ask: Name = Theme
---
import { createContext } from 'svelte';

// Share $state (or a class with $state fields) so readers see changes. Context belongs to one
// component tree, so unlike a module-level variable it is never shared between users on the server.
export interface {{Name}}Context {
    mode: 'light' | 'dark';
}

export const [get{{Name}}Context, set{{Name}}Context] = createContext<{{Name}}Context>();

// In a parent (a +layout.svelte, say), during component setup:
//     const {{Name:camel}} = $state<{{Name}}Context>({ mode: 'light' });
//     set{{Name}}Context({{Name:camel}});
//
// In any component inside it:
//     const {{Name:camel}} = get{{Name}}Context();
//     const dark = $derived({{Name:camel}}.mode === 'dark');
