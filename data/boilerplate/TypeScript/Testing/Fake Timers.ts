---
order: 40
description: Vitest fake timers: test setTimeout, setInterval, debounce and Date.now() code without waiting
---
// Uses afterEach, beforeEach, expect, it and vi from 'vitest'.
beforeEach(() => {
    vi.useFakeTimers();
});

afterEach(() => {
    vi.useRealTimers();
});

it('fires after the delay', () => {
    const callback = vi.fn();
    setTimeout(callback, 1000);

    vi.advanceTimersByTime(999);
    expect(callback).not.toHaveBeenCalled();
    vi.advanceTimersByTime(1);
    expect(callback).toHaveBeenCalledOnce();
    // With promises between timers, use: await vi.advanceTimersByTimeAsync(1000).
});

it('sets the clock', () => {
    vi.setSystemTime(new Date('2026-01-01T09:00:00Z')); // Date.now() and new Date() return this
    expect(new Date().toISOString()).toBe('2026-01-01T09:00:00.000Z');
});
