/*
 * Test Octeon-specific user-mode instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdint.h>

static uint64_t octeon_baddu(uint64_t rs, uint64_t rt)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        ".word 0x71095028\n\t" /* baddu $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [rs] "r" (rs), [rt] "r" (rt)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_dmul(uint64_t rs, uint64_t rt)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        ".word 0x71095003\n\t" /* dmul $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [rs] "r" (rs), [rt] "r" (rt)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_dpop(uint64_t rs)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        ".word 0x7100502d\n\t" /* dpop $10, $8 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [rs] "r" (rs)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_seq(uint64_t rs, uint64_t rt)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        ".word 0x7109502a\n\t" /* seq $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [rs] "r" (rs), [rt] "r" (rt)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_sne(uint64_t rs, uint64_t rt)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        ".word 0x7109502b\n\t" /* sne $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [rs] "r" (rs), [rt] "r" (rt)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_vmulu(uint64_t mpl0, uint64_t rs, uint64_t rt)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[mpl0]\n\t"
        "move $9, $0\n\t"
        ".word 0x71090008\n\t" /* mtm0 $8, $9 */
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        ".word 0x7109500f\n\t" /* vmulu $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [mpl0] "r" (mpl0), [rs] "r" (rs), [rt] "r" (rt)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_vmm0(uint64_t mpl0, uint64_t p0,
                            uint64_t rs, uint64_t rt)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[mpl0]\n\t"
        "move $9, $0\n\t"
        ".word 0x71090008\n\t" /* mtm0 $8, $9 */
        "move $8, %[p0]\n\t"
        "move $9, $0\n\t"
        ".word 0x71090009\n\t" /* mtp0 $8, $9 */
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        ".word 0x71095010\n\t" /* vmm0 $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [mpl0] "r" (mpl0), [p0] "r" (p0),
          [rs] "r" (rs), [rt] "r" (rt)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_vmm0_zeroes_mpl1(void)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[mpl0]\n\t"
        "move $9, $0\n\t"
        ".word 0x71090008\n\t" /* mtm0 $8, $9 */
        "move $8, %[mpl1]\n\t"
        "move $9, $0\n\t"
        ".word 0x7109000c\n\t" /* mtm1 $8, $9 */
        "move $8, %[vmm0_rs]\n\t"
        "move $9, $0\n\t"
        ".word 0x71095010\n\t" /* vmm0 $10, $8, $9 */
        "move $8, %[vmulu_rs]\n\t"
        "move $9, $0\n\t"
        ".word 0x7109500f\n\t" /* vmulu $10, $8, $9 */
        "move $8, $0\n\t"
        "move $9, $0\n\t"
        ".word 0x7109500f\n\t" /* vmulu $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [mpl0] "r" (1ULL), [mpl1] "r" (1ULL),
          [vmm0_rs] "r" (2ULL), [vmulu_rs] "r" (1ULL)
        : "$8", "$9", "$10");

    return rd;
}

static uint64_t octeon_mtp0_zeroes_p1(void)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[mpl0]\n\t"
        "move $9, $0\n\t"
        ".word 0x71090008\n\t" /* mtm0 $8, $9 */
        "move $8, %[p1]\n\t"
        "move $9, $0\n\t"
        ".word 0x7109000a\n\t" /* mtp1 $8, $9 */
        "move $8, $0\n\t"
        "move $9, $0\n\t"
        ".word 0x71090009\n\t" /* mtp0 $8, $9 */
        "move $8, $0\n\t"
        "move $9, $0\n\t"
        ".word 0x7109500f\n\t" /* vmulu $10, $8, $9 */
        "move $8, $0\n\t"
        "move $9, $0\n\t"
        ".word 0x7109500f\n\t" /* vmulu $10, $8, $9 */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [mpl0] "r" (0ULL), [p1] "r" (1ULL)
        : "$8", "$9", "$10");

    return rd;
}

int main(void)
{
    assert(octeon_baddu(0x123, 0x0f0) == 0x13);
    assert(octeon_dmul(0x12345678, 0x10) == 0x123456780);
    assert(octeon_dpop(0xf0f0f0f0f0f0f0f0ULL) == 32);
    assert(octeon_seq(0xabc, 0xabc) == 1);
    assert(octeon_seq(0xabc, 0xdef) == 0);
    assert(octeon_sne(0xabc, 0xabc) == 0);
    assert(octeon_sne(0xabc, 0xdef) == 1);
    assert(octeon_vmulu(5, 7, 11) == 46);
    assert(octeon_vmm0(5, 13, 7, 11) == 59);
    assert(octeon_vmm0_zeroes_mpl1() == 0);
    assert(octeon_mtp0_zeroes_p1() == 0);

    return 0;
}
