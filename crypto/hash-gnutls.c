/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2021 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include <gnutls/crypto.h>
#include "qapi/error.h"
#include "crypto/hash.h"
#include "hashpriv.h"


static int qcrypto_hash_alg_map[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = GNUTLS_DIG_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = GNUTLS_DIG_SHA1,
    [QCRYPTO_HASH_ALG_SHA224] = GNUTLS_DIG_SHA224,
    [QCRYPTO_HASH_ALG_SHA256] = GNUTLS_DIG_SHA256,
    [QCRYPTO_HASH_ALG_SHA384] = GNUTLS_DIG_SHA384,
    [QCRYPTO_HASH_ALG_SHA512] = GNUTLS_DIG_SHA512,
    [QCRYPTO_HASH_ALG_RIPEMD160] = GNUTLS_DIG_RMD160,
};

gboolean qcrypto_hash_supports(QCryptoHashAlgorithm alg)
{
    size_t i;
    const gnutls_digest_algorithm_t *algs;
    if (alg >= G_N_ELEMENTS(qcrypto_hash_alg_map) ||
        qcrypto_hash_alg_map[alg] == GNUTLS_DIG_UNKNOWN) {
        return false;
    }
    algs = gnutls_digest_list();
    for (i = 0; algs[i] != GNUTLS_DIG_UNKNOWN; i++) {
        if (algs[i] == qcrypto_hash_alg_map[alg]) {
            return true;
        }
    }
    return false;
}


static int
qcrypto_gnutls_hash_bytesv(QCryptoHashAlgorithm alg,
                           const struct iovec *iov,
                           size_t niov,
                           uint8_t **result,
                           size_t *resultlen,
                           Error **errp)
{
    int i, ret;
    gnutls_hash_hd_t hash;

    if (!qcrypto_hash_supports(alg)) {
        error_setg(errp,
                   "Unknown hash algorithm %d",
                   alg);
        return -1;
    }

    ret = gnutls_hash_get_len(qcrypto_hash_alg_map[alg]);
    if (*resultlen == 0) {
        *resultlen = ret;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != ret) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *resultlen, ret);
        return -1;
    }

    ret = gnutls_hash_init(&hash, qcrypto_hash_alg_map[alg]);
    if (ret < 0) {
        error_setg(errp,
                   "Unable to initialize hash algorithm: %s",
                   gnutls_strerror(ret));
        return -1;
    }

    for (i = 0; i < niov; i++) {
        gnutls_hash(hash, iov[i].iov_base, iov[i].iov_len);
    }

    gnutls_hash_deinit(hash, *result);
    return 0;
}


QCryptoHashDriver qcrypto_hash_lib_driver = {
    .hash_bytesv = qcrypto_gnutls_hash_bytesv,
};
