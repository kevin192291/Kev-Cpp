---
label: +layout.server.ts Load
order: 60
description: A layout load: what it returns is part of the data of every page below the layout (here, a theme cookie)
file: src/routes/+layout.server.ts
whole-file: yes
---
import type { LayoutServerLoad } from './$types';

export const load: LayoutServerLoad = ({ cookies }) => {
    return {
        theme: cookies.get('theme') === 'dark' ? 'dark' : 'light'
    };
};
