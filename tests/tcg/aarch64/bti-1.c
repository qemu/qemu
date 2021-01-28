/*
 * Branch target identification, basic notskip cases.
 */

#include "bti-crt.inc.c"

static void skip2_sigill(int sig, siginfo_t *info, ucontext_t *uc)
{
    uc->uc_mcontext.pc += 8;
    uc->uc_mcontext.pstate = 1;
}

#define NOP       "nop"
#define BTI_N     "hint #32"
#define BTI_C     "hint #34"
#define BTI_J     "hint #36"
#define BTI_JC    "hint #38"

#define BTYPE_1(DEST) \
    asm("mov %0,#1; adr x16, 1f; br x16; 1: " DEST "; mov %0,#0" \
        : "=r"(skipped) : : "x16")

#define BTYPE_2(DEST) \
    asm("mov %0,#1; adr x16, 1f; blr x16; 1: " DEST "; mov %0,#0" \
        : "=r"(skipped) : : "x16", "x30")

#define BTYPE_3(DEST) \
    asm("mov %0,#1; adr x15, 1f; br x15; 1: " DEST "; mov %0,#0" \
        : "=r"(skipped) : : "x15")

#define TEST(WHICH, DEST, EXPECT) \
    do { WHICH(DEST); fail += skipped ^ EXPECT; } while (0)


int main()
{
    int fail = 0;
    int skipped;

    /* Signal-like with SA_SIGINFO.  */
    signal_info(SIGILL, skip2_sigill);

    TEST(BTYPE_1, NOP, 1);
    TEST(BTYPE_1, BTI_N, 1);
    TEST(BTYPE_1, BTI_C, 0);
    TEST(BTYPE_1, BTI_J, 0);
    TEST(BTYPE_1, BTI_JC, 0);

    TEST(BTYPE_2, NOP, 1);
    TEST(BTYPE_2, BTI_N, 1);
    TEST(BTYPE_2, BTI_C, 0);
    TEST(BTYPE_2, BTI_J, 1);
    TEST(BTYPE_2, BTI_JC, 0);

    TEST(BTYPE_3, NOP, 1);
    TEST(BTYPE_3, BTI_N, 1);
    TEST(BTYPE_3, BTI_C, 1);
    TEST(BTYPE_3, BTI_J, 0);
    TEST(BTYPE_3, BTI_JC, 0);

    return fail;
}
