---
order: 80
description: Reads standard input line by line with readline, for piped data or typed lines
---
import { createInterface } from 'node:readline';

// cat data.txt | node src/main.ts, or type lines and end with Ctrl+D.
const lines = createInterface({ input: process.stdin, crlfDelay: Infinity });
let count = 0;
for await (const line of lines) {
    count++;
    {{cursor}}
    console.log(`${count}: ${line}`);
}
