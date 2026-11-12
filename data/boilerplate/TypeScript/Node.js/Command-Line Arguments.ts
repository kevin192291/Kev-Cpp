---
order: 50
description: Typed command-line options with node:util parseArgs: flags, values, defaults and --help
---
import { parseArgs } from 'node:util';

const usage = `Usage: tool [options] <file...>
    -o, --out <dir>   output folder (default: dist)
    -v, --verbose     print more
    -h, --help        show this help`;

// node tool.ts -o build --verbose a.txt b.txt → values.out is 'build', positionals ['a.txt', 'b.txt'].
function readArgs() {
    try {
        return parseArgs({
            options: {
                out: { type: 'string', short: 'o', default: 'dist' },
                verbose: { type: 'boolean', short: 'v', default: false },
                help: { type: 'boolean', short: 'h', default: false },
            },
            allowPositionals: true,
        });
    } catch (error) {
        // Unknown option or missing value.
        console.error(`${error instanceof Error ? error.message : error}\n\n${usage}`);
        process.exit(2);
    }
}

const { values: options, positionals: files } = readArgs();
if (options.help) {
    console.log(usage);
    process.exit(0);
}
if (options.verbose) {
    console.log(`Writing ${files.length} file(s) to ${options.out}`);
}
{{cursor}}
