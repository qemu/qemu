/*
 * Test m68k trap addresses.
 */

#define _GNU_SOURCE 1
#include <signal.h>
#include <assert.h>
#include <limits.h>

static int expect_sig;
static int expect_si_code;
static void *expect_si_addr;
static greg_t expect_mc_pc;
static volatile int got_signal;

static void sig_handler(int sig, siginfo_t *si, void *puc)
{
    ucontext_t *uc = puc;
    mcontext_t *mc = &uc->uc_mcontext;

    assert(sig == expect_sig);
    assert(si->si_code == expect_si_code);
    assert(si->si_addr == expect_si_addr);
    assert(mc->gregs[R_PC] == expect_mc_pc);

    got_signal = 1;
}

#define FMT_INS     [ad] "a"(&expect_si_addr), [pc] "a"(&expect_mc_pc)
#define FMT0_STR(S) \
    "move.l #1f, (%[ad])\n\tmove.l #1f, (%[pc])\n" S "\n1:\n"
#define FMT2_STR(S) \
    "move.l #0f, (%[ad])\n\tmove.l #1f, (%[pc])\n" S "\n1:\n"

#define CHECK_SIG   do { assert(got_signal); got_signal = 0; } while (0)

int main(int argc, char **argv)
{
    struct sigaction act = {
        .sa_sigaction = sig_handler,
        .sa_flags = SA_SIGINFO
    };
    int t0, t1;

    sigaction(SIGILL, &act, NULL);
    sigaction(SIGTRAP, &act, NULL);
    sigaction(SIGFPE, &act, NULL);

    expect_sig = SIGFPE;
    expect_si_code = FPE_INTOVF;
    asm volatile(FMT2_STR("0:\tchk %0, %1") : : "d"(0), "d"(-1), FMT_INS);
    CHECK_SIG;

#if 0
    /* FIXME: chk2 not correctly translated. */
    int bounds[2] = { 0, 1 };
    asm volatile(FMT2_STR("0:\tchk2.l %0, %1")
                 : : "m"(bounds), "d"(2), FMT_INS);
    CHECK_SIG;
#endif

    asm volatile(FMT2_STR("cmp.l %0, %1\n0:\ttrapv")
                 : : "d"(INT_MIN), "d"(1), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("cmp.l %0, %0\n0:\ttrapeq")
                 : : "d"(0), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("cmp.l %0, %0\n0:\ttrapeq.w #0x1234")
                 : : "d"(0), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("cmp.l %0, %0\n0:\ttrapeq.l #0x12345678")
                 : : "d"(0), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("fcmp.x %0, %0\n0:\tftrapeq")
                 : : "f"(0.0L), FMT_INS);
    CHECK_SIG;

    expect_si_code = FPE_INTDIV;

    asm volatile(FMT2_STR("0:\tdivs.w %1, %0")
                 : "=d"(t0) : "d"(0), "0"(1), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("0:\tdivsl.l %2, %1:%0")
                 : "=d"(t0), "=d"(t1) : "d"(0), "0"(1), FMT_INS);
    CHECK_SIG;

    expect_sig = SIGILL;
    expect_si_code = ILL_ILLTRP;
    asm volatile(FMT0_STR("trap #1") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #2") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #3") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #4") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #5") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #6") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #7") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #8") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #9") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #10") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #11") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #12") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #13") : : FMT_INS);
    CHECK_SIG;
    asm volatile(FMT0_STR("trap #14") : : FMT_INS);
    CHECK_SIG;

    expect_sig = SIGTRAP;
    expect_si_code = TRAP_BRKPT;
    asm volatile(FMT0_STR("trap #15") : : FMT_INS);
    CHECK_SIG;

    return 0;
}
