---
description: Comparison functions for sorting ints and strings with qsort()
order: 41
section: functions
includes: <stdlib.h> <string.h>
---
// qsort() wants <0, 0 or >0; (x > y) - (x < y) gives that without the overflow x - y can hit.
//     qsort(numbers, count, sizeof numbers[0], compare_ints);
//     qsort(names, count, sizeof names[0], compare_strings);
static int compare_ints(const void* a, const void* b)
{
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x > y) - (x < y);
}

// For an array of char*: qsort() passes pointers to the elements, so these are really char**.
static int compare_strings(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}
