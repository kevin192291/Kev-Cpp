---
order: 10
description: A Svelte 5 component with typed props, markup and scoped styles
file: src/lib/components/{{ComponentName}}.svelte
whole-file: yes
ask: ComponentName = Card
---
<script lang="ts">
    interface Props {
        title: string;
    }

    let { title }: Props = $props();
</script>

<section class="{{ComponentName:kebab}}">
    <h2>{title}</h2>
    {{cursor}}
</section>

<style>
    .{{ComponentName:kebab}} {
        padding: 1rem;
    }
</style>
