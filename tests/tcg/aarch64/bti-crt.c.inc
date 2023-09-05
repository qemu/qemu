/*
 * Minimal user-environment for testing BTI.
 *
 * Normal libc is not (yet) built with BTI support enabled,
 * and so could generate a BTI TRAP before ever reaching main.
 */

#include <stdlib.h>
#include <signal.h>
#include <ucontext.h>
#include <asm/unistd.h>

int main(void);

void _start(void)
{
    exit(main());
}

void exit(int ret)
{
    register int x0 __asm__("x0") = ret;
    register int x8 __asm__("x8") = __NR_exit;

    asm volatile("svc #0" : : "r"(x0), "r"(x8));
    __builtin_unreachable();
}

/*
 * Irritatingly, the user API struct sigaction does not match the
 * kernel API struct sigaction.  So for simplicity, isolate the
 * kernel ABI here, and make this act like signal.
 */
void signal_info(int sig, void (*fn)(int, siginfo_t *, ucontext_t *))
{
    struct kernel_sigaction {
        void (*handler)(int, siginfo_t *, ucontext_t *);
        unsigned long flags;
        unsigned long restorer;
        unsigned long mask;
    } sa = { fn, SA_SIGINFO, 0, 0 };

    register int x0 __asm__("x0") = sig;
    register void *x1 __asm__("x1") = &sa;
    register void *x2 __asm__("x2") = 0;
    register int x3 __asm__("x3") = sizeof(unsigned long);
    register int x8 __asm__("x8") = __NR_rt_sigaction;

    asm volatile("svc #0"
                 : : "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
}
