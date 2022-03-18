#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static void *expected;

void sigsegv(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;

    assert(info->si_addr == expected);
    uc->uc_mcontext.pc += 4;
}

int main()
{
    struct sigaction sa = {
        .sa_sigaction = sigsegv,
        .sa_flags = SA_SIGINFO
    };

    void *page;
    long ofs;

    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    page = mmap(0, getpagesize(), PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    ofs = 0x124;
    expected = page + ofs;

    asm("ptrue p0.d, vl1\n\t"
        "dup z0.d, %0\n\t"
        "ldnt1h {z1.d}, p0/z, [z0.d, %1]\n\t"
        "dup z1.d, %1\n\t"
        "ldnt1h {z0.d}, p0/z, [z1.d, %0]"
        : : "r"(page), "r"(ofs) : "v0", "v1");

    return EXIT_SUCCESS;
}
