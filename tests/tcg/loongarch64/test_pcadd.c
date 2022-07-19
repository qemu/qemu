#include <assert.h>
#include <inttypes.h>
#include <string.h>

#define TEST_PCADDU(N)                              \
void test_##N(int a)                                \
{                                                   \
    uint64_t rd1 = 0;                               \
    uint64_t rd2 = 0;                               \
    uint64_t rm, rn;                                \
                                                    \
    asm volatile(""#N" %0, 0x104\n\t"               \
                 ""#N" %1, 0x12345\n\t"             \
                 : "=r"(rd1), "=r"(rd2)             \
                 : );                               \
    rm = rd2 - rd1;                                 \
    if (!strcmp(#N, "pcalau12i")) {                 \
        rn = ((0x12345UL - 0x104) << a) & ~0xfff;   \
    } else {                                        \
        rn = ((0x12345UL - 0x104) << a) + 4;        \
    }                                               \
    assert(rm == rn);                               \
}

TEST_PCADDU(pcaddi)
TEST_PCADDU(pcaddu12i)
TEST_PCADDU(pcaddu18i)
TEST_PCADDU(pcalau12i)

int main()
{
    test_pcaddi(2);
    test_pcaddu12i(12);
    test_pcaddu18i(18);
    test_pcalau12i(12);

    return 0;
}
