---
order: 40
description: walk(dir): an async generator of every file path under a folder, skipping node_modules and .git
---
import { readdir } from 'node:fs/promises';
import { join } from 'node:path';

// for await (const file of walk('src')) {
//     if (file.endsWith('.ts')) console.log(file);
// }
// For a quick list, readdir(dir, { recursive: true }) or glob('**/*.ts') from node:fs/promises
// (Node 22+) also work; walk() can skip whole folders without reading them.
export async function* walk(
    dir: string,
    skip: ReadonlySet<string> = new Set(['node_modules', '.git']),
): AsyncGenerator<string> {
    for (const entry of await readdir(dir, { withFileTypes: true })) {
        const path = join(dir, entry.name);
        if (entry.isDirectory()) {
            if (!skip.has(entry.name)) {
                yield* walk(path, skip);
            }
        } else if (entry.isFile()) {
            yield path;
        }
    }
}
