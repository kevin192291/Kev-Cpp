---
order: 70
description: A search box that calls onsearch once typing pauses for `delay` ms; the parent can also bind:value
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = SearchInput
---
<script lang="ts">
    interface Props {
        value?: string;
        delay?: number;
        placeholder?: string;
        onsearch: (query: string) => void;
    }

    let { value = $bindable(''), delay = 300, placeholder = 'Search…', onsearch }: Props = $props();

    let timer: ReturnType<typeof setTimeout> | undefined;

    function schedule() {
        clearTimeout(timer);
        timer = setTimeout(() => onsearch(value.trim()), delay);
    }

    function searchNow(event: KeyboardEvent) {
        if (event.key === 'Enter') {
            clearTimeout(timer);
            onsearch(value.trim());
        }
    }

    // A pending search must not fire after the component is gone.
    $effect(() => {
        return () => clearTimeout(timer);
    });
</script>

<input type="search" bind:value oninput={schedule} onkeydown={searchNow} {placeholder} aria-label={placeholder} />
