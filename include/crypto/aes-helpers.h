/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * AES helper functions and modes
 */

#ifndef QEMU_AES_HELPERS_H
#define QEMU_AES_HELPERS_H

/**
 * AES_xor:
 * @src1: first source buffer of AES_BLOCK_SIZE bytes
 * @src2: second source buffer of AES_BLOCK_SIZE bytes
 * @dst: destination buffer of AES_BLOCK_SIZE bytes
 *
 * Bitwise XOR operation between two AES blocks.
 */
void AES_xor(const unsigned char *src1, const unsigned char *src2,
             unsigned char *dst);

/**
 * AES_cbc_encrypt:
 * @in: input plaintext block of AES_BLOCK_SIZE bytes
 * @out: output ciphertext block of AES_BLOCK_SIZE bytes
 * @iv: IV, updated after processing for chaining
 * @key: AES key
 *
 * Single block AES encrypt in CBC (Cipher Block Chaining) mode.
 * The input block is XORed with the IV, then encrypted with AES.
 * IV is updated at the end and is prepared for the next invocation.
 */
void AES_cbc_encrypt(const unsigned char *in, unsigned char *out,
                     unsigned char *iv, const AES_KEY *key);

/**
 * AES_cbc_decrypt:
 * @in: input ciphertext block of AES_BLOCK_SIZE bytes
 * @out: output plaintext block of AES_BLOCK_SIZE bytes
 * @iv: initialization vector, updated to input block for chaining
 * @key: AES key
 *
 * Single block AES decrypt in CBC (Cipher Block Chaining) mode.
 * The input block is decrypted, then XORed with the IV.
 * IV is updated at the end and is prepared for the next invocation.
 */
void AES_cbc_decrypt(const unsigned char *in, unsigned char *out,
                     unsigned char *iv, const AES_KEY *key);

/**
 * AES_ctr_encrypt:
 * @in: input data block of AES_BLOCK_SIZE bytes
 * @out: output data block of AES_BLOCK_SIZE bytes
 * @ctr: counter value of AES_BLOCK_SIZE bytes
 * @key: AES key
 *
 * Single block AES encrypt/decrypt in CTR (Counter) mode.
 * The counter block is encrypted, then XORed with the
 * input data block.
 * In CTR mode encrypt and decrypt are identical operations.
 * Note that the caller is responsible for incrementing the
 * counter block.
 */
void  AES_ctr_encrypt(const unsigned char *in, unsigned char *out,
                      const unsigned char *ctr, const AES_KEY *key);

/**
 * AES_xts_prep_next_tweak:
 * @tweak: pointer to tweak value to be updated (16 bytes buffer
 *         containing a 128 bit little endian integer)
 *
 * Tweak calculation for AES XTS.
 * Prepares the next tweak value for AES-XTS mode by multiplying
 * the current tweak by α (x) in GF(2^128) according to IEEE 1619-2007.
 */
void AES_xts_prep_next_tweak(unsigned char *tweak);

/**
 * AES_xts_encrypt:
 * @in: input plaintext block of AES_BLOCK_SIZE bytes
 * @out: output ciphertext block of AES_BLOCK_SIZE bytes
 * @tweak: tweak value (16 bytes)
 * @key: AES key
 *
 * Single block AES encrypt in XTS mode.
 * The input is XORed with the tweak, encrypted, then XORed with
 * the tweak again to produce the output.
 * Note that the caller is responsible for managing the tweak value.
 * Use AES_xts_prep_next_tweak() to advance the tweak for the next block.
 */
void AES_xts_encrypt(const unsigned char *in, unsigned char *out,
                     const unsigned char *tweak, const AES_KEY *key);

/**
 * AES_xts_decrypt:
 * @in: input ciphertext block of AES_BLOCK_SIZE bytes
 * @out: output plaintext block of AES_BLOCK_SIZE bytes
 * @tweak: tweak value (16 bytes)
 * @key: AES key
 *
 * Single block AES decrypt in XTS mode.
 * The input is XORed with the tweak, decrypted, then XORed with
 * the tweak again to produce the output.
 * Note that the caller is responsible for managing the tweak value.
 * Use AES_xts_prep_next_tweak() to advance the tweak for the next block.
 */
void AES_xts_decrypt(const unsigned char *in, unsigned char *out,
                     const unsigned char *tweak, const AES_KEY *key);

#endif
