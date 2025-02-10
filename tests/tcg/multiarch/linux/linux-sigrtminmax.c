/*
 * Test the lowest and the highest real-time signals.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* For hexagon and microblaze. */
#ifndef __SIGRTMIN
#define __SIGRTMIN 32
#endif

extern char **environ;

static bool seen_sigrtmin, seen_sigrtmax;

static void handle_signal(int sig)
{
    if (sig == SIGRTMIN) {
        seen_sigrtmin = true;
    } else if (sig == SIGRTMAX) {
        seen_sigrtmax = true;
    } else {
        _exit(1);
    }
}

int main(int argc, char **argv)
{
    char *qemu = getenv("QEMU");
    struct sigaction act;

    assert(qemu);

    if (!getenv("QEMU_RTSIG_MAP")) {
        char **new_argv = malloc((argc + 2) + sizeof(char *));
        int tsig1, hsig1, count1, tsig2, hsig2, count2;
        char rt_sigmap[64];

        /* Re-exec with a mapping that includes SIGRTMIN and SIGRTMAX. */
        new_argv[0] = qemu;
        memcpy(&new_argv[1], argv, (argc + 1) * sizeof(char *));
        tsig1 = __SIGRTMIN;
        /* The host must have a few signals starting from this one. */
        hsig1 = 36;
        count1 = SIGRTMIN - __SIGRTMIN + 1;
        tsig2 = SIGRTMAX;
        hsig2 = hsig1 + count1;
        count2 = 1;
        snprintf(rt_sigmap, sizeof(rt_sigmap), "%d %d %d,%d %d %d",
                 tsig1, hsig1, count1, tsig2, hsig2, count2);
        setenv("QEMU_RTSIG_MAP", rt_sigmap, 0);
        assert(execve(new_argv[0], new_argv, environ) == 0);
        return EXIT_FAILURE;
    }

    memset(&act, 0, sizeof(act));
    act.sa_handler = handle_signal;
    assert(sigaction(SIGRTMIN, &act, NULL) == 0);
    assert(sigaction(SIGRTMAX, &act, NULL) == 0);

    assert(kill(getpid(), SIGRTMIN) == 0);
    assert(seen_sigrtmin);
    assert(kill(getpid(), SIGRTMAX) == 0);
    assert(seen_sigrtmax);

    return EXIT_SUCCESS;
}
