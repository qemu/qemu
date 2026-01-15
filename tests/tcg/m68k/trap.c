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
#define OUT_CZ      "\n\tscs %0\n\tseq %1" : [c] "=r"(c), [z] "=r"(z) :

int main(int argc, char **argv)
{
    struct sigaction act = {
        .sa_sigaction = sig_handler,
        .sa_flags = SA_SIGINFO
    };
    int t0, t1;
    char bbounds[2] = { 0, 2 };
    short wbounds[2] = { 0, 2 };
    int lbounds[2] = { 0, 2 };
    static int sbounds[2] = { 0, 2 };
    void *intermediate;
    char c, z;

    /*
     * Tests for CMP2, which sets the condition code register just as
     * CHK2 does but doesn't raise out-of-bounds exceptions:
     */
    asm volatile("cmp2.b %2, %3" OUT_CZ "m"(bbounds), "d"(-1));
    assert(c && !z);
    asm volatile("cmp2.b %2, %3" OUT_CZ "m"(bbounds), "d"(0));
    assert(!c && z);
    asm volatile("cmp2.b %2, %3" OUT_CZ "m"(bbounds), "d"(1));
    assert(!c && !z);
    asm volatile("cmp2.b %2, %3" OUT_CZ "m"(bbounds), "d"(2));
    assert(!c && z);
    asm volatile("cmp2.b %2, %3" OUT_CZ "m"(bbounds), "d"(3));
    assert(c && !z);
    asm volatile("cmp2.w %2, %3" OUT_CZ "m"(wbounds), "d"(-1));
    assert(c && !z);
    asm volatile("cmp2.w %2, %3" OUT_CZ "m"(wbounds), "d"(0));
    assert(!c && z);
    asm volatile("cmp2.w %2, %3" OUT_CZ "m"(wbounds), "d"(1));
    assert(!c && !z);
    asm volatile("cmp2.w %2, %3" OUT_CZ "m"(wbounds), "d"(2));
    assert(!c && z);
    asm volatile("cmp2.w %2, %3" OUT_CZ "m"(wbounds), "d"(3));
    assert(c && !z);
    asm volatile("cmp2.l %2, %3" OUT_CZ "m"(lbounds), "d"(-1));
    assert(c && !z);
    asm volatile("cmp2.l %2, %3" OUT_CZ "m"(lbounds), "d"(0));
    assert(!c && z);
    asm volatile("cmp2.l %2, %3" OUT_CZ "m"(lbounds), "d"(1));
    assert(!c && !z);
    asm volatile("cmp2.l %2, %3" OUT_CZ "m"(lbounds), "d"(2));
    assert(!c && z);
    asm volatile("cmp2.l %2, %3" OUT_CZ "m"(lbounds), "d"(3));
    assert(c && !z);

    /*
     * CHK2 shouldn't raise out-of-bounds exceptions, either, when the
     * register value is within bounds:
     */
    asm volatile("chk2.b %2, %3" OUT_CZ "m"(bbounds), "d"(0));
    assert(!c && z);
    asm volatile("chk2.w %2, %3" OUT_CZ "m"(wbounds), "d"(0));
    assert(!c && z);
    asm volatile("chk2.l %2, %3" OUT_CZ "m"(lbounds), "d"(0));
    assert(!c && z);

    /* Address register indirect addressing (without displacement) */
    asm volatile("chk2.l %2, %3" OUT_CZ "Q"(lbounds), "d"(2));
    assert(!c && z);

    /* Absolute long addressing */
    asm volatile("chk2.l %2, %3" OUT_CZ "m"(sbounds), "d"(2));
    assert(!c && z);

    /* Memory indirect preindexed addressing */
    intermediate = (void *)lbounds - 0x0D0D0D0D;
    asm volatile("chk2.l %2@(0xBDBDBDBD,%3:l:4)@(0x0D0D0D0D), %4" OUT_CZ
                 "a"((void *)&intermediate - 0xBDBDBDBD - 0xEEEE * 4),
                 "r"(0xEEEE), "d"(2));
    assert(!c && z);

    sigaction(SIGILL, &act, NULL);
    sigaction(SIGTRAP, &act, NULL);
    sigaction(SIGFPE, &act, NULL);

    expect_sig = SIGFPE;
    expect_si_code = FPE_INTOVF;
    asm volatile(FMT2_STR("0:\tchk %0, %1") : : "d"(0), "d"(-1), FMT_INS);
    CHECK_SIG;

    /*
     * The extension word here should be counted when computing the
     * address of the next instruction in the exception stack frame
     */
    asm volatile(FMT2_STR("0:\tchk %0, %1")
                 : : "m"(lbounds), "d"(-1), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("0:\tchk2.b %0, %1")
                 : : "m"(bbounds), "d"(3), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("0:\tchk2.w %0, %1")
                 : : "m"(wbounds), "d"(3), FMT_INS);
    CHECK_SIG;

    asm volatile(FMT2_STR("0:\tchk2.l %0, %1")
                 : : "m"(lbounds), "d"(3), FMT_INS);
    CHECK_SIG;

    /* Absolute long addressing */
    asm volatile(FMT2_STR("0:\tchk2.l %0, %1")
                 : : "m"(sbounds), "d"(3), FMT_INS);
    CHECK_SIG;

    /*
     * Memory indirect preindexed addressing; also, the six extension
     * words here should be counted when computing the address of the
     * next instruction in the exception stack frame
     */
    asm volatile(FMT2_STR("0:\tchk2.l %0@(0xBDBDBDBD,%1:l:4)@(0x0D0D0D0D), %2")
                 : : "a"((void *)&intermediate - 0xBDBDBDBD - 0xEEEE * 4),
                 "r"(0xEEEE), "d"(3), FMT_INS);
    CHECK_SIG;

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
