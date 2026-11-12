---
description: Ctrl+C (and SIGTERM) set a flag the main loop checks, so the program can stop cleanly
order: 62
section: system
includes: <signal.h>
---
// A handler may only do async-signal-safe things; setting a volatile sig_atomic_t is one.
// Under -std=c17, put #define _POSIX_C_SOURCE 200809L above the first #include.
static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

// Call once at start-up, then loop while (!stop_requested). No SA_RESTART: a blocking read or sleep
// returns early (errno EINTR), so the loop notices straight away.
static void install_signal_handlers(void)
{
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}
