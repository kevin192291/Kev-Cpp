---
order: 120
section: matchers
description: A param matcher in src/params/: [id=integer] only matches digits (SvelteKit 2)
file: src/params/integer.ts
whole-file: yes
---
// Name a route folder [id=integer] and it only matches when this returns true; other URLs fall through
// to the next route, or to a 404. The file name is the matcher's name.
import type { ParamMatcher } from '@sveltejs/kit';

export const match = ((param: string): param is `${number}` => /^\d+$/.test(param)) satisfies ParamMatcher;
