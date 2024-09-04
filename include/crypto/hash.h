/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
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
 */

#ifndef QCRYPTO_HASH_H
#define QCRYPTO_HASH_H

#include "qapi/qapi-types-crypto.h"

#define QCRYPTO_HASH_DIGEST_LEN_MD5       16
#define QCRYPTO_HASH_DIGEST_LEN_SHA1      20
#define QCRYPTO_HASH_DIGEST_LEN_SHA224    28
#define QCRYPTO_HASH_DIGEST_LEN_SHA256    32
#define QCRYPTO_HASH_DIGEST_LEN_SHA384    48
#define QCRYPTO_HASH_DIGEST_LEN_SHA512    64
#define QCRYPTO_HASH_DIGEST_LEN_RIPEMD160 20

/* See also "QCryptoHashAlgo" defined in qapi/crypto.json */

/**
 * qcrypto_hash_supports:
 * @alg: the hash algorithm
 *
 * Determine if @alg hash algorithm is supported by the
 * current configured build.
 *
 * Returns: true if the algorithm is supported, false otherwise
 */
gboolean qcrypto_hash_supports(QCryptoHashAlgo alg);


/**
 * qcrypto_hash_digest_len:
 * @alg: the hash algorithm
 *
 * Determine the size of the hash digest in bytes
 *
 * Returns: the digest length in bytes
 */
size_t qcrypto_hash_digest_len(QCryptoHashAlgo alg);

/**
 * qcrypto_hash_bytesv:
 * @alg: the hash algorithm
 * @iov: the array of memory regions to hash
 * @niov: the length of @iov
 * @result: pointer to hold output hash
 * @resultlen: pointer to hold length of @result
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash across all the memory regions
 * present in @iov. The @result pointer will be
 * filled with raw bytes representing the computed
 * hash, which will have length @resultlen. The
 * memory pointer in @result must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_bytesv(QCryptoHashAlgo alg,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp);

/**
 * qcrypto_hash_bytes:
 * @alg: the hash algorithm
 * @buf: the memory region to hash
 * @len: the length of @buf
 * @result: pointer to hold output hash
 * @resultlen: pointer to hold length of @result
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash across all the memory region
 * @buf of length @len. The @result pointer will be
 * filled with raw bytes representing the computed
 * hash, which will have length @resultlen. The
 * memory pointer in @result must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_bytes(QCryptoHashAlgo alg,
                       const char *buf,
                       size_t len,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp);

/**
 * qcrypto_hash_digestv:
 * @alg: the hash algorithm
 * @iov: the array of memory regions to hash
 * @niov: the length of @iov
 * @digest: pointer to hold output hash
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash across all the memory regions
 * present in @iov. The @digest pointer will be
 * filled with the printable hex digest of the computed
 * hash, which will be terminated by '\0'. The
 * memory pointer in @digest must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_digestv(QCryptoHashAlgo alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **digest,
                         Error **errp);

/**
 * qcrypto_hash_digest:
 * @alg: the hash algorithm
 * @buf: the memory region to hash
 * @len: the length of @buf
 * @digest: pointer to hold output hash
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash across all the memory region
 * @buf of length @len. The @digest pointer will be
 * filled with the printable hex digest of the computed
 * hash, which will be terminated by '\0'. The
 * memory pointer in @digest must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_digest(QCryptoHashAlgo alg,
                        const char *buf,
                        size_t len,
                        char **digest,
                        Error **errp);

/**
 * qcrypto_hash_base64v:
 * @alg: the hash algorithm
 * @iov: the array of memory regions to hash
 * @niov: the length of @iov
 * @base64: pointer to hold output hash
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash across all the memory regions
 * present in @iov. The @base64 pointer will be
 * filled with the base64 encoding of the computed
 * hash, which will be terminated by '\0'. The
 * memory pointer in @base64 must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_base64v(QCryptoHashAlgo alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **base64,
                         Error **errp);

/**
 * qcrypto_hash_base64:
 * @alg: the hash algorithm
 * @buf: the memory region to hash
 * @len: the length of @buf
 * @base64: pointer to hold output hash
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash across all the memory region
 * @buf of length @len. The @base64 pointer will be
 * filled with the base64 encoding of the computed
 * hash, which will be terminated by '\0'. The
 * memory pointer in @base64 must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_base64(QCryptoHashAlgo alg,
                        const char *buf,
                        size_t len,
                        char **base64,
                        Error **errp);

#endif /* QCRYPTO_HASH_H */
