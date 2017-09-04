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
#include <nettle/pbkdf2.h>
#include <nettle/hmac.h>
#include "qapi/error.h"
#include "crypto/pbkdf.h"


bool qcrypto_pbkdf2_supports(QCryptoHashAlgorithm hash)
{
    switch (hash) {
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
    union {
        struct hmac_md5_ctx md5;
        struct hmac_sha1_ctx sha1;
        struct hmac_sha224_ctx sha224;
        struct hmac_sha256_ctx sha256;
        struct hmac_sha384_ctx sha384;
        struct hmac_sha512_ctx sha512;
        struct hmac_ripemd160_ctx ripemd160;
    } ctx;

    if (iterations > UINT_MAX) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu must be less than %u",
                         (long long unsigned)iterations, UINT_MAX);
        return -1;
    }

    switch (hash) {
    case QCRYPTO_HASH_ALG_MD5:
        hmac_md5_set_key(&ctx.md5, nkey, key);
        PBKDF2(&ctx.md5, hmac_md5_update, hmac_md5_digest,
               MD5_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    case QCRYPTO_HASH_ALG_SHA1:
        hmac_sha1_set_key(&ctx.sha1, nkey, key);
        PBKDF2(&ctx.sha1, hmac_sha1_update, hmac_sha1_digest,
               SHA1_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    case QCRYPTO_HASH_ALG_SHA224:
        hmac_sha224_set_key(&ctx.sha224, nkey, key);
        PBKDF2(&ctx.sha224, hmac_sha224_update, hmac_sha224_digest,
               SHA224_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    case QCRYPTO_HASH_ALG_SHA256:
        hmac_sha256_set_key(&ctx.sha256, nkey, key);
        PBKDF2(&ctx.sha256, hmac_sha256_update, hmac_sha256_digest,
               SHA256_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    case QCRYPTO_HASH_ALG_SHA384:
        hmac_sha384_set_key(&ctx.sha384, nkey, key);
        PBKDF2(&ctx.sha384, hmac_sha384_update, hmac_sha384_digest,
               SHA384_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    case QCRYPTO_HASH_ALG_SHA512:
        hmac_sha512_set_key(&ctx.sha512, nkey, key);
        PBKDF2(&ctx.sha512, hmac_sha512_update, hmac_sha512_digest,
               SHA512_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    case QCRYPTO_HASH_ALG_RIPEMD160:
        hmac_ripemd160_set_key(&ctx.ripemd160, nkey, key);
        PBKDF2(&ctx.ripemd160, hmac_ripemd160_update, hmac_ripemd160_digest,
               RIPEMD160_DIGEST_SIZE, iterations, nsalt, salt, nout, out);
        break;

    default:
        error_setg_errno(errp, ENOSYS,
                         "PBKDF does not support hash algorithm %s",
                         QCryptoHashAlgorithm_str(hash));
        return -1;
    }
    return 0;
}
