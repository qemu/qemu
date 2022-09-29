#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#define TEST(INSN, B_HI, B_LO, T_HI, T_LO) \
    do {                                                                \
        uint64_t th, tl, bh = B_HI, bl = B_LO;                          \
        asm("mtvsrd 32, %2\n\t"                                         \
            "mtvsrd 33, %3\n\t"                                         \
            "xxmrghd 32, 32, 33\n\t"                                    \
            INSN " 32, 32\n\t"                                          \
            "mfvsrd %0, 32\n\t"                                         \
            "xxswapd 32, 32\n\t"                                        \
            "mfvsrd %1, 32\n\t"                                         \
            : "=r" (th), "=r" (tl)                                      \
            : "r" (bh), "r" (bl)                                        \
            : "v0", "v1");                                              \
        printf(INSN "(0x%016" PRIx64 "%016" PRIx64 ") = 0x%016" PRIx64  \
               "%016" PRIx64 "\n", bh, bl, th, tl);                     \
        assert(th == T_HI && tl == T_LO);                               \
    } while (0)

int main(void)
{
    /* SNaN shouldn't be silenced */
    TEST("xscvspdpn", 0x7fbfffff00000000ULL, 0x0, 0x7ff7ffffe0000000ULL, 0x0);
    TEST("xscvdpspn", 0x7ff7ffffffffffffULL, 0x0, 0x7fbfffff7fbfffffULL, 0x0);

    /*
     * SNaN inputs having no significant bits in the upper 23 bits of the
     * signifcand will return Infinity as the result.
     */
    TEST("xscvdpspn", 0x7ff000001fffffffULL, 0x0, 0x7f8000007f800000ULL, 0x0);

    return 0;
}
