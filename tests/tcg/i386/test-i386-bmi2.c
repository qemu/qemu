/* See if various BMI2 instructions give expected results */
#include <assert.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    uint64_t ehlo = 0x202020204f4c4845ull;
    uint64_t mask = 0xa080800302020001ull;
    uint32_t result32;

#ifdef __x86_64
    uint64_t result64;

    /* 64 bits */
    asm volatile ("pextq   %2, %1, %0" : "=r"(result64) : "r"(ehlo), "m"(mask));
    assert(result64 == 133);

    asm volatile ("pdepq   %2, %1, %0" : "=r"(result64) : "r"(result64), "m"(mask));
    assert(result64 == (ehlo & mask));

    asm volatile ("pextq   %2, %1, %0" : "=r"(result64) : "r"(-1ull), "m"(mask));
    assert(result64 == 511); /* mask has 9 bits set */

    asm volatile ("pdepq   %2, %1, %0" : "=r"(result64) : "r"(-1ull), "m"(mask));
    assert(result64 == mask);
#endif

    /* 32 bits */
    asm volatile ("pextl   %2, %k1, %k0" : "=r"(result32) : "r"((uint32_t) ehlo), "m"(mask));
    assert(result32 == 5);

    asm volatile ("pdepl   %2, %k1, %k0" : "=r"(result32) : "r"(result32), "m"(mask));
    assert(result32 == (uint32_t)(ehlo & mask));

    asm volatile ("pextl   %2, %k1, %k0" : "=r"(result32) : "r"(-1ull), "m"(mask));
    assert(result32 == 7); /* mask has 3 bits set */

    asm volatile ("pdepl   %2, %k1, %k0" : "=r"(result32) : "r"(-1ull), "m"(mask));
    assert(result32 == (uint32_t)mask);

    return 0;
}

