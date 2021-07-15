/*
 * QEMU Crypto IV generator algorithms
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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
#include "qapi/error.h"
#include "crypto/ivgen.h"


struct QCryptoIVGenTestData {
    const char *path;
    uint64_t sector;
    QCryptoIVGenAlgorithm ivalg;
    QCryptoHashAlgorithm hashalg;
    QCryptoCipherAlgorithm cipheralg;
    const uint8_t *key;
    size_t nkey;
    const uint8_t *iv;
    size_t niv;
} test_data[] = {
    /* Small */
    {
        "/crypto/ivgen/plain/1",
        .sector = 0x1,
        .ivalg = QCRYPTO_IVGEN_ALG_PLAIN,
        .iv = (const uint8_t *)"\x01\x00\x00\x00\x00\x00\x00\x00"
                               "\x00\x00\x00\x00\x00\x00\x00\x00",
        .niv = 16,
    },
    /* Big ! */
    {
        "/crypto/ivgen/plain/1f2e3d4c",
        .sector = 0x1f2e3d4cULL,
        .ivalg = QCRYPTO_IVGEN_ALG_PLAIN,
        .iv = (const uint8_t *)"\x4c\x3d\x2e\x1f\x00\x00\x00\x00"
                               "\x00\x00\x00\x00\x00\x00\x00\x00",
        .niv = 16,
    },
    /* Truncation */
    {
        "/crypto/ivgen/plain/1f2e3d4c5b6a7988",
        .sector = 0x1f2e3d4c5b6a7988ULL,
        .ivalg = QCRYPTO_IVGEN_ALG_PLAIN,
        .iv = (const uint8_t *)"\x88\x79\x6a\x5b\x00\x00\x00\x00"
                               "\x00\x00\x00\x00\x00\x00\x00\x00",
        .niv = 16,
    },
    /* Small */
    {
        "/crypto/ivgen/plain64/1",
        .sector = 0x1,
        .ivalg = QCRYPTO_IVGEN_ALG_PLAIN64,
        .iv = (const uint8_t *)"\x01\x00\x00\x00\x00\x00\x00\x00"
                               "\x00\x00\x00\x00\x00\x00\x00\x00",
        .niv = 16,
    },
    /* Big ! */
    {
        "/crypto/ivgen/plain64/1f2e3d4c",
        .sector = 0x1f2e3d4cULL,
        .ivalg = QCRYPTO_IVGEN_ALG_PLAIN64,
        .iv = (const uint8_t *)"\x4c\x3d\x2e\x1f\x00\x00\x00\x00"
                               "\x00\x00\x00\x00\x00\x00\x00\x00",
        .niv = 16,
    },
    /* No Truncation */
    {
        "/crypto/ivgen/plain64/1f2e3d4c5b6a7988",
        .sector = 0x1f2e3d4c5b6a7988ULL,
        .ivalg = QCRYPTO_IVGEN_ALG_PLAIN64,
        .iv = (const uint8_t *)"\x88\x79\x6a\x5b\x4c\x3d\x2e\x1f"
                               "\x00\x00\x00\x00\x00\x00\x00\x00",
        .niv = 16,
    },
    /* Small */
    {
        "/crypto/ivgen/essiv/1",
        .sector = 0x1,
        .ivalg = QCRYPTO_IVGEN_ALG_ESSIV,
        .cipheralg = QCRYPTO_CIPHER_ALG_AES_128,
        .hashalg = QCRYPTO_HASH_ALG_SHA256,
        .key = (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06\x07"
                                "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
        .nkey = 16,
        .iv = (const uint8_t *)"\xd4\x83\x71\xb2\xa1\x94\x53\x88"
                               "\x1c\x7a\x2d\06\x2d\x0b\x65\x46",
        .niv = 16,
    },
    /* Big ! */
    {
        "/crypto/ivgen/essiv/1f2e3d4c",
        .sector = 0x1f2e3d4cULL,
        .ivalg = QCRYPTO_IVGEN_ALG_ESSIV,
        .cipheralg = QCRYPTO_CIPHER_ALG_AES_128,
        .hashalg = QCRYPTO_HASH_ALG_SHA256,
        .key = (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06\x07"
                                "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
        .nkey = 16,
        .iv = (const uint8_t *)"\x5d\x36\x09\x5d\xc6\x9e\x5e\xe9"
                               "\xe3\x02\x8d\xd8\x7a\x3d\xe7\x8f",
        .niv = 16,
    },
    /* No Truncation */
    {
        "/crypto/ivgen/essiv/1f2e3d4c5b6a7988",
        .sector = 0x1f2e3d4c5b6a7988ULL,
        .ivalg = QCRYPTO_IVGEN_ALG_ESSIV,
        .cipheralg = QCRYPTO_CIPHER_ALG_AES_128,
        .hashalg = QCRYPTO_HASH_ALG_SHA256,
        .key = (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06\x07"
                                "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
        .nkey = 16,
        .iv = (const uint8_t *)"\x58\xbb\x81\x94\x51\x83\x23\x23"
                               "\x7a\x08\x93\xa9\xdc\xd2\xd9\xab",
        .niv = 16,
    },
};


static void test_ivgen(const void *opaque)
{
    const struct QCryptoIVGenTestData *data = opaque;
    g_autofree uint8_t *iv = g_new0(uint8_t, data->niv);
    g_autoptr(QCryptoIVGen) ivgen = NULL;

    if (!qcrypto_cipher_supports(data->cipheralg,
                                 QCRYPTO_CIPHER_MODE_ECB)) {
        return;
    }

    ivgen = qcrypto_ivgen_new(
        data->ivalg,
        data->cipheralg,
        data->hashalg,
        data->key,
        data->nkey,
        &error_abort);

    qcrypto_ivgen_calculate(ivgen,
                            data->sector,
                            iv,
                            data->niv,
                            &error_abort);

    g_assert(memcmp(iv, data->iv, data->niv) == 0);
}

int main(int argc, char **argv)
{
    size_t i;
    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        if (test_data[i].ivalg == QCRYPTO_IVGEN_ALG_ESSIV &&
            !qcrypto_hash_supports(test_data[i].hashalg)) {
            continue;
        }
        g_test_add_data_func(test_data[i].path,
                             &(test_data[i]),
                             test_ivgen);
    }
    return g_test_run();
}
