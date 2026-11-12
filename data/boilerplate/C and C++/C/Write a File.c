---
description: write_file(): writes a buffer to a file and reports failure, including a full disk at fclose()
order: 22
section: io
includes: <stdbool.h> <stdio.h>
---
// Writes size bytes to path, replacing the file. Returns false on failure (errno says why).
// fclose() is checked too: buffered data is written then, so a full disk can show up there.
//     write_file("notes.txt", text, strlen(text));
bool write_file(const char* path, const void* data, size_t size)
{
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        return false;
    }
    bool ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0)
    {
        ok = false;
    }
    return ok;
}
