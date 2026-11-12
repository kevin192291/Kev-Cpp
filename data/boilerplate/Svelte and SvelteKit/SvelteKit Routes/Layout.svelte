---
label: +layout.svelte
order: 50
description: A root layout with a navigation bar that marks the current page and a bar shown while navigating
file: src/routes/+layout.svelte
whole-file: yes
---
<script lang="ts">
    import { navigating, page } from '$app/state';
    import type { LayoutProps } from './$types';

    // children is the page (or nested layout) inside this layout.
    let { children }: LayoutProps = $props();

    const links = [
        { href: '/', label: 'Home' },
        { href: '/about', label: 'About' }
    ];
</script>

{#if navigating.to}
    <div class="loading" aria-hidden="true"></div>
{/if}

<header>
    <nav>
        {#each links as link (link.href)}
            <a href={link.href} aria-current={page.url.pathname === link.href ? 'page' : undefined}>{link.label}</a>
        {/each}
    </nav>
</header>

<main>
    {@render children()}
</main>

<style>
    nav {
        display: flex;
        gap: 1rem;
        padding: 1rem;
    }

    a[aria-current='page'] {
        font-weight: bold;
    }

    main {
        padding: 1rem;
    }

    .loading {
        position: fixed;
        top: 0;
        left: 0;
        height: 3px;
        background: #2563eb;
        animation: grow 2s ease-out forwards;
    }

    @keyframes grow {
        from {
            width: 0;
        }
        to {
            width: 90%;
        }
    }
</style>
