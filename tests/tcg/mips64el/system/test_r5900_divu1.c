/*
 * Test R5900-specific DIVU1.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

struct quotient_remainder { uint32_t quotient, remainder; };

static struct quotient_remainder divu1(uint32_t rs, uint32_t rt)
{
    uint32_t lo, hi;

    __asm__ __volatile__ (
            "    divu1 $0, %2, %3\n"
            "    mflo1 %0\n"
            "    mfhi1 %1\n"
            : "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt));

    assert(rs / rt == lo);
    assert(rs % rt == hi);

    return (struct quotient_remainder) { .quotient = lo, .remainder = hi };
}

static void verify_divu1(uint32_t rs, uint32_t rt,
                         uint32_t expected_quotient,
                         uint32_t expected_remainder)
{
    struct quotient_remainder qr = divu1(rs, rt);

    assert(qr.quotient == expected_quotient);
    assert(qr.remainder == expected_remainder);
}

int main()
{
    verify_divu1(0, 1, 0, 0);
    verify_divu1(1, 1, 1, 0);
    verify_divu1(1, 2, 0, 1);
    verify_divu1(17, 19, 0, 17);
    verify_divu1(19, 17, 1, 2);
    verify_divu1(77773, 101, 770, 3);

    return 0;
}
