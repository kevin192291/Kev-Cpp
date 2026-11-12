---
label: Form with use:enhance (+page.svelte)
order: 20
description: The page for Form Actions: posts without a reload, shows errors and keeps the fields' text
file: src/routes/{{Name:kebab}}/+page.svelte
whole-file: yes
ask: Name = contact
---
<script lang="ts">
    import { enhance } from '$app/forms';
    import type { PageProps } from './$types';

    // form is what the action returned: the fail(…) object after a problem, { success: true } after it worked.
    let { form }: PageProps = $props();
    let sending = $state(false);
</script>

<!-- Works without JavaScript too; use:enhance just avoids the full page reload. -->
<form
    method="POST"
    use:enhance={() => {
        sending = true;
        return async ({ update }) => {
            // Does what a normal submission would: updates `form`, and clears the fields after a success.
            await update();
            sending = false;
        };
    }}
>
    {#if form?.error}
        <p class="error" role="alert">{form.error}</p>
    {:else if form?.success}
        <p class="success" role="status">Thanks, your message was sent.</p>
    {/if}

    <label>
        Name
        <input name="name" value={form?.name ?? ''} required />
    </label>
    <label>
        Email
        <input name="email" type="email" value={form?.email ?? ''} required />
    </label>
    <label>
        Message
        <textarea name="message" value={form?.message ?? ''} required></textarea>
    </label>

    <button disabled={sending}>{sending ? 'Sending…' : 'Send'}</button>
</form>

<style>
    form {
        display: grid;
        gap: 0.75rem;
        max-width: 30rem;
    }

    label {
        display: grid;
        gap: 0.25rem;
    }

    .error {
        color: #dc2626;
    }

    .success {
        color: #16a34a;
    }
</style>
