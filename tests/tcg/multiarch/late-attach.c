/*
 * Test attaching GDB to a running process.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static const char *phase = "start";

int main(void)
{
    sigset_t set;
    int sig;

    assert(sigfillset(&set) == 0);
    assert(sigprocmask(SIG_BLOCK, &set, NULL) == 0);

    /* Let GDB know it can send SIGUSR1. */
    phase = "sigwait";
    if (getenv("LATE_ATTACH_PY")) {
        assert(sigwait(&set, &sig) == 0);
        if (sig != SIGUSR1) {
            fprintf(stderr, "Unexpected signal %d\n", sig);
            return EXIT_FAILURE;
        }
    }

    /* Check that the guest does not see host_interrupt_signal. */
    assert(sigpending(&set) == 0);
    for (sig = 1; sig < NSIG; sig++) {
        if (sigismember(&set, sig)) {
            fprintf(stderr, "Unexpected signal %d\n", sig);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
