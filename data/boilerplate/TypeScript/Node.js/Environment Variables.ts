---
order: 60
description: Configuration from environment variables: defaults, numbers, booleans, and a clear error when one is missing
---
// Read everything once at startup, so a missing setting fails at launch, not hours later.
// Node can load a .env file itself: node --env-file=.env src/main.ts (or process.loadEnvFile()).
function env(name: string, fallback?: string): string {
    const value = process.env[name];
    if (value !== undefined && value !== '') {
        return value;
    }
    if (fallback === undefined) {
        throw new Error(`Environment variable ${name} is not set`);
    }
    return fallback;
}

function envInt(name: string, fallback?: number): number {
    const text = env(name, fallback?.toString());
    const value = Number(text);
    if (!Number.isInteger(value)) {
        throw new Error(`Environment variable ${name} must be a whole number, got "${text}"`);
    }
    return value;
}

function envBool(name: string, fallback = false): boolean {
    return ['1', 'true', 'yes', 'on'].includes(env(name, String(fallback)).toLowerCase());
}

export const config = {
    port: envInt('PORT', 3000),
    databaseUrl: env('DATABASE_URL'),
    logLevel: env('LOG_LEVEL', 'info'),
    debug: envBool('DEBUG'),
    production: process.env.NODE_ENV === 'production',
};
