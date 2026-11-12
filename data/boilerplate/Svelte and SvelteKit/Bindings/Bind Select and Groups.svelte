---
label: Bind Select, Radio and Checkbox Group
order: 20
description: A bound <select>, a radio group (one value) and a checkbox group (an array of the checked values)
---
<select bind:value={country}>
    {#each countries as option (option.code)}
        <option value={option.code}>{option.name}</option>
    {/each}
</select>

<fieldset>
    <legend>Size</legend>
    {#each ['small', 'medium', 'large'] as size (size)}
        <label>
            <input type="radio" name="size" value={size} bind:group={chosenSize} />
            {size}
        </label>
    {/each}
</fieldset>

<fieldset>
    <legend>Toppings</legend>
    {#each ['cheese', 'olives', 'peppers'] as topping (topping)}
        <label>
            <input type="checkbox" value={topping} bind:group={toppings} />
            {topping}
        </label>
    {/each}
</fieldset>
