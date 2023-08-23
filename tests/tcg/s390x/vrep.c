/*
 * Test the VREP instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vx.h"

static void handle_sigill(int sig, siginfo_t *info, void *ucontext)
{
    mcontext_t *mcontext = &((ucontext_t *)ucontext)->uc_mcontext;
    char *insn = (char *)info->si_addr;

    if (insn[0] != 0xe7 || insn[5] != 0x4d) {
        _exit(EXIT_FAILURE);
    }

    mcontext->gregs[2] = SIGILL;
}

static inline __attribute__((__always_inline__)) unsigned long
vrep(S390Vector *v1, const S390Vector *v3, const uint16_t i2, const uint8_t m4)
{
    register unsigned long sig asm("r2") = -1;

    asm("vrep %[v1],%[v3],%[i2],%[m4]\n"
        : [v1] "=v" (v1->v)
        , [sig] "+r" (sig)
        : [v3] "v" (v3->v)
        , [i2] "i" (i2)
        , [m4] "i" (m4));

    return sig;
}

int main(int argc, char *argv[])
{
    S390Vector v3 = {.d[0] = 1, .d[1] = 2};
    struct sigaction act;
    S390Vector v1;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGILL, &act, NULL);
    assert(err == 0);

    assert(vrep(&v1, &v3, 7, 0) == -1);
    assert(v1.d[0] == 0x0101010101010101ULL);
    assert(v1.d[1] == 0x0101010101010101ULL);

    assert(vrep(&v1, &v3, 7, 1) == -1);
    assert(v1.d[0] == 0x0002000200020002ULL);
    assert(v1.d[1] == 0x0002000200020002ULL);

    assert(vrep(&v1, &v3, 1, 2) == -1);
    assert(v1.d[0] == 0x0000000100000001ULL);
    assert(v1.d[1] == 0x0000000100000001ULL);

    assert(vrep(&v1, &v3, 1, 3) == -1);
    assert(v1.d[0] == 2);
    assert(v1.d[1] == 2);

    assert(vrep(&v1, &v3, 0x10, 0) == SIGILL);
    assert(vrep(&v1, &v3, 0x101, 0) == SIGILL);
    assert(vrep(&v1, &v3, 0x8, 1) == SIGILL);
    assert(vrep(&v1, &v3, 0x108, 1) == SIGILL);
    assert(vrep(&v1, &v3, 0x4, 2) == SIGILL);
    assert(vrep(&v1, &v3, 0x104, 2) == SIGILL);
    assert(vrep(&v1, &v3, 0x2, 3) == SIGILL);
    assert(vrep(&v1, &v3, 0x102, 3) == SIGILL);

    return EXIT_SUCCESS;
}
