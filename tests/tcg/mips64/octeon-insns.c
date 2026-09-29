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

static uint64_t octeon_qmac_lo(uint64_t rs, uint64_t rt, uint64_t lo)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        "mtlo %[lo]\n\t"
        "mthi $0\n\t"
        ".word 0x710904d2\n\t" /* qmac.03 $8, $9 */
        "mflo %[rd]\n\t"
        : [rd] "=r" (rd)
        : [rs] "r" (rs), [rt] "r" (rt), [lo] "r" (lo)
        : "$8", "$9");

    return rd;
}

static uint64_t octeon_qmacs_state(uint64_t rs, uint64_t rt, uint64_t lo)
{
    uint64_t hi, rd;

    asm volatile(
        "move $8, %[rs]\n\t"
        "move $9, %[rt]\n\t"
        "mtlo %[lo]\n\t"
        "mthi $0\n\t"
        ".word 0x71090012\n\t" /* qmacs.00 $8, $9 */
        "mfhi %[hi]\n\t"
        "mflo %[rd]\n\t"
        : [hi] "=r" (hi), [rd] "=r" (rd)
        : [rs] "r" (rs), [rt] "r" (rt), [lo] "r" (lo)
        : "$8", "$9");

    return ((hi & 1) << 32) | (rd & 0xffffffff);
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

static uint64_t octeon_cop2_key0_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80104\n\t" /* dmtc2 $8, AES_KEY0 selector */
        ".word 0x482a0104\n\t" /* dmfc2 $10, AES_KEY0 selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_key2_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80106\n\t" /* dmtc2 $8, AES_KEY2 selector */
        ".word 0x482a0106\n\t" /* dmfc2 $10, AES_KEY2 selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_key3_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80107\n\t" /* dmtc2 $8, AES_KEY3 selector */
        ".word 0x482a0107\n\t" /* dmfc2 $10, AES_KEY3 selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_keylength_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80110\n\t" /* dmtc2 $8, AES_KEYLENGTH selector */
        ".word 0x482a0110\n\t" /* dmfc2 $10, AES_KEYLENGTH selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_hsh_dat0_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80040\n\t" /* dmtc2 $8, HSH_DAT0 selector */
        ".word 0x482a0040\n\t" /* dmfc2 $10, HSH_DAT0 selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_crc_len_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a81202\n\t" /* dmtc2 $8, CRC_LEN selector */
        ".word 0x482a0202\n\t" /* dmfc2 $10, CRC_LEN selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_crc_poly_reflect_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a84210\n\t" /* dmtc2 $8, CRC_POLYNOMIAL_REFLECT selector */
        ".word 0x482a0200\n\t" /* dmfc2 $10, CRC_POLYNOMIAL selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_gfm_mul_reflect_write_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80058\n\t" /* dmtc2 $8, GFM_MUL_REFLECT0 selector */
        ".word 0x482a0258\n\t" /* dmfc2 $10, GFM_MUL0 selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_cop2_gfm_mul_reflect_readback(uint64_t value)
{
    uint64_t rd;

    asm volatile(
        "move $8, %[value]\n\t"
        ".word 0x48a80258\n\t" /* dmtc2 $8, GFM_MUL0 selector */
        ".word 0x482a0058\n\t" /* dmfc2 $10, GFM_MUL_REFLECT0 selector */
        "move %[rd], $10\n\t"
        : [rd] "=r" (rd)
        : [value] "r" (value)
        : "$8", "$10");

    return rd;
}

static uint64_t octeon_rdhwr31_non_decreasing(void)
{
    uint64_t first, second;

    asm volatile(
        ".word 0x7c08f83b\n\t" /* rdhwr $8, $31 */
        ".word 0x7c09f83b\n\t" /* rdhwr $9, $31 */
        "move %[first], $8\n\t"
        "move %[second], $9\n\t"
        : [first] "=r" (first), [second] "=r" (second)
        :
        : "$8", "$9");

    return second >= first;
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
    assert(octeon_qmac_lo(0x0003000000000000ULL, 2, 1) == 13);
    assert(octeon_qmacs_state(1, 1, 0x7ffffffe) == 0x17fffffffULL);
    assert(octeon_qmacs_state(0x8000, 0x8000, 0) == 0x17fffffffULL);
    assert(octeon_vmulu(5, 7, 11) == 46);
    assert(octeon_vmm0(5, 13, 7, 11) == 59);
    assert(octeon_vmm0_zeroes_mpl1() == 0);
    assert(octeon_mtp0_zeroes_p1() == 0);
    assert(octeon_cop2_key0_readback(0x1122334455667788ULL) ==
           0x1122334455667788ULL);
    assert(octeon_cop2_key2_readback(0x8877665544332211ULL) ==
           0x8877665544332211ULL);
    assert(octeon_cop2_key3_readback(0x0102030405060708ULL) ==
           0x0102030405060708ULL);
    assert(octeon_cop2_keylength_readback(0xa5) == 1);
    assert(octeon_cop2_hsh_dat0_readback(0x0102030405060708ULL) ==
           0x0102030405060708ULL);
    assert(octeon_cop2_crc_len_readback(0xb5) == 5);
    assert(octeon_cop2_crc_poly_reflect_readback(0x12345678) == 0x482c6a1e);
    assert(octeon_cop2_gfm_mul_reflect_write_readback(
               0x0123456789abcdefULL) == 0xf7b3d591e6a2c480ULL);
    assert(octeon_cop2_gfm_mul_reflect_readback(
               0xfedcba9876543210ULL) == 0x084c2a6e195d3b7fULL);
    assert(octeon_rdhwr31_non_decreasing());

    return 0;
}
