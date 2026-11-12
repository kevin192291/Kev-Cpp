---
description: parse_long(): text to a number, rejecting empty text, trailing junk and out-of-range values
order: 23
section: io
includes: <errno.h> <stdbool.h> <stdlib.h>
---
// Parses all of text as a base-10 number. False for empty text, anything after the number, or a value
// too big for a long (atoi() can't tell you any of these).
static bool parse_long(const char* text, long* out)
{
    char* end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE)
    {
        return false;
    }
    *out = value;
    return true;
}
