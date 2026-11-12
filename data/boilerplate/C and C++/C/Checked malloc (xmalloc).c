---
description: xmalloc(), xcalloc() and xrealloc(): allocation that ends the program instead of returning NULL
order: 50
section: memory
includes: <stdio.h> <stdlib.h>
---
// Running out of memory stops the program with a message, so callers need no NULL checks. Fine for
// tools and games; a library should return an error instead.
static void out_of_memory(size_t size)
{
    fprintf(stderr, "Out of memory (%zu bytes)\n", size);
    abort(); // stops in the debugger, unlike exit()
}

void* xmalloc(size_t size)
{
    void* p = malloc(size);
    if (p == NULL && size != 0)
    {
        out_of_memory(size);
    }
    return p;
}

void* xcalloc(size_t count, size_t size)
{
    void* p = calloc(count, size);
    if (p == NULL && count != 0 && size != 0)
    {
        out_of_memory(count * size);
    }
    return p;
}

void* xrealloc(void* old, size_t size)
{
    void* p = realloc(old, size);
    if (p == NULL && size != 0)
    {
        out_of_memory(size);
    }
    return p;
}
