---
description: A command-line tool whose options (-v, -o FILE, -n COUNT, -h) are read with getopt()
file: main.c
whole-file: yes
order: 12
section: starters
---
// prog [-v] [-o FILE] [-n COUNT] INPUT...
#define _POSIX_C_SOURCE 200809L // getopt() under -std=c17

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(FILE* out, const char* program)
{
    fprintf(out, "Usage: %s [-v] [-o FILE] [-n COUNT] INPUT...\n", program);
}

int main(int argc, char* argv[])
{
    bool verbose = false;
    const char* output = "out.txt";
    long count = 1;

    int opt;
    while ((opt = getopt(argc, argv, "vo:n:h")) != -1)
    {
        switch (opt)
        {
        case 'v':
            verbose = true;
            break;
        case 'o':
            output = optarg;
            break;
        case 'n': {
            char* end;
            count = strtol(optarg, &end, 10);
            if (end == optarg || *end != '\0' || count < 1)
            {
                fprintf(stderr, "%s: -n needs a positive number, not '%s'\n", argv[0], optarg);
                return EXIT_FAILURE;
            }
            break;
        }
        case 'h':
            usage(stdout, argv[0]);
            return EXIT_SUCCESS;
        default: // getopt() has already said what was wrong
            usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind >= argc)
    {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (verbose)
    {
        printf("output %s, count %ld\n", output, count);
    }
    for (int i = optind; i < argc; ++i)
    {
        printf("input: %s\n", argv[i]);
    }
    {{cursor}}
    return EXIT_SUCCESS;
}
