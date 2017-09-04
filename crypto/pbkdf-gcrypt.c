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

#include "qemu/osdep.h"
#include <gcrypt.h>
#include "qapi/error.h"
#include "crypto/pbkdf.h"

bool qcrypto_pbkdf2_supports(QCryptoHashAlgorithm hash)
{
    switch (hash) {
    case QCRYPTO_HASH_ALG_MD5:
    case QCRYPTO_HASH_ALG_SHA1:
    case QCRYPTO_HASH_ALG_SHA224:
    case QCRYPTO_HASH_ALG_SHA256:
    case QCRYPTO_HASH_ALG_SHA384:
    case QCRYPTO_HASH_ALG_SHA512:
    case QCRYPTO_HASH_ALG_RIPEMD160:
        return true;
    default:
        return false;
    }
}

int qcrypto_pbkdf2(QCryptoHashAlgorithm hash,
                   const uint8_t *key, size_t nkey,
                   const uint8_t *salt, size_t nsalt,
                   uint64_t iterations,
                   uint8_t *out, size_t nout,
                   Error **errp)
{
    static const int hash_map[QCRYPTO_HASH_ALG__MAX] = {
        [QCRYPTO_HASH_ALG_MD5] = GCRY_MD_MD5,
        [QCRYPTO_HASH_ALG_SHA1] = GCRY_MD_SHA1,
        [QCRYPTO_HASH_ALG_SHA224] = GCRY_MD_SHA224,
        [QCRYPTO_HASH_ALG_SHA256] = GCRY_MD_SHA256,
        [QCRYPTO_HASH_ALG_SHA384] = GCRY_MD_SHA384,
        [QCRYPTO_HASH_ALG_SHA512] = GCRY_MD_SHA512,
        [QCRYPTO_HASH_ALG_RIPEMD160] = GCRY_MD_RMD160,
    };
    int ret;

    if (iterations > ULONG_MAX) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu must be less than %lu",
                         (long long unsigned)iterations, ULONG_MAX);
        return -1;
    }

    if (hash >= G_N_ELEMENTS(hash_map) ||
        hash_map[hash] == GCRY_MD_NONE) {
        error_setg_errno(errp, ENOSYS,
                         "PBKDF does not support hash algorithm %s",
                         QCryptoHashAlgorithm_str(hash));
        return -1;
    }

    ret = gcry_kdf_derive(key, nkey, GCRY_KDF_PBKDF2,
                          hash_map[hash],
                          salt, nsalt, iterations,
                          nout, out);
    if (ret != 0) {
        error_setg(errp, "Cannot derive password: %s",
                   gcry_strerror(ret));
        return -1;
    }

    return 0;
}
