/* Test PC misalignment exception */

#ifdef __thumb__
#error "This test must be compiled for ARM"
#endif

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

static void *expected;

static void sigbus(int sig, siginfo_t *info, void *vuc)
{
    assert(info->si_code == BUS_ADRALN);
    assert(info->si_addr == expected);
    exit(EXIT_SUCCESS);
}

int main()
{
    void *tmp;

    struct sigaction sa = {
        .sa_sigaction = sigbus,
        .sa_flags = SA_SIGINFO
    };

    if (sigaction(SIGBUS, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    asm volatile("adr %0, 1f + 2\n\t"
                 "str %0, %1\n\t"
                 "bx  %0\n"
                 "1:"
                 : "=&r"(tmp), "=m"(expected));

    /*
     * From v8, it is CONSTRAINED UNPREDICTABLE whether BXWritePC aligns
     * the address or not.  If so, we can legitimately fall through.
     */
    return EXIT_SUCCESS;
}
