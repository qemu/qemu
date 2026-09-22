/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * AES helper functions and mode implementations
 *
 * Authors:
 *   Harald Freudenberger <freude@linux.ibm.com>
 */

#include "qemu/osdep.h"
#include "crypto/aes.h"
#include "crypto/aes-helpers.h"

void AES_xor(const unsigned char *src1, const unsigned char *src2,
             unsigned char *dst)
{
    int i;

    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        dst[i] = src1[i] ^ src2[i];
    }
}

void AES_cbc_encrypt(const unsigned char *in, unsigned char *out,
                     unsigned char *iv, const AES_KEY *key)
{
    unsigned char buf[AES_BLOCK_SIZE];

    /* in xor iv => buf */
    AES_xor(in, iv, buf);
    /* encrypt buf => out */
    AES_encrypt(buf, out, key);
    /* prep iv for next round */
    memcpy(iv, out, AES_BLOCK_SIZE);
}

void AES_cbc_decrypt(const unsigned char *in, unsigned char *out,
                     unsigned char *iv, const AES_KEY *key)
{
    unsigned char buf[AES_BLOCK_SIZE];

    /* decrypt in => buf */
    AES_decrypt(in, buf, key);
    /* buf xor iv => out */
    AES_xor(buf, iv, out);
    /* prep iv for next round */
    memcpy(iv, in, AES_BLOCK_SIZE);
}

void AES_ctr_encrypt(const unsigned char *in, unsigned char *out,
                     const unsigned char *ctr, const AES_KEY *key)
{
    unsigned char buf[AES_BLOCK_SIZE];

    /* encrypt ctr => buf */
    AES_encrypt(ctr, buf, key);
    /* exor input data with encrypted ctr => out */
    AES_xor(in, buf, out);
}

/*
 * Tweak calculation for AES XTS.
 * Multiply tweak by α (x) in GF(2^128) per IEEE 1619-2007. The tweak
 * is a 128-bit little-endian integer (tweak[0]=LSB, tweak[15]=MSB).
 * This implementation has been verified on little and big endian.
 */
void AES_xts_prep_next_tweak(unsigned char *tweak)
{
    unsigned char carry;
    int i;

    carry = tweak[AES_BLOCK_SIZE - 1] >> 7;

    for (i = AES_BLOCK_SIZE - 1; i > 0; i--) {
        tweak[i] = (unsigned char)((tweak[i] << 1) | (tweak[i - 1] >> 7));
    }

    tweak[i] = (unsigned char)(tweak[i] << 1);
    tweak[i] ^= (unsigned char)(0x87 & (unsigned char)(-(unsigned char)carry));
}

void AES_xts_encrypt(const unsigned char *in, unsigned char *out,
                     const unsigned char *tweak, const AES_KEY *key)
{
    unsigned char buf1[AES_BLOCK_SIZE], buf2[AES_BLOCK_SIZE];

    /* in xor tweak => buf1 */
    AES_xor(in, tweak, buf1);
    /* encrypt buf1 => buf2 */
    AES_encrypt(buf1, buf2, key);
    /* buf2 xor tweak => out */
    AES_xor(buf2, tweak, out);
}

void AES_xts_decrypt(const unsigned char *in, unsigned char *out,
                     const unsigned char *tweak, const AES_KEY *key)
{
    unsigned char buf1[AES_BLOCK_SIZE], buf2[AES_BLOCK_SIZE];

    /* in xor tweak => buf1 */
    AES_xor(in, tweak, buf1);
    /* encrypt buf1 => buf2 */
    AES_decrypt(buf1, buf2, key);
    /* buf2 xor tweak => out */
    AES_xor(buf2, tweak, out);
}
