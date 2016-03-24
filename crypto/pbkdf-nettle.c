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
#include "qapi/error.h"
#include "crypto/pbkdf.h"
#include "nettle/pbkdf2.h"


bool qcrypto_pbkdf2_supports(QCryptoHashAlgorithm hash)
{
    switch (hash) {
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
    switch (hash) {
    case QCRYPTO_HASH_ALG_SHA1:
        pbkdf2_hmac_sha1(nkey, key,
                         iterations,
                         nsalt, salt,
                         nout, out);
        break;

    case QCRYPTO_HASH_ALG_SHA256:
        pbkdf2_hmac_sha256(nkey, key,
                           iterations,
                           nsalt, salt,
                           nout, out);
        break;

    default:
        error_setg_errno(errp, ENOSYS,
                         "PBKDF does not support hash algorithm %d", hash);
        return -1;
    }
    return 0;
}
