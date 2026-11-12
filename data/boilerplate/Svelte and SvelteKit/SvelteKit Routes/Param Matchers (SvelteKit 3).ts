---
order: 130
section: matchers
description: Param matchers in src/params.ts: [id=integer] matches digits and gives a number (SvelteKit 3)
file: src/params.ts
whole-file: yes
---
// Name a route folder [id=integer] and it only matches when the matcher returns a value (that value becomes
// params.id); undefined means no match, and the URL falls through to the next route or a 404.
import { defineParams } from '@sveltejs/kit/params';

export const params = defineParams({
    integer: (param) => (/^\d+$/.test(param) ? Number(param) : undefined),
    lang: (param) => (param === 'en' || param === 'ja' ? param : undefined)
});
