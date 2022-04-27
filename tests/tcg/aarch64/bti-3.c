/*
 * BTI vs PACIASP
 */

#include "bti-crt.inc.c"

static void skip2_sigill(int sig, siginfo_t *info, ucontext_t *uc)
{
    uc->uc_mcontext.pc += 8;
    uc->uc_mcontext.pstate = 1;
}

#define BTYPE_1() \
    asm("mov %0,#1; adr x16, 1f; br x16; 1: hint #25; mov %0,#0" \
        : "=r"(skipped) : : "x16", "x30")

#define BTYPE_2() \
    asm("mov %0,#1; adr x16, 1f; blr x16; 1: hint #25; mov %0,#0" \
        : "=r"(skipped) : : "x16", "x30")

#define BTYPE_3() \
    asm("mov %0,#1; adr x15, 1f; br x15; 1: hint #25; mov %0,#0" \
        : "=r"(skipped) : : "x15", "x30")

#define TEST(WHICH, EXPECT) \
    do { WHICH(); fail += skipped ^ EXPECT; } while (0)

int main()
{
    int fail = 0;
    int skipped;

    /* Signal-like with SA_SIGINFO.  */
    signal_info(SIGILL, skip2_sigill);

    /* With SCTLR_EL1.BT0 set, PACIASP is not compatible with type=3. */
    TEST(BTYPE_1, 0);
    TEST(BTYPE_2, 0);
    TEST(BTYPE_3, 1);

    return fail;
}
