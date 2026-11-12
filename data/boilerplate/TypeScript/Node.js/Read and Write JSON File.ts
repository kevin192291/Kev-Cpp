---
order: 30
description: readJson(file, fallback) and writeJson(file, data) with fs/promises; writes can't leave half a file
---
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import { dirname } from 'node:path';

// const settings = await readJson('settings.json', { theme: 'dark' });
// Missing file → fallback. Invalid JSON or no permission still throws.
// The type comes from the fallback; validate the contents if others edit the file.
export async function readJson<T>(file: string, fallback: T): Promise<T> {
    try {
        return JSON.parse(await readFile(file, 'utf8')) as T;
    } catch (error) {
        if (error instanceof Error && 'code' in error && error.code === 'ENOENT') {
            return fallback;
        }
        throw error;
    }
}

// Writes a temporary file and renames it over the old one, so a crash mid-write leaves the old
// file intact. Creates missing folders.
export async function writeJson(file: string, data: unknown): Promise<void> {
    await mkdir(dirname(file), { recursive: true });
    const temp = `${file}.${process.pid}.tmp`;
    await writeFile(temp, `${JSON.stringify(data, null, 4)}\n`);
    await rename(temp, file);
}
