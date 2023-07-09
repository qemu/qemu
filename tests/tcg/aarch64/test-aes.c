/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../multiarch/test-aes-main.c.inc"

bool test_SB_SR(uint8_t *o, const uint8_t *i)
{
    /* aese also adds round key, so supply zero. */
    asm("ld1 { v0.16b }, [%1]\n\t"
        "movi v1.16b, #0\n\t"
        "aese v0.16b, v1.16b\n\t"
        "st1 { v0.16b }, [%0]"
        : : "r"(o), "r"(i) : "v0", "v1", "memory");
    return true;
}

bool test_MC(uint8_t *o, const uint8_t *i)
{
    asm("ld1 { v0.16b }, [%1]\n\t"
        "aesmc v0.16b, v0.16b\n\t"
        "st1 { v0.16b }, [%0]"
        : : "r"(o), "r"(i) : "v0", "memory");
    return true;
}

bool test_SB_SR_MC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}

bool test_ISB_ISR(uint8_t *o, const uint8_t *i)
{
    /* aesd also adds round key, so supply zero. */
    asm("ld1 { v0.16b }, [%1]\n\t"
        "movi v1.16b, #0\n\t"
        "aesd v0.16b, v1.16b\n\t"
        "st1 { v0.16b }, [%0]"
        : : "r"(o), "r"(i) : "v0", "v1", "memory");
    return true;
}

bool test_IMC(uint8_t *o, const uint8_t *i)
{
    asm("ld1 { v0.16b }, [%1]\n\t"
        "aesimc v0.16b, v0.16b\n\t"
        "st1 { v0.16b }, [%0]"
        : : "r"(o), "r"(i) : "v0", "memory");
    return true;
}

bool test_ISB_ISR_AK_IMC(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}

bool test_ISB_ISR_IMC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}
