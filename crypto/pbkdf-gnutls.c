/*
 * QEMU Crypto PBKDF support (Password-Based Key Derivation Function)
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
#include "crypto/pbkdf.h"

bool qcrypto_pbkdf2_supports(QCryptoHashAlgo hash)
{
    switch (hash) {
    case QCRYPTO_HASH_ALGO_MD5:
    case QCRYPTO_HASH_ALGO_SHA1:
    case QCRYPTO_HASH_ALGO_SHA224:
    case QCRYPTO_HASH_ALGO_SHA256:
    case QCRYPTO_HASH_ALGO_SHA384:
    case QCRYPTO_HASH_ALGO_SHA512:
    case QCRYPTO_HASH_ALGO_RIPEMD160:
        return qcrypto_hash_supports(hash);
    default:
        return false;
    }
}

int qcrypto_pbkdf2(QCryptoHashAlgo hash,
                   const uint8_t *key, size_t nkey,
                   const uint8_t *salt, size_t nsalt,
                   uint64_t iterations,
                   uint8_t *out, size_t nout,
                   Error **errp)
{
    static const int hash_map[QCRYPTO_HASH_ALGO__MAX] = {
        [QCRYPTO_HASH_ALGO_MD5] = GNUTLS_DIG_MD5,
        [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_DIG_SHA1,
        [QCRYPTO_HASH_ALGO_SHA224] = GNUTLS_DIG_SHA224,
        [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_DIG_SHA256,
        [QCRYPTO_HASH_ALGO_SHA384] = GNUTLS_DIG_SHA384,
        [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_DIG_SHA512,
        [QCRYPTO_HASH_ALGO_RIPEMD160] = GNUTLS_DIG_RMD160,
    };
    int ret;
    const gnutls_datum_t gkey = { (unsigned char *)key, nkey };
    const gnutls_datum_t gsalt = { (unsigned char *)salt, nsalt };

    if (iterations > ULONG_MAX) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu must be less than %lu",
                         (long long unsigned)iterations, ULONG_MAX);
        return -1;
    }

    if (hash >= G_N_ELEMENTS(hash_map) ||
        hash_map[hash] == GNUTLS_DIG_UNKNOWN) {
        error_setg_errno(errp, ENOSYS,
                         "PBKDF does not support hash algorithm %s",
                         QCryptoHashAlgo_str(hash));
        return -1;
    }

    ret = gnutls_pbkdf2(hash_map[hash],
                        &gkey,
                        &gsalt,
                        iterations,
                        out,
                        nout);
    if (ret != 0) {
        error_setg(errp, "Cannot derive password: %s",
                   gnutls_strerror(ret));
        return -1;
    }

    return 0;
}
