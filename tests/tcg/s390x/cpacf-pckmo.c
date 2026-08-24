/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for CPACF PCKMO instruction (privileged operation).
 *
 * As PCKMO is a privileged instruction, when executed in user mode,
 * it triggers a privileged operation exception, which is mapped to SIGILL.
 */

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cpacf.h"

static void handle_sigill(int sig, siginfo_t *info, void *ucontext)
{
    /* PCKMO is privileged, so we expect SIGILL - this is success */
    _exit(EXIT_SUCCESS);
}

int main(void)
{
    struct sigaction act;
    uint8_t param_block[32] = {0};  /* Parameter block for PCKMO */
    int err;

    /* Setup signal handler for SIGILL (privileged operation exception) */
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGILL, &act, NULL);
    assert(err == 0);

    /*
     * Attempt to execute PCKMO with subfunction CPACF_PCKMO_QUERY.
     * As PCKMO is privileged, it should trigger SIGILL.
     */
    cpacf_pckmo(CPACF_PCKMO_QUERY, param_block);

    /* Should never be reached - PCKMO should trigger exception */
    return EXIT_FAILURE;
}
