/*
 * Test R5900-specific DIV1.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

struct quotient_remainder { int32_t quotient, remainder; };

static struct quotient_remainder div1(int32_t rs, int32_t rt)
{
    int32_t lo, hi;

    __asm__ __volatile__ (
            "    div1 $0, %2, %3\n"
            "    mflo1 %0\n"
            "    mfhi1 %1\n"
            : "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt));

    assert(rs / rt == lo);
    assert(rs % rt == hi);

    return (struct quotient_remainder) { .quotient = lo, .remainder = hi };
}

static void verify_div1(int32_t rs, int32_t rt,
                        int32_t expected_quotient,
                        int32_t expected_remainder)
{
    struct quotient_remainder qr = div1(rs, rt);

    assert(qr.quotient == expected_quotient);
    assert(qr.remainder == expected_remainder);
}

static void verify_div1_negations(int32_t rs, int32_t rt,
                                  int32_t expected_quotient,
                                  int32_t expected_remainder)
{
    verify_div1(rs, rt, expected_quotient, expected_remainder);
    verify_div1(rs, -rt, -expected_quotient, expected_remainder);
    verify_div1(-rs, rt, -expected_quotient, -expected_remainder);
    verify_div1(-rs, -rt, expected_quotient, -expected_remainder);
}

int main()
{
    verify_div1_negations(0, 1, 0, 0);
    verify_div1_negations(1, 1, 1, 0);
    verify_div1_negations(1, 2, 0, 1);
    verify_div1_negations(17, 19, 0, 17);
    verify_div1_negations(19, 17, 1, 2);
    verify_div1_negations(77773, 101, 770, 3);

    verify_div1(-0x80000000,  1, -0x80000000, 0);

    /*
     * Supplementary explanation from the Toshiba TX System RISC TX79 Core
     * Architecture manual, A-38 and B-7, https://wiki.qemu.org/File:C790.pdf
     *
     * Normally, when 0x80000000 (-2147483648) the signed minimum value is
     * divided by 0xFFFFFFFF (-1), the operation will result in an overflow.
     * However, in this instruction an overflow exception doesn't occur and
     * the result will be as follows:
     *
     * Quotient is 0x80000000 (-2147483648), and remainder is 0x00000000 (0).
     */
    verify_div1(-0x80000000, -1, -0x80000000, 0);

    return 0;
}
