---
order: 10
description: A Node.js script in TypeScript: top-level await, exit code on failure; `node file.ts` runs it (Node 22.18+)
file: src/main.ts
whole-file: yes
---
#!/usr/bin/env node
// Run with `node src/main.ts`: Node 22.18+ runs TypeScript by removing the types, so leave out
// enums and constructor parameter properties. "type": "module" in package.json makes tsc treat
// the file as an ES module, which top-level await needs.
import { basename } from 'node:path';

async function main(args: string[]): Promise<void> {
    console.log(`${basename(import.meta.filename)} started with`, args);
    {{cursor}}
}

try {
    await main(process.argv.slice(2));
} catch (error) {
    console.error(error instanceof Error ? error.message : error);
    process.exitCode = 1;
}
