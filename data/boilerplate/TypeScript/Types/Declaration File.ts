---
order: 130
description: A global .d.ts: types for window properties, build-time constants, untyped packages and asset imports
file: src/global.d.ts
whole-file: yes
---
// Types for things TypeScript can't see. Declarations only, no code that runs. Keep top-level
// import/export out of this file: they make it a module, and `interface Window` then stops
// adding to the global Window.

// window.appConfig, set by a <script> tag before the app loads.
interface Window {
    appConfig?: {
        apiUrl: string;
        version: string;
    };
}

// A constant the bundler replaces at build time (Vite's `define`, esbuild's --define).
declare const __APP_VERSION__: string;

// A package that ships no types and has none on DefinitelyTyped (try npm i -D @types/<name> first).
declare module 'untyped-package' {
    export function slugify(text: string): string;
}

// Importing a non-code file (Vite already declares images, CSS and ?raw in vite/client).
declare module '*.csv' {
    const rows: string[][];
    export default rows;
}
