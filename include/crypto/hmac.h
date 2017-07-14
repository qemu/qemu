/*
 * QEMU Crypto hmac algorithms
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#ifndef QCRYPTO_HMAC_H
#define QCRYPTO_HMAC_H

#include "qapi-types.h"

typedef struct QCryptoHmac QCryptoHmac;
struct QCryptoHmac {
    QCryptoHashAlgorithm alg;
    void *opaque;
};

/**
 * qcrypto_hmac_supports:
 * @alg: the hmac algorithm
 *
 * Determine if @alg hmac algorithm is supported by
 * the current configured build
 *
 * Returns:
 *  true if the algorithm is supported, false otherwise
 */
bool qcrypto_hmac_supports(QCryptoHashAlgorithm alg);

/**
 * qcrypto_hmac_new:
 * @alg: the hmac algorithm
 * @key: the key bytes
 * @nkey: the length of @key
 * @errp: pointer to a NULL-initialized error object
 *
 * Creates a new hmac object with the algorithm @alg
 *
 * The @key parameter provides the bytes representing
 * the secret key to use. The @nkey parameter specifies
 * the length of @key in bytes
 *
 * Note: must use qcrypto_hmac_free() to release the
 * returned hmac object when no longer required
 *
 * Returns:
 *  a new hmac object, or NULL on error
 */
QCryptoHmac *qcrypto_hmac_new(QCryptoHashAlgorithm alg,
                              const uint8_t *key, size_t nkey,
                              Error **errp);

/**
 * qcrypto_hmac_free:
 * @hmac: the hmac object
 *
 * Release the memory associated with @hmac that was
 * previously allocated by qcrypto_hmac_new()
 */
void qcrypto_hmac_free(QCryptoHmac *hmac);

/**
 * qcrypto_hmac_bytesv:
 * @hmac: the hmac object
 * @iov: the array of memory regions to hmac
 * @niov: the length of @iov
 * @result: pointer to hold output hmac
 * @resultlen: pointer to hold length of @result
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hmac across all the memory regions
 * present in @iov. The @result pointer will be
 * filled with raw bytes representing the computed
 * hmac, which will have length @resultlen. The
 * memory pointer in @result must be released
 * with a call to g_free() when no longer required.
 *
 * Returns:
 *  0 on success, -1 on error
 */
int qcrypto_hmac_bytesv(QCryptoHmac *hmac,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp);

/**
 * qcrypto_hmac_bytes:
 * @hmac: the hmac object
 * @buf: the memory region to hmac
 * @len: the length of @buf
 * @result: pointer to hold output hmac
 * @resultlen: pointer to hold length of @result
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hmac across all the memory region
 * @buf of length @len. The @result pointer will be
 * filled with raw bytes representing the computed
 * hmac, which will have length @resultlen. The
 * memory pointer in @result must be released
 * with a call to g_free() when no longer required.
 *
 * Returns:
 *  0 on success, -1 on error
 */
int qcrypto_hmac_bytes(QCryptoHmac *hmac,
                       const char *buf,
                       size_t len,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp);

/**
 * qcrypto_hmac_digestv:
 * @hmac: the hmac object
 * @iov: the array of memory regions to hmac
 * @niov: the length of @iov
 * @digest: pointer to hold output hmac
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hmac across all the memory regions
 * present in @iov. The @digest pointer will be
 * filled with the printable hex digest of the computed
 * hmac, which will be terminated by '\0'. The
 * memory pointer in @digest must be released
 * with a call to g_free() when no longer required.
 *
 * Returns:
 *  0 on success, -1 on error
 */
int qcrypto_hmac_digestv(QCryptoHmac *hmac,
                         const struct iovec *iov,
                         size_t niov,
                         char **digest,
                         Error **errp);

/**
 * qcrypto_hmac_digest:
 * @hmac: the hmac object
 * @buf: the memory region to hmac
 * @len: the length of @buf
 * @digest: pointer to hold output hmac
 * @errp: pointer to a NULL-initialized error object
 *
 * Computes the hmac across all the memory region
 * @buf of length @len. The @digest pointer will be
 * filled with the printable hex digest of the computed
 * hmac, which will be terminated by '\0'. The
 * memory pointer in @digest must be released
 * with a call to g_free() when no longer required.
 *
 * Returns: 0 on success, -1 on error
 */
int qcrypto_hmac_digest(QCryptoHmac *hmac,
                        const char *buf,
                        size_t len,
                        char **digest,
                        Error **errp);

#endif
