#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#define WORD_A 0xAAAAAAAAUL
#define WORD_B 0xBBBBBBBBUL
#define WORD_C 0xCCCCCCCCUL
#define WORD_D 0xDDDDDDDDUL

#define DWORD_HI (WORD_A << 32 | WORD_B)
#define DWORD_LO (WORD_C << 32 | WORD_D)

#define TEST(HI, LO, UIM, RES) \
    do {                                                        \
        union {                                                 \
            uint64_t u;                                         \
            double f;                                           \
        } h = { .u = HI }, l = { .u = LO };                     \
        /*                                                      \
         * Use a pair of FPRs to load the VSR avoiding insns    \
         * newer than xxswapd.                                  \
         */                                                     \
        asm("xxmrghd 32, %0, %1\n\t"                            \
            "xxspltw 32, 32, %2\n\t"                            \
            "xxmrghd %0, 32, %0\n\t"                            \
            "xxswapd 32, 32\n\t"                                \
            "xxmrghd %1, 32, %1\n\t"                            \
            : "+f" (h.f), "+f" (l.f)                            \
            : "i" (UIM)                                         \
            : "v0");                                            \
        printf("xxspltw(0x%016" PRIx64 "%016" PRIx64 ", %d) ="  \
               " %016" PRIx64 "%016" PRIx64 "\n", HI, LO, UIM,  \
               h.u, l.u);                                       \
        assert(h.u == (RES));                                   \
        assert(l.u == (RES));                                   \
    } while (0)

int main(void)
{
    TEST(DWORD_HI, DWORD_LO, 0, WORD_A << 32 | WORD_A);
    TEST(DWORD_HI, DWORD_LO, 1, WORD_B << 32 | WORD_B);
    TEST(DWORD_HI, DWORD_LO, 2, WORD_C << 32 | WORD_C);
    TEST(DWORD_HI, DWORD_LO, 3, WORD_D << 32 | WORD_D);
    return 0;
}
