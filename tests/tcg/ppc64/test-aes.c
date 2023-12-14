/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../multiarch/test-aes-main.c.inc"

#undef BIG_ENDIAN
#define BIG_ENDIAN  (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

static unsigned char bswap_le[16] __attribute__((aligned(16))) = {
    8,9,10,11,12,13,14,15,
    0,1,2,3,4,5,6,7
};

bool test_SB_SR(uint8_t *o, const uint8_t *i)
{
    /* vcipherlast also adds round key, so supply zero. */
    if (BIG_ENDIAN) {
        asm("lxvd2x 32,0,%1\n\t"
            "vspltisb 1,0\n\t"
            "vcipherlast 0,0,1\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i) : "memory", "v0", "v1");
    } else {
        asm("lxvd2x 32,0,%1\n\t"
            "lxvd2x 34,0,%2\n\t"
            "vspltisb 1,0\n\t"
            "vperm 0,0,0,2\n\t"
            "vcipherlast 0,0,1\n\t"
            "vperm 0,0,0,2\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i), "r"(bswap_le) : "memory", "v0", "v1", "v2");
    }
    return true;
}

bool test_MC(uint8_t *o, const uint8_t *i)
{
    return false;
}

bool test_SB_SR_MC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    if (BIG_ENDIAN) {
        asm("lxvd2x 32,0,%1\n\t"
            "lxvd2x 33,0,%2\n\t"
            "vcipher 0,0,1\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i), "r"(k) : "memory", "v0", "v1");
    } else {
        asm("lxvd2x 32,0,%1\n\t"
            "lxvd2x 33,0,%2\n\t"
            "lxvd2x 34,0,%3\n\t"
            "vperm 0,0,0,2\n\t"
            "vperm 1,1,1,2\n\t"
            "vcipher 0,0,1\n\t"
            "vperm 0,0,0,2\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i), "r"(k), "r"(bswap_le)
              : "memory", "v0", "v1", "v2");
    }
    return true;
}

bool test_ISB_ISR(uint8_t *o, const uint8_t *i)
{
    /* vcipherlast also adds round key, so supply zero. */
    if (BIG_ENDIAN) {
        asm("lxvd2x 32,0,%1\n\t"
            "vspltisb 1,0\n\t"
            "vncipherlast 0,0,1\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i) : "memory", "v0", "v1");
    } else {
        asm("lxvd2x 32,0,%1\n\t"
            "lxvd2x 34,0,%2\n\t"
            "vspltisb 1,0\n\t"
            "vperm 0,0,0,2\n\t"
            "vncipherlast 0,0,1\n\t"
            "vperm 0,0,0,2\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i), "r"(bswap_le) : "memory", "v0", "v1", "v2");
    }
    return true;
}

bool test_IMC(uint8_t *o, const uint8_t *i)
{
    return false;
}

bool test_ISB_ISR_AK_IMC(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    if (BIG_ENDIAN) {
        asm("lxvd2x 32,0,%1\n\t"
            "lxvd2x 33,0,%2\n\t"
            "vncipher 0,0,1\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i), "r"(k) : "memory", "v0", "v1");
    } else {
        asm("lxvd2x 32,0,%1\n\t"
            "lxvd2x 33,0,%2\n\t"
            "lxvd2x 34,0,%3\n\t"
            "vperm 0,0,0,2\n\t"
            "vperm 1,1,1,2\n\t"
            "vncipher 0,0,1\n\t"
            "vperm 0,0,0,2\n\t"
            "stxvd2x 32,0,%0"
            : : "r"(o), "r"(i), "r"(k), "r"(bswap_le)
              : "memory", "v0", "v1", "v2");
    }
    return true;
}

bool test_ISB_ISR_IMC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}
