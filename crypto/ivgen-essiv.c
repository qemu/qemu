/*
 * QEMU Crypto block IV generator - essiv
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

#include "crypto/ivgen-essiv.h"

typedef struct QCryptoIVGenESSIV QCryptoIVGenESSIV;
struct QCryptoIVGenESSIV {
    QCryptoCipher *cipher;
    QCryptoCipherAlgorithm cipheralg;
};

static int qcrypto_ivgen_essiv_init(QCryptoIVGen *ivgen,
                                    QCryptoCipherAlgorithm cipheralg,
                                    QCryptoHashAlgorithm hash,
                                    const uint8_t *key, size_t nkey,
                                    Error **errp)
{
    uint8_t *salt;
    size_t nhash;
    QCryptoIVGenESSIV *essiv = g_new0(QCryptoIVGenESSIV, 1);

    nhash = qcrypto_hash_digest_len(hash);
    /* Salt must be larger of hash size or key size */
    salt = g_new0(uint8_t, nhash > nkey ? nhash : nkey);

    if (qcrypto_hash_bytes(hash, (const gchar*)key, nkey,
                           &salt, &nhash,
                           errp) < 0) {
        g_free(essiv);
        return -1;
    }

    essiv->cipheralg = cipheralg;
    essiv->cipher = qcrypto_cipher_new(cipheralg,
                                       QCRYPTO_CIPHER_MODE_ECB,
                                       salt, nkey,
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
    size_t ndata = qcrypto_cipher_get_block_len(essiv->cipheralg);
    uint8_t *data = g_new(uint8_t, ndata);

    sector = cpu_to_le64((uint32_t)sector);
    memcpy(data, (uint8_t *)&sector, ndata);
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
    qcrypto_ivgen_essiv_init,
    qcrypto_ivgen_essiv_calculate,
    qcrypto_ivgen_essiv_cleanup,
};

