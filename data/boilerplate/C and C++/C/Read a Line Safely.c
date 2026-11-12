---
description: read_line(): fgets() without the newline, skipping the rest of a line too long for the buffer
order: 20
section: io
includes: <stdbool.h> <stdio.h> <string.h>
---
// Reads one line into buf without its newline. A longer line is cut to fit and the rest of it skipped.
// Returns false at the end of the input.
//     char line[256];
//     while (read_line(line, sizeof line, stdin)) { ... }
static bool read_line(char* buf, size_t size, FILE* in)
{
    if (fgets(buf, (int)size, in) == NULL)
    {
        return false;
    }
    size_t len = strcspn(buf, "\n");
    if (buf[len] == '\n')
    {
        buf[len] = '\0';
    }
    else
    {
        int c = fgetc(in);
        while (c != EOF && c != '\n')
        {
            c = fgetc(in);
        }
    }
    return true;
}
