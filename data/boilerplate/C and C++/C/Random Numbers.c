---
description: random_int(min, max) without modulo bias, and random_double() from 0 up to 1
order: 61
section: system
includes: <stdlib.h> <time.h>
---
// rand() suits games and tests, not secrets. Seed it once at start-up: srand((unsigned int)time(NULL));

// A number from min to max, both included; max - min must be less than RAND_MAX. rand() % n would
// favour small numbers, so draws past the last whole multiple of the range are thrown away.
static int random_int(int min, int max)
{
    unsigned int range = (unsigned int)max - (unsigned int)min + 1u;
    unsigned int per_value = ((unsigned int)RAND_MAX + 1u) / range;
    unsigned int limit = per_value * range;
    unsigned int r;
    do
    {
        r = (unsigned int)rand();
    } while (r >= limit);
    return min + (int)(r / per_value);
}

// A number from 0 up to, but not including, 1.
static double random_double(void)
{
    return rand() / ((double)RAND_MAX + 1.0);
}
