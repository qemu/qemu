#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>

#define CRF_LT  (1 << 3)
#define CRF_GT  (1 << 2)
#define CRF_EQ  (1 << 1)
#define CRF_SO  (1 << 0)
#define UNDEF   0

#ifdef __has_builtin
#if !__has_builtin(__builtin_bcdsub)
#define NO_BUILTIN_BCDSUB
#endif
#endif

#ifdef NO_BUILTIN_BCDSUB
#define BCDSUB(T, A, B, PS) \
    ".long 4 << 26 | (" #T ") << 21 | (" #A ") << 16 | (" #B ") << 11"  \
    " | 1 << 10 | (" #PS ") << 9 | 65\n\t"
#else
#define BCDSUB(T, A, B, PS) "bcdsub. " #T ", " #A ", " #B ", " #PS "\n\t"
#endif

#define TEST(AH, AL, BH, BL, PS, TH, TL, CR6)                                  \
    do {                                                                       \
        int cr = 0;                                                            \
        uint64_t th, tl;                                                       \
        /*                                                                     \
         * Use GPR pairs to load the VSR values and place the resulting VSR and\
         * CR6 in th, tl, and cr. Note that we avoid newer instructions (e.g., \
         * mtvsrdd/mfvsrld) so we can run this test on POWER8 machines.        \
         */                                                                    \
        asm ("mtvsrd 32, %3\n\t"                                               \
             "mtvsrd 33, %4\n\t"                                               \
             "xxmrghd 32, 32, 33\n\t"                                          \
             "mtvsrd 33, %5\n\t"                                               \
             "mtvsrd 34, %6\n\t"                                               \
             "xxmrghd 33, 33, 34\n\t"                                          \
             BCDSUB(0, 0, 1, PS)                                               \
             "mfocrf %0, 0b10\n\t"                                             \
             "mfvsrd %1, 32\n\t"                                               \
             "xxswapd 32, 32\n\t"                                              \
             "mfvsrd %2, 32\n\t"                                               \
             : "=r" (cr), "=r" (th), "=r" (tl)                                 \
             : "r" (AH), "r" (AL), "r" (BH), "r" (BL)                          \
             : "v0", "v1", "v2");                                              \
        if (TH != UNDEF || TL != UNDEF) {                                      \
            assert(tl == TL);                                                  \
            assert(th == TH);                                                  \
        }                                                                      \
        assert((cr >> 4) == CR6);                                              \
    } while (0)

/*
 * Unbounded result is equal to zero:
 *   sign = (PS) ? 0b1111 : 0b1100
 *   CR6 = 0b0010
 */
void test_bcdsub_eq(void)
{
    /* maximum positive BCD value */
    TEST(0x9999999999999999, 0x999999999999999c,
         0x9999999999999999, 0x999999999999999c,
         0, 0x0, 0xc, CRF_EQ);
    TEST(0x9999999999999999, 0x999999999999999c,
         0x9999999999999999, 0x999999999999999c,
         1, 0x0, 0xf, CRF_EQ);
}

/*
 * Unbounded result is greater than zero:
 *   sign = (PS) ? 0b1111 : 0b1100
 *   CR6 = (overflow) ? 0b0101 : 0b0100
 */
void test_bcdsub_gt(void)
{
    /* maximum positive and negative one BCD values */
    TEST(0x9999999999999999, 0x999999999999999c, 0x0, 0x1d, 0,
         0x0, 0xc, (CRF_GT | CRF_SO));
    TEST(0x9999999999999999, 0x999999999999999c, 0x0, 0x1d, 1,
         0x0, 0xf, (CRF_GT | CRF_SO));

    TEST(0x9999999999999999, 0x999999999999998c, 0x0, 0x1d, 0,
         0x9999999999999999, 0x999999999999999c, CRF_GT);
    TEST(0x9999999999999999, 0x999999999999998c, 0x0, 0x1d, 1,
         0x9999999999999999, 0x999999999999999f, CRF_GT);
}

/*
 * Unbounded result is less than zero:
 *   sign = 0b1101
 *   CR6 = (overflow) ? 0b1001 : 0b1000
 */
void test_bcdsub_lt(void)
{
    /* positive zero and positive one BCD values */
    TEST(0x0, 0xc, 0x0, 0x1c, 0, 0x0, 0x1d, CRF_LT);
    TEST(0x0, 0xc, 0x0, 0x1c, 1, 0x0, 0x1d, CRF_LT);

    /* maximum negative and positive one BCD values */
    TEST(0x9999999999999999, 0x999999999999999d, 0x0, 0x1c, 0,
         0x0, 0xd, (CRF_LT | CRF_SO));
    TEST(0x9999999999999999, 0x999999999999999d, 0x0, 0x1c, 1,
         0x0, 0xd, (CRF_LT | CRF_SO));
}

void test_bcdsub_invalid(void)
{
    TEST(0x0, 0x1c, 0x0, 0xf00, 0, UNDEF, UNDEF, CRF_SO);
    TEST(0x0, 0x1c, 0x0, 0xf00, 1, UNDEF, UNDEF, CRF_SO);

    TEST(0x0, 0xf00, 0x0, 0x1c, 0, UNDEF, UNDEF, CRF_SO);
    TEST(0x0, 0xf00, 0x0, 0x1c, 1, UNDEF, UNDEF, CRF_SO);

    TEST(0x0, 0xbad, 0x0, 0xf00, 0, UNDEF, UNDEF, CRF_SO);
    TEST(0x0, 0xbad, 0x0, 0xf00, 1, UNDEF, UNDEF, CRF_SO);
}

int main(void)
{
    struct sigaction action;

    action.sa_handler = _exit;
    sigaction(SIGABRT, &action, NULL);

    test_bcdsub_eq();
    test_bcdsub_gt();
    test_bcdsub_lt();
    test_bcdsub_invalid();

    return 0;
}
