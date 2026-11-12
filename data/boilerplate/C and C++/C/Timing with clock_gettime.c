---
description: now_seconds(): a monotonic clock for measuring how long something takes
order: 60
section: system
includes: <time.h>
---
// Seconds from an arbitrary start. CLOCK_MONOTONIC never jumps when the system clock is set, so the
// difference between two calls is a true duration:
//     double start = now_seconds();
//     ...
//     printf("took %.3f s\n", now_seconds() - start);
// Under -std=c17, put #define _POSIX_C_SOURCE 199309L above the first #include.
static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
