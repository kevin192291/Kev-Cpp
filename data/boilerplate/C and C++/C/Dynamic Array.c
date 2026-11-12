---
description: A growable array (C's std::vector) with push, pop and free
ask: Name = IntArray
ask: Type = int
order: 31
section: data
includes: <stdbool.h> <stdlib.h>
---
// A growable array of {{Type}}: start from {{Name:pascal}} a = {0}; and free it with {{Name:snake}}_free(&a).
// Items are a.items[0] to a.items[a.count - 1].
typedef struct {{Name:pascal}}
{
    {{Type}}* items;
    size_t count;
    size_t capacity;
} {{Name:pascal}};

// Returns false (leaving the array as it was) when out of memory.
bool {{Name:snake}}_push({{Name:pascal}}* a, {{Type}} item)
{
    if (a->count == a->capacity)
    {
        size_t capacity = a->capacity == 0 ? 8 : a->capacity * 2;
        {{Type}}* items = realloc(a->items, capacity * sizeof *items);
        if (items == NULL)
        {
            return false;
        }
        a->items = items;
        a->capacity = capacity;
    }
    a->items[a->count++] = item;
    return true;
}

// Takes the last item off into *out; false if the array is empty.
bool {{Name:snake}}_pop({{Name:pascal}}* a, {{Type}}* out)
{
    if (a->count == 0)
    {
        return false;
    }
    *out = a->items[--a->count];
    return true;
}

void {{Name:snake}}_free({{Name:pascal}}* a)
{
    free(a->items);
    a->items = NULL;
    a->count = 0;
    a->capacity = 0;
}
