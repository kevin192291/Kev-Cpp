---
label: +server.ts JSON Endpoint
order: 80
description: An API route answering GET with a JSON list and POST by adding to it (validates the body)
file: src/routes/api/{{Name:kebab}}/+server.ts
whole-file: yes
ask: Name = items
---
import { error } from '@sveltejs/kit';
import type { RequestHandler } from './$types';

interface Item {
    id: number;
    title: string;
}

// Kept in memory to start with (lost on restart, not shared between servers): swap in a database.
const items: Item[] = [];
let nextId = 1;

// GET /api/{{Name:kebab}}?q=text
export const GET: RequestHandler = ({ url }) => {
    const query = url.searchParams.get('q')?.toLowerCase() ?? '';
    return Response.json(query ? items.filter((item) => item.title.toLowerCase().includes(query)) : items);
};

// POST /api/{{Name:kebab}} with a body like {"title": "Milk"}
export const POST: RequestHandler = async ({ request }) => {
    const body = await request.json().catch(() => null);
    const title = typeof body?.title === 'string' ? body.title.trim() : '';
    if (!title) {
        error(400, 'Expected a JSON body like {"title": "…"}');
    }

    const item: Item = { id: nextId++, title };
    items.push(item);
    return Response.json(item, { status: 201 });
};
