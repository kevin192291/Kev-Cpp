---
description: A function with one exit that releases whatever it acquired, reached by goto on any failure
order: 51
section: memory
includes: <stdio.h> <stdlib.h>
---
// Everything starts out NULL so the cleanup can release only what was acquired. Returns 0 on success.
int process_file(const char* path)
{
    int result = -1;
    FILE* f = NULL;
    char* buffer = NULL;

    f = fopen(path, "rb");
    if (f == NULL)
    {
        goto cleanup;
    }
    buffer = malloc(4096);
    if (buffer == NULL)
    {
        goto cleanup;
    }
    {{cursor}}
    result = 0;

cleanup:
    free(buffer);
    if (f != NULL)
    {
        fclose(f);
    }
    return result;
}
