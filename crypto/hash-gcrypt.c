/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2016 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include <gcrypt.h>
#include "qapi/error.h"
#include "crypto/hash.h"


static int qcrypto_hash_alg_map[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = GCRY_MD_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = GCRY_MD_SHA1,
    [QCRYPTO_HASH_ALG_SHA224] = GCRY_MD_SHA224,
    [QCRYPTO_HASH_ALG_SHA256] = GCRY_MD_SHA256,
    [QCRYPTO_HASH_ALG_SHA384] = GCRY_MD_SHA384,
    [QCRYPTO_HASH_ALG_SHA512] = GCRY_MD_SHA512,
    [QCRYPTO_HASH_ALG_RIPEMD160] = GCRY_MD_RMD160,
};

gboolean qcrypto_hash_supports(QCryptoHashAlgorithm alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hash_alg_map) &&
        qcrypto_hash_alg_map[alg] != GCRY_MD_NONE) {
        return true;
    }
    return false;
}


int qcrypto_hash_bytesv(QCryptoHashAlgorithm alg,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp)
{
    int i, ret;
    gcry_md_hd_t md;
    unsigned char *digest;

    if (!qcrypto_hash_supports(alg)) {
        error_setg(errp,
                   "Unknown hash algorithm %d",
                   alg);
        return -1;
    }

    ret = gcry_md_open(&md, qcrypto_hash_alg_map[alg], 0);

    if (ret < 0) {
        error_setg(errp,
                   "Unable to initialize hash algorithm: %s",
                   gcry_strerror(ret));
        return -1;
    }

    for (i = 0; i < niov; i++) {
        gcry_md_write(md, iov[i].iov_base, iov[i].iov_len);
    }

    ret = gcry_md_get_algo_dlen(qcrypto_hash_alg_map[alg]);
    if (ret <= 0) {
        error_setg(errp,
                   "Unable to get hash length: %s",
                   gcry_strerror(ret));
        goto error;
    }
    if (*resultlen == 0) {
        *resultlen = ret;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != ret) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *resultlen, ret);
        goto error;
    }

    digest = gcry_md_read(md, 0);
    if (!digest) {
        error_setg(errp,
                   "No digest produced");
        goto error;
    }
    memcpy(*result, digest, *resultlen);

    gcry_md_close(md);
    return 0;

 error:
    gcry_md_close(md);
    return -1;
}
