---
description: read_file(): a whole file into a NUL-terminated buffer the caller frees
order: 21
section: io
includes: <stdio.h> <stdlib.h>
---
// Reads a whole file into a NUL-terminated buffer the caller frees; NULL if it can't be opened or read.
// Reads in growing chunks, so pipes and /proc files (which report no size) work too.
char* read_file(const char* path, size_t* size_out)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        return NULL;
    }

    size_t capacity = 4096;
    size_t size = 0;
    char* data = malloc(capacity);
    while (data != NULL)
    {
        size += fread(data + size, 1, capacity - size - 1, f);
        if (size < capacity - 1)
        {
            break; // a short read: the end of the file, or an error
        }
        char* bigger = realloc(data, capacity * 2);
        if (bigger == NULL)
        {
            free(data);
        }
        data = bigger;
        capacity *= 2;
    }
    if (data != NULL && ferror(f))
    {
        free(data);
        data = NULL;
    }
    fclose(f);

    if (data == NULL)
    {
        return NULL;
    }
    data[size] = '\0';
    if (size_out != NULL)
    {
        *size_out = size;
    }
    return data;
}
