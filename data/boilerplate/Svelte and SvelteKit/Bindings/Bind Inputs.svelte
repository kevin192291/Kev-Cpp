---
label: Bind Text, Number and Checkbox
order: 10
description: bind:value on text, number and textarea fields and bind:checked on a checkbox ($state variables)
---
<label>
    Name
    <input bind:value={name} />
</label>
<label>
    Age
    <input type="number" bind:value={age} min="0" />
</label>
<label>
    Message
    <textarea bind:value={message}></textarea>
</label>
<label>
    <input type="checkbox" bind:checked={subscribed} />
    Send me updates
</label>
