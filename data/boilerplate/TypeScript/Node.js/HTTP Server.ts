---
order: 20
description: A small JSON API on node:http with no dependencies: routing, body parsing, errors, clean shutdown
file: src/server.ts
whole-file: yes
---
// A JSON API on Node's own http module. Run: node src/server.ts, then
// curl -X POST localhost:3000/api/todos -d '{"title":"Try it"}' and curl localhost:3000/api/todos
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';

const port = Number(process.env.PORT ?? 3000);

interface Todo {
    id: number;
    title: string;
    done: boolean;
}

const todos: Todo[] = [];

class HttpError extends Error {
    readonly status: number;

    constructor(status: number, message: string) {
        super(message);
        this.status = status;
    }
}

function sendJson(res: ServerResponse, status: number, data: unknown): void {
    const body = JSON.stringify(data);
    res.writeHead(status, {
        'content-type': 'application/json; charset=utf-8',
        'content-length': Buffer.byteLength(body),
    });
    res.end(body);
}

async function readJson(req: IncomingMessage, maxBytes = 1_000_000): Promise<unknown> {
    const chunks: Buffer[] = [];
    let size = 0;
    for await (const chunk of req) {
        const buffer = chunk as Buffer;
        size += buffer.length;
        if (size > maxBytes) {
            throw new HttpError(413, 'Request body too large');
        }
        chunks.push(buffer);
    }
    try {
        return JSON.parse(Buffer.concat(chunks).toString('utf8'));
    } catch {
        throw new HttpError(400, 'Request body is not valid JSON');
    }
}

async function handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const url = new URL(req.url ?? '/', 'http://localhost');
    const route = `${req.method} ${url.pathname}`;

    if (route === 'GET /api/todos') {
        return sendJson(res, 200, todos);
    }
    if (route === 'POST /api/todos') {
        const body = await readJson(req);
        if (typeof body !== 'object' || body === null || !('title' in body) || typeof body.title !== 'string') {
            throw new HttpError(422, 'Expected { "title": string }');
        }
        const todo: Todo = { id: todos.length + 1, title: body.title, done: false };
        todos.push(todo);
        return sendJson(res, 201, todo);
    }
    {{cursor}}
    throw new HttpError(404, `No route for ${route}`);
}

const server = createServer((req, res) => {
    handle(req, res).catch((error: unknown) => {
        if (error instanceof HttpError) {
            sendJson(res, error.status, { error: error.message });
        } else {
            console.error(error);
            sendJson(res, 500, { error: 'Internal server error' });
        }
    });
});

server.listen(port, '127.0.0.1', () => {
    console.log(`Listening on http://127.0.0.1:${port}`);
});

// Ctrl+C or `kill`: stop accepting connections and exit once open requests finish.
for (const signal of ['SIGINT', 'SIGTERM'] as const) {
    process.on(signal, () => server.close(() => process.exit(0)));
}
