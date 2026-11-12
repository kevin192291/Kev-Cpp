---
order: 120
description: `satisfies`: check a value against a type while keeping its own, more exact type
---
interface AppConfig {
    theme: 'light' | 'dark';
    port: number;
    routes: Record<string, string>;
}

// Checked against AppConfig (a typo or a wrong type is an error), yet config.routes.home is
// known to exist and config.theme is 'dark'. With `: AppConfig` both would be lost.
export const config = {
    theme: 'dark',
    port: 8080,
    routes: {
        home: '/',
        about: '/about',
    },
} satisfies AppConfig;

// Every value must be a hex colour; the keys stay the ones written here.
export const colors = {
    primary: '#3366ff',
    danger: '#e53935',
} satisfies Record<string, `#${string}`>;
