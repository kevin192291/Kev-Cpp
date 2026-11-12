---
order: 70
description: Run a program: collect its output (execFile), or stream it to the terminal and get the exit code (spawn)
---
import { execFile, spawn } from 'node:child_process';
import { promisify } from 'node:util';

const execFileAsync = promisify(execFile);

// const commit = await run('git', ['rev-parse', '--short', 'HEAD']);
// No shell is involved, so arguments with spaces, quotes or $ reach the program as written.
// Rejects if the program is missing or exits with a non-zero code (error.stderr has its message).
export async function run(command: string, args: readonly string[], cwd?: string): Promise<string> {
    const { stdout } = await execFileAsync(command, args, { cwd, maxBuffer: 64 * 1024 * 1024 });
    return stdout.trim();
}

// const code = await runInTerminal('npm', ['test']);
// Output goes straight to this process's terminal, colours included; resolves with the exit code.
export function runInTerminal(command: string, args: readonly string[], cwd?: string): Promise<number> {
    return new Promise((resolve, reject) => {
        const child = spawn(command, args, { cwd, stdio: 'inherit' });
        child.on('error', reject); // e.g. ENOENT: the program isn't installed
        child.on('close', (code) => resolve(code ?? 1)); // null code: killed by a signal
    });
}
