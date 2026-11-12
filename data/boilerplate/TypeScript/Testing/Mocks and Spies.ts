---
order: 20
description: Vitest test doubles: vi.fn records calls, vi.spyOn watches a method, vi.stubGlobal replaces fetch
---
// Uses expect, it and vi from 'vitest'.
it('records calls with vi.fn', () => {
    // A stand-in callback; hand it to the code under test instead of calling it here.
    const onSave = vi.fn((text: string) => text.length);
    ['draft', 'final'].forEach((text) => onSave(text));

    expect(onSave).toHaveBeenCalledTimes(2);
    expect(onSave).toHaveBeenLastCalledWith('final');
    expect(onSave.mock.results[0]?.value).toBe(5);
});

it('watches a method with vi.spyOn', () => {
    // Replaces console.warn with a silent spy until mockRestore() (or vi.restoreAllMocks()).
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    console.warn('disk almost full');

    expect(warn).toHaveBeenCalledWith('disk almost full');
    warn.mockRestore();
});

it('replaces fetch with vi.stubGlobal', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => Response.json({ id: 1, name: 'Ada' })));
    try {
        const response = await fetch('/api/users/1');

        expect(await response.json()).toEqual({ id: 1, name: 'Ada' });
        expect(fetch).toHaveBeenCalledWith('/api/users/1');
    } finally {
        vi.unstubAllGlobals();
    }
});
