---
description: str_dup(), str_concat() and str_format(): strings in new memory the caller frees
order: 34
section: data
includes: <stdarg.h> <stdio.h> <stdlib.h> <string.h>
---
// Each returns a new string the caller frees, or NULL when out of memory.
char* str_dup(const char* s)
{
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy != NULL)
    {
        memcpy(copy, s, len);
    }
    return copy;
}

char* str_concat(const char* a, const char* b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char* out = malloc(len_a + len_b + 1);
    if (out != NULL)
    {
        memcpy(out, a, len_a);
        memcpy(out + len_a, b, len_b + 1);
    }
    return out;
}

// printf() into a new string: str_format("%s-%03d.png", name, frame).
char* str_format(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (len < 0)
    {
        return NULL;
    }
    char* out = malloc((size_t)len + 1);
    if (out != NULL)
    {
        va_start(args, format);
        vsnprintf(out, (size_t)len + 1, format, args);
        va_end(args);
    }
    return out;
}
