---
order: 90
description: prompt(question): asks in the terminal and waits for the answer; piped answers work too
---
import * as readline from 'node:readline';

// One reader for the whole script. Its lines are queued, so answers piped in ahead of the
// questions (printf 'Ada\ny\n' | node script.ts) are not lost, as they would be with
// a new interface or rl.question() per question.
const terminal = readline.createInterface({ input: process.stdin, output: process.stdout });
const answers = terminal[Symbol.asyncIterator]();

// const name = await prompt('Project name? ');
// const sure = (await prompt('Delete everything? [y/N] '))?.toLowerCase() === 'y';
// Gives null once input has ended (Ctrl+D, or the end of piped input). Call terminal.close()
// after the last question, or Node keeps waiting for more input.
export async function prompt(question: string): Promise<string | null> {
    terminal.setPrompt(question);
    terminal.prompt();
    const next = await answers.next();
    return next.done ? null : next.value.trim();
}
