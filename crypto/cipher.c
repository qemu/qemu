/*
 * QEMU Crypto cipher algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#include "crypto/cipher.h"

static size_t alg_key_len[QCRYPTO_CIPHER_ALG_LAST] = {
    [QCRYPTO_CIPHER_ALG_AES_128] = 16,
    [QCRYPTO_CIPHER_ALG_AES_192] = 24,
    [QCRYPTO_CIPHER_ALG_AES_256] = 32,
    [QCRYPTO_CIPHER_ALG_DES_RFB] = 8,
};

static bool
qcrypto_cipher_validate_key_length(QCryptoCipherAlgorithm alg,
                                   size_t nkey,
                                   Error **errp)
{
    if ((unsigned)alg >= QCRYPTO_CIPHER_ALG_LAST) {
        error_setg(errp, "Cipher algorithm %d out of range",
                   alg);
        return false;
    }

    if (alg_key_len[alg] != nkey) {
        error_setg(errp, "Cipher key length %zu should be %zu",
                   alg_key_len[alg], nkey);
        return false;
    }
    return true;
}

#include "crypto/cipher-builtin.c"
