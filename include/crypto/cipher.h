/*
 * QEMU Crypto cipher algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QCRYPTO_CIPHER_H
#define QCRYPTO_CIPHER_H

#include "qapi-types.h"

typedef struct QCryptoCipher QCryptoCipher;

/* See also "QCryptoCipherAlgorithm" and "QCryptoCipherMode"
 * enums defined in qapi/crypto.json */

/**
 * QCryptoCipher:
 *
 * The QCryptoCipher object provides a way to perform encryption
 * and decryption of data, with a standard API, regardless of the
 * algorithm used. It further isolates the calling code from the
 * details of the specific underlying implementation, whether
 * built-in, libgcrypt or nettle.
 *
 * Each QCryptoCipher object is capable of performing both
 * encryption and decryption, and can operate in a number
 * or modes including ECB, CBC.
 *
 * <example>
 *   <title>Encrypting data with AES-128 in CBC mode</title>
 *   <programlisting>
 * QCryptoCipher *cipher;
 * uint8_t key = ....;
 * size_t keylen = 16;
 * uint8_t iv = ....;
 *
 * if (!qcrypto_cipher_supports(QCRYPTO_CIPHER_ALG_AES_128)) {
 *    error_report(errp, "Feature <blah> requires AES cipher support");
 *    return -1;
 * }
 *
 * cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALG_AES_128,
 *                             QCRYPTO_CIPHER_MODE_CBC,
 *                             key, keylen,
 *                             errp);
 * if (!cipher) {
 *    return -1;
 * }
 *
 * if (qcrypto_cipher_set_iv(cipher, iv, keylen, errp) < 0) {
 *    return -1;
 * }
 *
 * if (qcrypto_cipher_encrypt(cipher, rawdata, encdata, datalen, errp) < 0) {
 *    return -1;
 * }
 *
 * qcrypto_cipher_free(cipher);
 *   </programlisting>
 * </example>
 *
 */

struct QCryptoCipher {
    QCryptoCipherAlgorithm alg;
    QCryptoCipherMode mode;
    void *opaque;
};

/**
 * qcrypto_cipher_supports:
 * @alg: the cipher algorithm
 * @mode: the cipher mode
 *
 * Determine if @alg cipher algorithm in @mode is supported by the
 * current configured build
 *
 * Returns: true if the algorithm is supported, false otherwise
 */
bool qcrypto_cipher_supports(QCryptoCipherAlgorithm alg,
                             QCryptoCipherMode mode);

/**
 * qcrypto_cipher_get_block_len:
 * @alg: the cipher algorithm
 *
 * Get the required data block size in bytes. When
 * encrypting data, it must be a multiple of the
 * block size.
 *
 * Returns: the block size in bytes
 */
size_t qcrypto_cipher_get_block_len(QCryptoCipherAlgorithm alg);


/**
 * qcrypto_cipher_get_key_len:
 * @alg: the cipher algorithm
 *
 * Get the required key size in bytes.
 *
 * Returns: the key size in bytes
 */
size_t qcrypto_cipher_get_key_len(QCryptoCipherAlgorithm alg);


/**
 * qcrypto_cipher_get_iv_len:
 * @alg: the cipher algorithm
 * @mode: the cipher mode
 *
 * Get the required initialization vector size
 * in bytes, if one is required.
 *
 * Returns: the IV size in bytes, or 0 if no IV is permitted
 */
size_t qcrypto_cipher_get_iv_len(QCryptoCipherAlgorithm alg,
                                 QCryptoCipherMode mode);


/**
 * qcrypto_cipher_new:
 * @alg: the cipher algorithm
 * @mode: the cipher usage mode
 * @key: the private key bytes
 * @nkey: the length of @key
 * @errp: pointer to a NULL-initialized error object
 *
 * Creates a new cipher object for encrypting/decrypting
 * data with the algorithm @alg in the usage mode @mode.
 *
 * The @key parameter provides the bytes representing
 * the encryption/decryption key to use. The @nkey parameter
 * specifies the length of @key in bytes. Each algorithm has
 * one or more valid key lengths, and it is an error to provide
 * a key of the incorrect length.
 *
 * The returned cipher object must be released with
 * qcrypto_cipher_free() when no longer required
 *
 * Returns: a new cipher object, or NULL on error
 */
QCryptoCipher *qcrypto_cipher_new(QCryptoCipherAlgorithm alg,
                                  QCryptoCipherMode mode,
                                  const uint8_t *key, size_t nkey,
                                  Error **errp);

/**
 * qcrypto_cipher_free:
 * @cipher: the cipher object
 *
 * Release the memory associated with @cipher that
 * was previously allocated by qcrypto_cipher_new()
 */
void qcrypto_cipher_free(QCryptoCipher *cipher);

/**
 * qcrypto_cipher_encrypt:
 * @cipher: the cipher object
 * @in: buffer holding the plain text input data
 * @out: buffer to fill with the cipher text output data
 * @len: the length of @in and @out buffers
 * @errp: pointer to a NULL-initialized error object
 *
 * Encrypts the plain text stored in @in, filling
 * @out with the resulting ciphered text. Both the
 * @in and @out buffers must have the same size,
 * given by @len.
 *
 * Returns: 0 on success, or -1 on error
 */
int qcrypto_cipher_encrypt(QCryptoCipher *cipher,
                           const void *in,
                           void *out,
                           size_t len,
                           Error **errp);


/**
 * qcrypto_cipher_decrypt:
 * @cipher: the cipher object
 * @in: buffer holding the cipher text input data
 * @out: buffer to fill with the plain text output data
 * @len: the length of @in and @out buffers
 * @errp: pointer to a NULL-initialized error object
 *
 * Decrypts the cipher text stored in @in, filling
 * @out with the resulting plain text. Both the
 * @in and @out buffers must have the same size,
 * given by @len.
 *
 * Returns: 0 on success, or -1 on error
 */
int qcrypto_cipher_decrypt(QCryptoCipher *cipher,
                           const void *in,
                           void *out,
                           size_t len,
                           Error **errp);

/**
 * qcrypto_cipher_setiv:
 * @cipher: the cipher object
 * @iv: the initialization vector or counter (CTR mode) bytes
 * @niv: the length of @iv
 * @errpr: pointer to a NULL-initialized error object
 *
 * If the @cipher object is setup to use a mode that requires
 * initialization vectors or counter, this sets the @niv
 * bytes. The @iv data should have the same length as the
 * cipher key used when originally constructing the cipher
 * object. It is an error to set an initialization vector
 * or counter if the cipher mode does not require one.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_cipher_setiv(QCryptoCipher *cipher,
                         const uint8_t *iv, size_t niv,
                         Error **errp);

#endif /* QCRYPTO_CIPHER_H */
