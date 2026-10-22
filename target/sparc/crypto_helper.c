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
