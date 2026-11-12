---
order: 100
description: Numbers for people with Intl: currency, compact (1.3M), percentages and file sizes
---
// Formatters are slow to create, so make them once. `undefined` means the user's own locale;
// the examples show en-US output.
const usd = new Intl.NumberFormat(undefined, { style: 'currency', currency: 'USD' });
const yen = new Intl.NumberFormat('ja-JP', { style: 'currency', currency: 'JPY' });
const compact = new Intl.NumberFormat(undefined, { notation: 'compact', maximumFractionDigits: 1 });
const percent = new Intl.NumberFormat(undefined, { style: 'percent', maximumFractionDigits: 1 });

export const formatUsd = (amount: number): string => usd.format(amount); // 1234.5 → "$1,234.50"
export const formatYen = (amount: number): string => yen.format(amount); // 1234.5 → "￥1,235"
export const formatCompact = (n: number): string => compact.format(n); // 1_250_000 → "1.3M"
export const formatPercent = (ratio: number): string => percent.format(ratio); // 0.256 → "25.6%"

// formatBytes(1536) → "1.5 kB", formatBytes(3_500_000) → "3.5 MB" (powers of 1000, like file managers).
export function formatBytes(bytes: number): string {
    const units = ['byte', 'kilobyte', 'megabyte', 'gigabyte', 'terabyte'] as const;
    let value = bytes;
    let index = 0;
    while (Math.abs(value) >= 1000 && index < units.length - 1) {
        value /= 1000;
        index++;
    }
    return new Intl.NumberFormat(undefined, {
        style: 'unit',
        unit: units[index],
        unitDisplay: index === 0 ? 'long' : 'short',
        maximumFractionDigits: 1,
    }).format(value);
}
