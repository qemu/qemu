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

int main(void)
{
    assert(octeon_baddu(0x123, 0x0f0) == 0x13);
    assert(octeon_dmul(0x12345678, 0x10) == 0x123456780);
    assert(octeon_dpop(0xf0f0f0f0f0f0f0f0ULL) == 32);
    assert(octeon_seq(0xabc, 0xabc) == 1);
    assert(octeon_seq(0xabc, 0xdef) == 0);
    assert(octeon_sne(0xabc, 0xabc) == 0);
    assert(octeon_sne(0xabc, 0xdef) == 1);

    return 0;
}
