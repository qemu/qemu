/*
 * QEMU Crypto block IV generator
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

#include "ivgenpriv.h"
#include "ivgen-plain.h"
#include "ivgen-plain64.h"
#include "ivgen-essiv.h"


QCryptoIVGen *qcrypto_ivgen_new(QCryptoIVGenAlgorithm alg,
                                QCryptoCipherAlgorithm cipheralg,
                                QCryptoHashAlgorithm hash,
                                const uint8_t *key, size_t nkey,
                                Error **errp)
{
    QCryptoIVGen *ivgen = g_new0(QCryptoIVGen, 1);

    ivgen->algorithm = alg;
    ivgen->cipher = cipheralg;
    ivgen->hash = hash;

    switch (alg) {
    case QCRYPTO_IVGEN_ALG_PLAIN:
        ivgen->driver = &qcrypto_ivgen_plain;
        break;
    case QCRYPTO_IVGEN_ALG_PLAIN64:
        ivgen->driver = &qcrypto_ivgen_plain64;
        break;
    case QCRYPTO_IVGEN_ALG_ESSIV:
        ivgen->driver = &qcrypto_ivgen_essiv;
        break;
    default:
        error_setg(errp, "Unknown block IV generator algorithm %d", alg);
        g_free(ivgen);
        return NULL;
    }

    if (ivgen->driver->init(ivgen, key, nkey, errp) < 0) {
        g_free(ivgen);
        return NULL;
    }

    return ivgen;
}


int qcrypto_ivgen_calculate(QCryptoIVGen *ivgen,
                            uint64_t sector,
                            uint8_t *iv, size_t niv,
                            Error **errp)
{
    return ivgen->driver->calculate(ivgen, sector, iv, niv, errp);
}


QCryptoIVGenAlgorithm qcrypto_ivgen_get_algorithm(QCryptoIVGen *ivgen)
{
    return ivgen->algorithm;
}


QCryptoCipherAlgorithm qcrypto_ivgen_get_cipher(QCryptoIVGen *ivgen)
{
    return ivgen->cipher;
}


QCryptoHashAlgorithm qcrypto_ivgen_get_hash(QCryptoIVGen *ivgen)
{
    return ivgen->hash;
}


void qcrypto_ivgen_free(QCryptoIVGen *ivgen)
{
    if (!ivgen) {
        return;
    }
    ivgen->driver->cleanup(ivgen);
    g_free(ivgen);
}
