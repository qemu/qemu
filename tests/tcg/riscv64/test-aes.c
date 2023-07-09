/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../multiarch/test-aes-main.c.inc"

bool test_SB_SR(uint8_t *o, const uint8_t *i)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;

    /* aes64es rd, rs1, rs2 = 0011001 rs2 rs1 000 rd 0110011 */
    asm(".insn r 0x33, 0x0, 0x19, %0, %2, %3\n\t"
        ".insn r 0x33, 0x0, 0x19, %1, %3, %2"
        : "=&r"(o8[0]), "=&r"(o8[1]) : "r"(i8[0]), "r"(i8[1]));
    return true;
}

bool test_MC(uint8_t *o, const uint8_t *i)
{
    return false;
}

bool test_SB_SR_MC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;
    const uint64_t *k8 = (const uint64_t *)k;

    /* aesesm rd, rs1, rs2 = 0011011 rs2 rs1 000 rd 0110011 */
    asm(".insn r 0x33, 0x0, 0x1b, %0, %2, %3\n\t"
        ".insn r 0x33, 0x0, 0x1b, %1, %3, %2\n\t"
        "xor %0,%0,%4\n\t"
        "xor %1,%1,%5"
        : "=&r"(o8[0]), "=&r"(o8[1])
        : "r"(i8[0]), "r"(i8[1]), "r"(k8[0]), "r"(k8[1]));
    return true;
}

bool test_ISB_ISR(uint8_t *o, const uint8_t *i)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;

    /* aes64ds rd, rs1, rs2 = 0011101 rs2 rs1 000 rd 0110011 */
    asm(".insn r 0x33, 0x0, 0x1d, %0, %2, %3\n\t"
        ".insn r 0x33, 0x0, 0x1d, %1, %3, %2"
        : "=&r"(o8[0]), "=&r"(o8[1]) : "r"(i8[0]), "r"(i8[1]));
    return true;
}

bool test_IMC(uint8_t *o, const uint8_t *i)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;

    /* aes64im rd, rs1 = 0011000 00000 rs1 001 rd 0010011 */
    asm(".insn r 0x13, 0x1, 0x18, %0, %0, x0\n\t"
        ".insn r 0x13, 0x1, 0x18, %1, %1, x0"
        : "=r"(o8[0]), "=r"(o8[1]) : "0"(i8[0]), "1"(i8[1]));
    return true;
}

bool test_ISB_ISR_AK_IMC(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}

bool test_ISB_ISR_IMC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;
    const uint64_t *k8 = (const uint64_t *)k;

    /* aes64dsm rd, rs1, rs2 = 0011111 rs2 rs1 000 rd 0110011 */
    asm(".insn r 0x33, 0x0, 0x1f, %0, %2, %3\n\t"
        ".insn r 0x33, 0x0, 0x1f, %1, %3, %2\n\t"
        "xor %0,%0,%4\n\t"
        "xor %1,%1,%5"
        : "=&r"(o8[0]), "=&r"(o8[1])
        : "r"(i8[0]), "r"(i8[1]), "r"(k8[0]), "r"(k8[1]));
    return true;
}
