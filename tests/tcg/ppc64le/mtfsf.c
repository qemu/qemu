#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <sys/prctl.h>

#define FPSCR_VE     7  /* Floating-point invalid operation exception enable */
#define FPSCR_VXSOFT 10 /* Floating-point invalid operation exception (soft) */
#define FPSCR_FI     17 /* Floating-point fraction inexact                   */

#define FP_VE           (1ull << FPSCR_VE)
#define FP_VXSOFT       (1ull << FPSCR_VXSOFT)
#define FP_FI           (1ull << FPSCR_FI)

void sigfpe_handler(int sig, siginfo_t *si, void *ucontext)
{
    if (si->si_code == FPE_FLTINV) {
        exit(0);
    }
    exit(1);
}

int main(void)
{
    union {
        double d;
        long long ll;
    } fpscr;

    struct sigaction sa = {
        .sa_sigaction = sigfpe_handler,
        .sa_flags = SA_SIGINFO
    };

    /*
     * Enable the MSR bits F0 and F1 to enable exceptions.
     * This shouldn't be needed in linux-user as these bits are enabled by
     * default, but this allows to execute either in a VM or a real machine
     * to compare the behaviors.
     */
    prctl(PR_SET_FPEXC, PR_FP_EXC_PRECISE);

    /* First test if the FI bit is being set correctly */
    fpscr.ll = FP_FI;
    __builtin_mtfsf(0b11111111, fpscr.d);
    fpscr.d = __builtin_mffs();
    assert((fpscr.ll & FP_FI) != 0);

    /* Then test if the deferred exception is being called correctly */
    sigaction(SIGFPE, &sa, NULL);

    /*
     * Although the VXSOFT exception has been chosen, based on test in a Power9
     * any combination of exception bit + its enabling bit should work.
     * But if a different exception is chosen si_code check should
     * change accordingly.
     */
    fpscr.ll = FP_VE | FP_VXSOFT;
    __builtin_mtfsf(0b11111111, fpscr.d);

    return 1;
}
