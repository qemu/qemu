/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
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
#define QCRYPTO_HASH_DIGEST_LEN_SM3       32

/* See also "QCryptoHashAlgo" defined in qapi/crypto.json */

typedef struct QCryptoHash QCryptoHash;
struct QCryptoHash {
    QCryptoHashAlgo alg;
    void *opaque;
    void *driver;
};

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
 * present in @iov.
 *
 * If @result_len is set to a non-zero value by the caller, then
 * @result must hold a pointer that is @result_len in size, and
 * @result_len match the size of the hash output. The digest will
 * be written into @result.
 *
 * If @result_len is set to zero, then this function will allocate
 * a buffer to hold the hash output digest, storing a pointer to
 * the buffer in @result, and setting @result_len to its size.
 * The memory referenced in @result must be released with a call
 * to g_free() when no longer required by the caller.
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
 * @buf of length @len.
 *
 * If @result_len is set to a non-zero value by the caller, then
 * @result must hold a pointer that is @result_len in size, and
 * @result_len match the size of the hash output. The digest will
 * be written into @result.
 *
 * If @result_len is set to zero, then this function will allocate
 * a buffer to hold the hash output digest, storing a pointer to
 * the buffer in @result, and setting @result_len to its size.
 * The memory referenced in @result must be released with a call
 * to g_free() when no longer required by the caller.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_bytes(QCryptoHashAlgo alg,
                       const void *buf,
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
 * qcrypto_hash_updatev:
 * @hash: hash object from qcrypto_hash_new
 * @iov: the array of memory regions to hash
 * @niov: the length of @iov
 * @errp: pointer to a NULL-initialized error object
 *
 * Updates the given hash object with all the memory regions
 * present in @iov.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_updatev(QCryptoHash *hash,
                         const struct iovec *iov,
                         size_t niov,
                         Error **errp);
/**
 * qcrypto_hash_update:
 * @hash: hash object from qcrypto_hash_new
 * @buf: the memory region to hash
 * @len: the length of @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Updates the given hash object with the data from
 * the given buffer.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_update(QCryptoHash *hash,
                        const void *buf,
                        size_t len,
                        Error **errp);

/**
 * qcrypto_hash_finalize_digest:
 * @hash: the hash object to finalize
 * @digest: pointer to hold output hash
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash from the given hash object. Hash object
 * is expected to have its data updated from the qcrypto_hash_update function.
 * The @digest pointer will be filled with the printable hex digest of the
 * computed hash, which will be terminated by '\0'. The memory pointer
 * in @digest must be released with a call to g_free() when
 * no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_finalize_digest(QCryptoHash *hash,
                                 char **digest,
                                 Error **errp);

/**
 * qcrypto_hash_finalize_base64:
 * @hash_ctx: hash object to finalize
 * @base64: pointer to store the hash result in
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash from the given hash object. Hash object
 * is expected to have it's data updated from the qcrypto_hash_update function.
 * The @base64 pointer will be filled with the base64 encoding of the computed
 * hash, which will be terminated by '\0'. The memory pointer in @base64
 * must be released with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_finalize_base64(QCryptoHash *hash,
                                 char **base64,
                                 Error **errp);

/**
 * qcrypto_hash_finalize_bytes:
 * @hash_ctx: hash object to finalize
 * @result: pointer to store the hash result in
 * @result_len: Pointer to store the length of the result in
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hash from the given hash object. Hash object
 * is expected to have it's data updated from the qcrypto_hash_update function.
 *
 * If @result_len is set to a non-zero value by the caller, then
 * @result must hold a pointer that is @result_len in size, and
 * @result_len match the size of the hash output. The digest will
 * be written into @result.
 *
 * If @result_len is set to zero, then this function will allocate
 * a buffer to hold the hash output digest, storing a pointer to
 * the buffer in @result, and setting @result_len to its size.
 * The memory referenced in @result must be released with a call
 * to g_free() when no longer required by the caller.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hash_finalize_bytes(QCryptoHash *hash,
                                uint8_t **result,
                                size_t *result_len,
                                Error **errp);

/**
 * qcrypto_hash_new:
 * @alg: the hash algorithm
 * @errp: pointer to a NULL-initialized error object
 *
 * Creates a new hashing context for the chosen algorithm for
 * usage with qcrypto_hash_update.
 *
 * Returns: New hash object with the given algorithm, or NULL on error.
 */
QCryptoHash *qcrypto_hash_new(QCryptoHashAlgo alg, Error **errp);

/**
 * qcrypto_hash_free:
 * @hash: hash object to free
 *
 * Frees a hashing context for the chosen algorithm.
 */
void qcrypto_hash_free(QCryptoHash *hash);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoHash, qcrypto_hash_free)

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
                        const void *buf,
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
                        const void *buf,
                        size_t len,
                        char **base64,
                        Error **errp);

#endif /* QCRYPTO_HASH_H */
