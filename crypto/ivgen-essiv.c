/*
 * QEMU Crypto block IV generator - essiv
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
#include "qemu/bswap.h"
#include "ivgen-essiv.h"

typedef struct QCryptoIVGenESSIV QCryptoIVGenESSIV;
struct QCryptoIVGenESSIV {
    QCryptoCipher *cipher;
};

static int qcrypto_ivgen_essiv_init(QCryptoIVGen *ivgen,
                                    const uint8_t *key, size_t nkey,
                                    Error **errp)
{
    uint8_t *salt;
    size_t nhash;
    size_t nsalt;
    QCryptoIVGenESSIV *essiv = g_new0(QCryptoIVGenESSIV, 1);

    /* Not necessarily the same as nkey */
    nsalt = qcrypto_cipher_get_key_len(ivgen->cipher);

    nhash = qcrypto_hash_digest_len(ivgen->hash);
    /* Salt must be larger of hash size or key size */
    salt = g_new0(uint8_t, MAX(nhash, nsalt));

    if (qcrypto_hash_bytes(ivgen->hash, (const gchar *)key, nkey,
                           &salt, &nhash,
                           errp) < 0) {
        g_free(essiv);
        g_free(salt);
        return -1;
    }

    /* Now potentially truncate salt to match cipher key len */
    essiv->cipher = qcrypto_cipher_new(ivgen->cipher,
                                       QCRYPTO_CIPHER_MODE_ECB,
                                       salt, MIN(nhash, nsalt),
                                       errp);
    if (!essiv->cipher) {
        g_free(essiv);
        g_free(salt);
        return -1;
    }

    g_free(salt);
    ivgen->private = essiv;

    return 0;
}

static int qcrypto_ivgen_essiv_calculate(QCryptoIVGen *ivgen,
                                         uint64_t sector,
                                         uint8_t *iv, size_t niv,
                                         Error **errp)
{
    QCryptoIVGenESSIV *essiv = ivgen->private;
    size_t ndata = qcrypto_cipher_get_block_len(ivgen->cipher);
    uint8_t *data = g_new(uint8_t, ndata);

    sector = cpu_to_le64(sector);
    memcpy(data, (uint8_t *)&sector, MIN(sizeof(sector), ndata));
    if (sizeof(sector) < ndata) {
        memset(data + sizeof(sector), 0, ndata - sizeof(sector));
    }

    if (qcrypto_cipher_encrypt(essiv->cipher,
                               data,
                               data,
                               ndata,
                               errp) < 0) {
        g_free(data);
        return -1;
    }

    if (ndata > niv) {
        ndata = niv;
    }
    memcpy(iv, data, ndata);
    if (ndata < niv) {
        memset(iv + ndata, 0, niv - ndata);
    }
    g_free(data);
    return 0;
}

static void qcrypto_ivgen_essiv_cleanup(QCryptoIVGen *ivgen)
{
    QCryptoIVGenESSIV *essiv = ivgen->private;

    qcrypto_cipher_free(essiv->cipher);
    g_free(essiv);
}


struct QCryptoIVGenDriver qcrypto_ivgen_essiv = {
    .init = qcrypto_ivgen_essiv_init,
    .calculate = qcrypto_ivgen_essiv_calculate,
    .cleanup = qcrypto_ivgen_essiv_cleanup,
};

