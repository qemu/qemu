/*
 * SPARC crypto instructions for QEMU
 *
 * Based on Oracle SPARC Architecture 2011
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/helper-proto.h"
#include "crypto/aes-round.h"
#include "crypto/aes.h"
#include "qemu/bswap.h"
#include "qemu/crc32c.h"

/*
 * SPARC four-operand AES round (Section 7.3).
 * s2:s3 holds the state; half selects output columns 0-1 or 2-3, and
 * key supplies their round key. dec selects decryption; last omits
 * MixColumns/InvMixColumns. Return the selected 64-bit half.
 *
 * Note all 64-bit values (state and key) hold AES bytes in big-endian
 * order, i.e. the most significant byte is the first state byte.
 */
static uint64_t aes_round(uint64_t key, uint64_t s2, uint64_t s3,
                          int half, bool dec, bool last)
{
    AESState st, rk, r;
    const unsigned first = HOST_BIG_ENDIAN ? 0 : 1;
    const unsigned out = first ^ half;

    st.d[first] = s2;       /* columns 0-1 */
    st.d[first ^ 1] = s3;   /* columns 2-3 */
    rk.d[out] = key;
    rk.d[out ^ 1] = 0;

    if (dec) {
        if (last) {
            aesdec_ISB_ISR_AK(&r, &st, &rk, true);
        } else {
            aesdec_ISB_ISR_AK_IMC(&r, &st, &rk, true);
        }
    } else {
        if (last) {
            aesenc_SB_SR_AK(&r, &st, &rk, true);
        } else {
            aesenc_SB_SR_MC_AK(&r, &st, &rk, true);
        }
    }

    return r.d[out];
}

uint64_t helper_aes_eround01(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, false, false);
}

uint64_t helper_aes_eround23(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, false, false);
}

uint64_t helper_aes_dround01(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, true, false);
}

uint64_t helper_aes_dround23(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, true, false);
}

uint64_t helper_aes_eround01_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, false, true);
}

uint64_t helper_aes_eround23_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, false, true);
}

uint64_t helper_aes_dround01_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, true, true);
}

uint64_t helper_aes_dround23_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, true, true);
}

/* Combine the transformed word with the two words in rs1. */
static uint64_t aes_kexpand(uint64_t rs1, uint32_t t)
{
    uint32_t hi = (rs1 >> 32) ^ t;
    uint32_t lo = (uint32_t)rs1 ^ hi;

    return deposit64(lo, 32, 32, hi);
}

/* Apply the AES S-box to each byte, preserving byte positions. */
static uint32_t aes_subword(uint32_t x)
{
    return ((uint32_t)AES_sbox[x >> 24] << 24) |
           ((uint32_t)AES_sbox[(x >> 16) & 0xff] << 16) |
           ((uint32_t)AES_sbox[(x >> 8) & 0xff] << 8) |
           (uint32_t)AES_sbox[x & 0xff];
}

/*
 * AES Cryptographic Operations 4-operand (Section 7.3)
 * AES key expansion with RotWord, SubWord, and RCON.
 */
uint64_t helper_aes_kexpand1(uint64_t rs1, uint64_t rs2, uint32_t imm)
{
    static const uint8_t rcon[10] = {
        0x01, 0x02, 0x04, 0x08, 0x10,
        0x20, 0x40, 0x80, 0x1b, 0x36,
    };
    uint32_t t = rol32((uint32_t)rs2, 8);

    t = aes_subword(t);

    if (imm < ARRAY_SIZE(rcon)) {
        t ^= (uint32_t)rcon[imm] << 24;
    }

    return aes_kexpand(rs1, t);
}

/*
 * AES Cryptographic Operations 3-operand (Section 7.4)
 * AES key expansion with SubWord, without RotWord or RCON.
 */
uint64_t helper_aes_kexpand0(uint64_t rs1, uint64_t rs2)
{
    return aes_kexpand(rs1, aes_subword(rs2));
}

/*
 * AES Cryptographic Operations 3-operand (Section 7.4)
 * AES key expansion without SubWord, RotWord, or RCON.
 */
uint64_t helper_aes_kexpand2(uint64_t rs1, uint64_t rs2)
{
    return aes_kexpand(rs1, (uint32_t)rs2);
}

/*
 * CRC32C Operation 3-operand (Section 7.19)
 * Accumulate CRC32C over the eight bytes of rs2, most significant first.
 */
uint64_t helper_crc32c(uint64_t rs1, uint64_t rs2)
{
    uint8_t buf[8];
    /* IV and result are byte-swapped in the register. */
    uint32_t crc = bswap32((uint32_t)rs1);

    stq_be_p(buf, rs2);

    /* Undo crc32c()'s final inversion for the instruction's raw result. */
    crc = crc32c(crc, buf, sizeof(buf)) ^ UINT32_MAX;

    /* Result lives in the low 32 bits; upper half reads as zero. */
    return (uint64_t)bswap32(crc);
}
