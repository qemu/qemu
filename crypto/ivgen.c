/*
 * QEMU Crypto block IV generator
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

#include "crypto/ivgenpriv.h"
#include "crypto/ivgen-plain.h"
#include "crypto/ivgen-plain64.h"
#include "crypto/ivgen-essiv.h"


/**
 * @qcrypto_ivgen_new:
 * @alg: the initialization vector generator algorithm
 * @cipheralg: the cipher algorithm (if applicable)
 * @hash: the hash algorithm (if applicable)
 * @key: the encryption key (if applicable)
 * @nkey: the length of @key in bytes
 * @errp: pointer to an uninitialized error object
 *
 * Create a new initialization vector generator implementing
 * the algorithm specified in @alg.
 *
 * For the ESSIV algorithm, the @cipheralg, @ciphermode
 * @hash, @key and @nkey parameters are required. For
 * other algorithms they should be NULL / zero as
 * appropriate.
 *
 * Returns the new IV generator or NULL on error
 */
QCryptoIVGen *qcrypto_ivgen_new(QCryptoIVGenAlgorithm alg,
                                QCryptoCipherAlgorithm cipheralg,
                                QCryptoHashAlgorithm hash,
                                const uint8_t *key, size_t nkey,
                                Error **errp)
{
    QCryptoIVGen *ivgen = g_new0(QCryptoIVGen, 1);


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

    if (ivgen->driver->init(ivgen, cipheralg, hash, key, nkey, errp) < 0) {
        g_free(ivgen);
        return NULL;
    }

    return ivgen;
}


/**
 * qcrypto_ivgen_calculate:
 * @ivgen: the IV generator object
 * @sector: the sector number in the volume
 * @iv: buffer to store the generated IV in
 * @niv: length of @iv in bytes
 * @errp: pointer to an uninitialized error object
 *
 * Calculate a new initialization vector storing the
 * result in @iv. The @iv buffer must be pre-allocated
 * by the caller and @niv provides its length in bytes.
 *
 * Returns 0 on success, -1 on failure
 */
int qcrypto_ivgen_calculate(QCryptoIVGen *ivgen,
                            uint64_t sector,
                            uint8_t *iv, size_t niv,
                            Error **errp)
{
    return ivgen->driver->calculate(ivgen, sector, iv, niv, errp);
}


/**
 * qcrypto_ivgen_free:
 * @ivgen: the IV generator object
 *
 * Release all resources associated with the
 * IV generator
 */
void qcrypto_ivgen_free(QCryptoIVGen *ivgen)
{
    if (!ivgen) {
        return;
    }
    ivgen->driver->cleanup(ivgen);
    g_free(ivgen);
}
