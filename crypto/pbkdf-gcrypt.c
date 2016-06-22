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
    case QCRYPTO_HASH_ALG_SHA256:
        return true;
    default:
        return false;
    }
}

int qcrypto_pbkdf2(QCryptoHashAlgorithm hash,
                   const uint8_t *key, size_t nkey,
                   const uint8_t *salt, size_t nsalt,
                   unsigned int iterations,
                   uint8_t *out, size_t nout,
                   Error **errp)
{
    static const int hash_map[QCRYPTO_HASH_ALG__MAX] = {
        [QCRYPTO_HASH_ALG_MD5] = GCRY_MD_MD5,
        [QCRYPTO_HASH_ALG_SHA1] = GCRY_MD_SHA1,
        [QCRYPTO_HASH_ALG_SHA256] = GCRY_MD_SHA256,
    };
    int ret;

    if (hash >= G_N_ELEMENTS(hash_map) ||
        hash_map[hash] == GCRY_MD_NONE) {
        error_setg(errp, "Unexpected hash algorithm %d", hash);
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
