/*
 * Common user code for specification exception testing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void test(void);
extern long expected_old_psw[2];

static void handle_sigill(int sig, siginfo_t *info, void *ucontext)
{
    if ((long)info->si_addr != expected_old_psw[1]) {
        _exit(EXIT_FAILURE);
    }
    _exit(EXIT_SUCCESS);
}

int main(void)
{
    struct sigaction act;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGILL, &act, NULL);
    assert(err == 0);

    test();

    return EXIT_FAILURE;
}
