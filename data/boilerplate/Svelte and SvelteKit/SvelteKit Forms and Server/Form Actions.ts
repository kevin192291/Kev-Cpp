---
label: Form Actions (+page.server.ts)
order: 10
description: A form action that validates the posted fields and returns fail(400, …) so the page can show the problem
file: src/routes/{{Name:kebab}}/+page.server.ts
whole-file: yes
ask: Name = contact
---
import { fail } from '@sveltejs/kit';
import type { Actions } from './$types';

export const actions = {
    // <form method="POST"> posts here. Add named actions (save: async (event) => …) and post to them
    // with <form method="POST" action="?/save"> instead.
    default: async ({ request }) => {
        const data = await request.formData();
        const name = String(data.get('name') ?? '').trim();
        const email = String(data.get('email') ?? '').trim();
        const message = String(data.get('message') ?? '').trim();

        // fail() sets the status and hands this object to the page as `form`, so the fields keep their text.
        if (!name || !message) {
            return fail(400, { name, email, message, error: 'Please fill in your name and a message.' });
        }
        if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
            return fail(400, { name, email, message, error: 'Please enter a valid email address.' });
        }

        // Store or send the message here (database, mail service…).

        return { success: true };
    }
} satisfies Actions;
