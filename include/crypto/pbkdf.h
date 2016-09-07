/*
 * QEMU Crypto PBKDF support (Password-Based Key Derivation Function)
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#ifndef QCRYPTO_PBKDF_H
#define QCRYPTO_PBKDF_H

#include "crypto/hash.h"

/**
 * This module provides an interface to the PBKDF2 algorithm
 *
 *   https://en.wikipedia.org/wiki/PBKDF2
 *
 * <example>
 *   <title>Generating an AES encryption key from a user password</title>
 *   <programlisting>
 * #include "crypto/cipher.h"
 * #include "crypto/random.h"
 * #include "crypto/pbkdf.h"
 *
 * ....
 *
 * char *password = "a-typical-awful-user-password";
 * size_t nkey = qcrypto_cipher_get_key_len(QCRYPTO_CIPHER_ALG_AES_128);
 * uint8_t *salt = g_new0(uint8_t, nkey);
 * uint8_t *key = g_new0(uint8_t, nkey);
 * int iterations;
 * QCryptoCipher *cipher;
 *
 * if (qcrypto_random_bytes(salt, nkey, errp) < 0) {
 *     g_free(key);
 *     g_free(salt);
 *     return -1;
 * }
 *
 * iterations = qcrypto_pbkdf2_count_iters(QCRYPTO_HASH_ALG_SHA256,
 *                                         (const uint8_t *)password,
 *                                         strlen(password),
 *                                         salt, nkey, errp);
 * if (iterations < 0) {
 *     g_free(key);
 *     g_free(salt);
 *     return -1;
 * }
 *
 * if (qcrypto_pbkdf2(QCRYPTO_HASH_ALG_SHA256,
 *                    (const uint8_t *)password, strlen(password),
 *                    salt, nkey, iterations, key, nkey, errp) < 0) {
 *     g_free(key);
 *     g_free(salt);
 *     return -1;
 * }
 *
 * g_free(salt);
 *
 * cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALG_AES_128,
 *                             QCRYPTO_CIPHER_MODE_ECB,
 *                             key, nkey, errp);
 * g_free(key);
 *
 * ....encrypt some data...
 *
 * qcrypto_cipher_free(cipher);
 *   </programlisting>
 * </example>
 *
 */

/**
 * qcrypto_pbkdf2_supports:
 * @hash: the hash algorithm
 *
 * Determine if the current build supports the PBKDF2 algorithm
 * in combination with the hash @hash.
 *
 * Returns true if supported, false otherwise
 */
bool qcrypto_pbkdf2_supports(QCryptoHashAlgorithm hash);


/**
 * qcrypto_pbkdf2:
 * @hash: the hash algorithm to use
 * @key: the user password / key
 * @nkey: the length of @key in bytes
 * @salt: a random salt
 * @nsalt: length of @salt in bytes
 * @iterations: the number of iterations to compute
 * @out: pointer to pre-allocated buffer to hold output
 * @nout: length of @out in bytes
 * @errp: pointer to a NULL-initialized error object
 *
 * Apply the PBKDF2 algorithm to derive an encryption
 * key from a user password provided in @key. The
 * @salt parameter is used to perturb the algorithm.
 * The @iterations count determines how many times
 * the hashing process is run, which influences how
 * hard it is to crack the key. The number of @iterations
 * should be large enough such that the algorithm takes
 * 1 second or longer to derive a key. The derived key
 * will be stored in the preallocated buffer @out.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_pbkdf2(QCryptoHashAlgorithm hash,
                   const uint8_t *key, size_t nkey,
                   const uint8_t *salt, size_t nsalt,
                   uint64_t iterations,
                   uint8_t *out, size_t nout,
                   Error **errp);

/**
 * qcrypto_pbkdf2_count_iters:
 * @hash: the hash algorithm to use
 * @key: the user password / key
 * @nkey: the length of @key in bytes
 * @salt: a random salt
 * @nsalt: length of @salt in bytes
 * @nout: size of desired derived key
 * @errp: pointer to a NULL-initialized error object
 *
 * Time the PBKDF2 algorithm to determine how many
 * iterations are required to derive an encryption
 * key from a user password provided in @key in 1
 * second of compute time. The result of this can
 * be used as a the @iterations parameter of a later
 * call to qcrypto_pbkdf2(). The value of @nout should
 * match that value that will later be provided with
 * a call to qcrypto_pbkdf2().
 *
 * Returns: number of iterations in 1 second, -1 on error
 */
uint64_t qcrypto_pbkdf2_count_iters(QCryptoHashAlgorithm hash,
                                    const uint8_t *key, size_t nkey,
                                    const uint8_t *salt, size_t nsalt,
                                    size_t nout,
                                    Error **errp);

#endif /* QCRYPTO_PBKDF_H */
