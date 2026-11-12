---
order: 90
description: Dates for people with Intl: locale formats, time zones, "3 days ago", and a local YYYY-MM-DD
---
// Formatters are slow to create, so make them once. `undefined` means the user's own locale.
const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' }); // Nov 12, 2026
const dateTimeFormat = new Intl.DateTimeFormat(undefined, { dateStyle: 'full', timeStyle: 'short' });
const tokyoTime = new Intl.DateTimeFormat('ja-JP', { timeZone: 'Asia/Tokyo', hour: '2-digit', minute: '2-digit' });
const relativeTime = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto' }); // "yesterday", "in 3 hours"

export const formatDate = (date: Date): string => dateFormat.format(date);
export const formatDateTime = (date: Date): string => dateTimeFormat.format(date);
export const formatTokyoTime = (date: Date): string => tokyoTime.format(date);

const units: [Intl.RelativeTimeFormatUnit, number][] = [
    ['year', 365 * 24 * 3600],
    ['month', 30 * 24 * 3600],
    ['week', 7 * 24 * 3600],
    ['day', 24 * 3600],
    ['hour', 3600],
    ['minute', 60],
];

// timeAgo(date) → "5 minutes ago", "yesterday", "in 2 weeks", "now".
export function timeAgo(date: Date, now = new Date()): string {
    const seconds = (date.getTime() - now.getTime()) / 1000;
    for (const [unit, size] of units) {
        if (Math.abs(seconds) >= size) {
            return relativeTime.format(Math.round(seconds / size), unit);
        }
    }
    return relativeTime.format(Math.round(seconds), 'second');
}

// YYYY-MM-DD in local time. toISOString().slice(0, 10) gives the UTC date, a day off near midnight.
export function localIsoDate(date: Date): string {
    const pad = (n: number): string => String(n).padStart(2, '0');
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
}
