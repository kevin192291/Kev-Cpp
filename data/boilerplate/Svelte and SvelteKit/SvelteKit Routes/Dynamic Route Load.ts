---
label: [slug] Route Load
order: 40
description: A load function for a dynamic route that reads params.slug and shows a 404 page when nothing matches
file: src/routes/{{Name:kebab}}/[slug]/+page.server.ts
whole-file: yes
ask: Name = posts
---
import { error } from '@sveltejs/kit';
import type { PageServerLoad } from './$types';

interface Item {
    slug: string;
    title: string;
    body: string;
}

// params.slug is the [slug] part of the URL: /{{Name:kebab}}/hello-world gives 'hello-world'.
export const load: PageServerLoad = async ({ params, fetch }) => {
    const response = await fetch(`/api/{{Name:kebab}}/${encodeURIComponent(params.slug)}`);
    if (response.status === 404) {
        error(404, 'Not found');
    }
    if (!response.ok) {
        error(response.status, 'Could not load it');
    }
    return { item: (await response.json()) as Item };
};
