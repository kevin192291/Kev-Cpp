---
order: 100
section: helpers
description: Page options for +page.ts, +page.server.ts or a +layout file (a layout's apply to every page below it)
---
// Build this page into static HTML at build time.
export const prerender = true;
// Render on the server (false: render only in the browser).
export const ssr = true;
// Load JavaScript and hydrate (false: plain HTML, no scripts).
export const csr = true;
// '/about' rather than '/about/'.
export const trailingSlash = 'never';
