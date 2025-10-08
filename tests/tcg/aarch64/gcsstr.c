/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gcs.h"

/*
 * A single garbage store to the gcs stack.
 * The asm inside must be unique, so disallow inlining.
 */
void __attribute__((noinline))
test_gcsstr(void)
{
    register uint64_t *ptr __asm__("x0") = gcspr();
    /* GCSSTR x1, x0 */
    __asm__("inst_gcsstr: .inst 0xd91f1c01" : : "r"(--ptr));
}

static void test_sigsegv(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;
    uint64_t inst_gcsstr;

    __asm__("adr %0, inst_gcsstr" : "=r"(inst_gcsstr));
    assert(uc->uc_mcontext.pc == inst_gcsstr);
    assert(info->si_code == SEGV_CPERR);
    /* TODO: Dig for ESR and verify syndrome. */
    exit(0);
}

int main()
{
    struct sigaction sa = {
        .sa_sigaction = test_sigsegv,
        .sa_flags = SA_SIGINFO,
    };

    /* Enable GCSSTR and test the store succeeds. */
    enable_gcs(PR_SHADOW_STACK_WRITE);
    test_gcsstr();

    /* Disable GCSSTR and test the resulting sigsegv. */
    enable_gcs(0);
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }
    test_gcsstr();
    abort();
}
