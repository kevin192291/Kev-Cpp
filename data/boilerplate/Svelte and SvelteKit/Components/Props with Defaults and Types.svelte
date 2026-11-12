---
order: 90
section: patterns
description: Typed $props with defaults, an optional children snippet and a callback prop (in <script lang="ts">)
---
import type { Snippet } from 'svelte';

interface Props {
    title: string;
    count?: number;
    variant?: 'info' | 'warning';
    children?: Snippet;
    // A callback prop: call it where Svelte 4 would dispatch an event.
    onchange?: (count: number) => void;
}

let { title, count = 0, variant = 'info', children, onchange }: Props = $props();
{{cursor}}
