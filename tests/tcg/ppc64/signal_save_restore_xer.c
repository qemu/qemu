#include <assert.h>
#include <stdint.h>
#include <signal.h>
#include <sys/user.h>

#define XER_SO   (1 << 31)
#define XER_OV   (1 << 30)
#define XER_CA   (1 << 29)
#define XER_OV32 (1 << 19)
#define XER_CA32 (1 << 18)

uint64_t saved;

void sigtrap_handler(int sig, siginfo_t *si, void *ucontext)
{
    ucontext_t *uc = ucontext;
    uc->uc_mcontext.regs->nip += 4;
    saved = uc->uc_mcontext.regs->xer;
    uc->uc_mcontext.regs->xer |= XER_OV | XER_OV32;
}

int main(void)
{
    uint64_t initial = XER_CA | XER_CA32, restored;
    struct sigaction sa = {
        .sa_sigaction = sigtrap_handler,
        .sa_flags = SA_SIGINFO
    };

    sigaction(SIGTRAP, &sa, NULL);

    asm("mtspr 1, %1\n\t"
        "trap\n\t"
        "mfspr %0, 1\n\t"
        : "=r" (restored)
        : "r" (initial));

    assert(saved == initial);
    assert(restored == (XER_OV | XER_OV32 | XER_CA | XER_CA32));

    return 0;
}
