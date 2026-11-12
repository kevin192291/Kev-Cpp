---
label: Error Handling with errno/perror
description: Reports a failed fopen() with perror() and a failed fclose() with strerror(errno)
order: 24
section: io
includes: <errno.h> <stdio.h> <stdlib.h> <string.h>
---
// perror() prints "text: reason" for the call that just failed; strerror(errno) gives the reason for a
// message of your own. Read errno straight after the failure: later calls may change it.
// path is the file's name.
FILE* f = fopen(path, "w");
if (f == NULL)
{
    perror(path); // "out.txt: Permission denied"
    return EXIT_FAILURE;
}
{{cursor}}
if (fclose(f) != 0)
{
    fprintf(stderr, "Couldn't write %s: %s\n", path, strerror(errno));
    return EXIT_FAILURE;
}
