---
description: A main() that checks its arguments, prints usage when there are none and goes through each one
file: main.c
whole-file: yes
order: 11
section: starters
---
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s FILE...\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; ++i)
    {
        printf("%d: %s\n", i, argv[i]);
    }
    {{cursor}}
    return EXIT_SUCCESS;
}
