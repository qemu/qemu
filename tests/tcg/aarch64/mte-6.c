#include "mte.h"

void pass(int sig, siginfo_t *info, void *uc)
{
    assert(info->si_code == SEGV_MTESERR);
    exit(0);
}

int main(void)
{
    enable_mte(PR_MTE_TCF_SYNC);

    void *brk = sbrk(16);
    if (brk == (void *)-1) {
        perror("sbrk");
        return 2;
    }

    if (mprotect(brk, 16, PROT_READ | PROT_WRITE | PROT_MTE)) {
        perror("mprotect");
        return 2;
    }

    int *p1, *p2;
    long excl = 1;

    asm("irg %0,%1,%2" : "=r"(p1) : "r"(brk), "r"(excl));
    asm("gmi %0,%1,%0" : "+r"(excl) : "r"(p1));
    asm("irg %0,%1,%2" : "=r"(p2) : "r"(brk), "r"(excl));
    asm("stg %0,[%0]" : : "r"(p1));

    *p1 = 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pass;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    *p2 = 0;

    abort();
}
