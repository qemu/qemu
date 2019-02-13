/*
 * QEMU Crypto XTS cipher mode
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * This code is originally derived from public domain / WTFPL code in
 * LibTomCrypt crytographic library http://libtom.org. The XTS code
 * was donated by Elliptic Semiconductor Inc (www.ellipticsemi.com)
 * to the LibTom Projects
 *
 */

#ifndef QCRYPTO_XTS_H
#define QCRYPTO_XTS_H


#define XTS_BLOCK_SIZE 16

typedef void xts_cipher_func(const void *ctx,
                             size_t length,
                             uint8_t *dst,
                             const uint8_t *src);

/**
 * xts_decrypt:
 * @datactx: the cipher context for data decryption
 * @tweakctx: the cipher context for tweak decryption
 * @encfunc: the cipher function for encryption
 * @decfunc: the cipher function for decryption
 * @iv: the initialization vector tweak of XTS_BLOCK_SIZE bytes
 * @length: the length of @dst and @src
 * @dst: buffer to hold the decrypted plaintext
 * @src: buffer providing the ciphertext
 *
 * Decrypts @src into @dst
 */
void xts_decrypt(const void *datactx,
                 const void *tweakctx,
                 xts_cipher_func *encfunc,
                 xts_cipher_func *decfunc,
                 uint8_t *iv,
                 size_t length,
                 uint8_t *dst,
                 const uint8_t *src);

/**
 * xts_decrypt:
 * @datactx: the cipher context for data encryption
 * @tweakctx: the cipher context for tweak encryption
 * @encfunc: the cipher function for encryption
 * @decfunc: the cipher function for decryption
 * @iv: the initialization vector tweak of XTS_BLOCK_SIZE bytes
 * @length: the length of @dst and @src
 * @dst: buffer to hold the encrypted ciphertext
 * @src: buffer providing the plaintext
 *
 * Decrypts @src into @dst
 */
void xts_encrypt(const void *datactx,
                 const void *tweakctx,
                 xts_cipher_func *encfunc,
                 xts_cipher_func *decfunc,
                 uint8_t *iv,
                 size_t length,
                 uint8_t *dst,
                 const uint8_t *src);


#endif /* QCRYPTO_XTS_H */
