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

#include "qemu/osdep.h"
#include <glib.h>

#include "crypto/init.h"
#include "crypto/cipher.h"

typedef struct QCryptoCipherTestData QCryptoCipherTestData;
struct QCryptoCipherTestData {
    const char *path;
    QCryptoCipherAlgorithm alg;
    QCryptoCipherMode mode;
    const char *key;
    const char *plaintext;
    const char *ciphertext;
    const char *iv;
};

/* AES test data comes from appendix F of:
 *
 * http://csrc.nist.gov/publications/nistpubs/800-38a/sp800-38a.pdf
 */
static QCryptoCipherTestData test_data[] = {
    {
        /* NIST F.1.1 ECB-AES128.Encrypt */
        .path = "/crypto/cipher/aes-ecb-128",
        .alg = QCRYPTO_CIPHER_ALG_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key = "2b7e151628aed2a6abf7158809cf4f3c",
        .plaintext =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "3ad77bb40d7a3660a89ecaf32466ef97"
            "f5d3d58503b9699de785895a96fdbaaf"
            "43b1cd7f598ece23881b00e3ed030688"
            "7b0c785e27e8ad3f8223207104725dd4"
    },
    {
        /* NIST F.1.3 ECB-AES192.Encrypt */
        .path = "/crypto/cipher/aes-ecb-192",
        .alg = QCRYPTO_CIPHER_ALG_AES_192,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key = "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
        .plaintext  =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "bd334f1d6e45f25ff712a214571fa5cc"
            "974104846d0ad3ad7734ecb3ecee4eef"
            "ef7afd2270e2e60adce0ba2face6444e"
            "9a4b41ba738d6c72fb16691603c18e0e"
    },
    {
        /* NIST F.1.5 ECB-AES256.Encrypt */
        .path = "/crypto/cipher/aes-ecb-256",
        .alg = QCRYPTO_CIPHER_ALG_AES_256,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key =
            "603deb1015ca71be2b73aef0857d7781"
            "1f352c073b6108d72d9810a30914dff4",
        .plaintext  =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "f3eed1bdb5d2a03c064b5a7e3db181f8"
            "591ccb10d410ed26dc5ba74a31362870"
            "b6ed21b99ca6f4f9f153e7b1beafed1d"
            "23304b7a39f9f3ff067d8d8f9e24ecc7",
    },
    {
        /* NIST F.2.1 CBC-AES128.Encrypt */
        .path = "/crypto/cipher/aes-cbc-128",
        .alg = QCRYPTO_CIPHER_ALG_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_CBC,
        .key = "2b7e151628aed2a6abf7158809cf4f3c",
        .iv = "000102030405060708090a0b0c0d0e0f",
        .plaintext  =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "7649abac8119b246cee98e9b12e9197d"
            "5086cb9b507219ee95db113a917678b2"
            "73bed6b8e3c1743b7116e69e22229516"
            "3ff1caa1681fac09120eca307586e1a7",
    },
    {
        /* NIST F.2.3 CBC-AES128.Encrypt */
        .path = "/crypto/cipher/aes-cbc-192",
        .alg = QCRYPTO_CIPHER_ALG_AES_192,
        .mode = QCRYPTO_CIPHER_MODE_CBC,
        .key = "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
        .iv = "000102030405060708090a0b0c0d0e0f",
        .plaintext  =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "4f021db243bc633d7178183a9fa071e8"
            "b4d9ada9ad7dedf4e5e738763f69145a"
            "571b242012fb7ae07fa9baac3df102e0"
            "08b0e27988598881d920a9e64f5615cd",
    },
    {
        /* NIST F.2.5 CBC-AES128.Encrypt */
        .path = "/crypto/cipher/aes-cbc-256",
        .alg = QCRYPTO_CIPHER_ALG_AES_256,
        .mode = QCRYPTO_CIPHER_MODE_CBC,
        .key =
            "603deb1015ca71be2b73aef0857d7781"
            "1f352c073b6108d72d9810a30914dff4",
        .iv = "000102030405060708090a0b0c0d0e0f",
        .plaintext  =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
            "9cfc4e967edb808d679f777bc6702c7d"
            "39f23369a9d9bacfa530e26304231461"
            "b2eb05e2c39be9fcda6c19078c6a9d1b",
    },
    {
        .path = "/crypto/cipher/des-rfb-ecb-56",
        .alg = QCRYPTO_CIPHER_ALG_DES_RFB,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key = "0123456789abcdef",
        .plaintext =
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
        .ciphertext =
            "8f346aaf64eaf24040720d80648c52e7"
            "aefc616be53ab1a3d301e69d91e01838"
            "ffd29f1bb5596ad94ea2d8e6196b7f09"
            "30d8ed0bf2773af36dd82a6280c20926",
    },
};


static inline int unhex(char c)
{
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return c - '0';
}

static inline char hex(int i)
{
    if (i < 10) {
        return '0' + i;
    }
    return 'a' + (i - 10);
}

static size_t unhex_string(const char *hexstr,
                           uint8_t **data)
{
    size_t len;
    size_t i;

    if (!hexstr) {
        *data = NULL;
        return 0;
    }

    len = strlen(hexstr);
    *data = g_new0(uint8_t, len / 2);

    for (i = 0; i < len; i += 2) {
        (*data)[i/2] = (unhex(hexstr[i]) << 4) | unhex(hexstr[i+1]);
    }
    return len / 2;
}

static char *hex_string(const uint8_t *bytes,
                        size_t len)
{
    char *hexstr = g_new0(char, len * 2 + 1);
    size_t i;

    for (i = 0; i < len; i++) {
        hexstr[i*2] = hex((bytes[i] >> 4) & 0xf);
        hexstr[i*2+1] = hex(bytes[i] & 0xf);
    }
    hexstr[len*2] = '\0';

    return hexstr;
}

static void test_cipher(const void *opaque)
{
    const QCryptoCipherTestData *data = opaque;

    QCryptoCipher *cipher;
    uint8_t *key, *iv, *ciphertext, *plaintext, *outtext;
    size_t nkey, niv, nciphertext, nplaintext;
    char *outtexthex;
    size_t ivsize, keysize, blocksize;

    nkey = unhex_string(data->key, &key);
    niv = unhex_string(data->iv, &iv);
    nciphertext = unhex_string(data->ciphertext, &ciphertext);
    nplaintext = unhex_string(data->plaintext, &plaintext);

    g_assert(nciphertext == nplaintext);

    outtext = g_new0(uint8_t, nciphertext);

    cipher = qcrypto_cipher_new(
        data->alg, data->mode,
        key, nkey,
        &error_abort);
    g_assert(cipher != NULL);

    keysize = qcrypto_cipher_get_key_len(data->alg);
    blocksize = qcrypto_cipher_get_block_len(data->alg);
    ivsize = qcrypto_cipher_get_iv_len(data->alg, data->mode);

    g_assert_cmpint(keysize, ==, nkey);
    g_assert_cmpint(ivsize, ==, niv);
    if (niv) {
        g_assert_cmpint(blocksize, ==, niv);
    }

    if (iv) {
        g_assert(qcrypto_cipher_setiv(cipher,
                                      iv, niv,
                                      &error_abort) == 0);
    }
    g_assert(qcrypto_cipher_encrypt(cipher,
                                    plaintext,
                                    outtext,
                                    nplaintext,
                                    &error_abort) == 0);

    outtexthex = hex_string(outtext, nciphertext);

    g_assert_cmpstr(outtexthex, ==, data->ciphertext);

    g_free(outtexthex);

    if (iv) {
        g_assert(qcrypto_cipher_setiv(cipher,
                                      iv, niv,
                                      &error_abort) == 0);
    }
    g_assert(qcrypto_cipher_decrypt(cipher,
                                    ciphertext,
                                    outtext,
                                    nplaintext,
                                    &error_abort) == 0);

    outtexthex = hex_string(outtext, nplaintext);

    g_assert_cmpstr(outtexthex, ==, data->plaintext);

    g_free(outtext);
    g_free(outtexthex);
    g_free(key);
    g_free(iv);
    g_free(ciphertext);
    g_free(plaintext);
    qcrypto_cipher_free(cipher);
}


static void test_cipher_null_iv(void)
{
    QCryptoCipher *cipher;
    uint8_t key[32] = { 0 };
    uint8_t plaintext[32] = { 0 };
    uint8_t ciphertext[32] = { 0 };

    cipher = qcrypto_cipher_new(
        QCRYPTO_CIPHER_ALG_AES_256,
        QCRYPTO_CIPHER_MODE_CBC,
        key, sizeof(key),
        &error_abort);
    g_assert(cipher != NULL);

    /* Don't call qcrypto_cipher_setiv */

    qcrypto_cipher_encrypt(cipher,
                           plaintext,
                           ciphertext,
                           sizeof(plaintext),
                           &error_abort);

    qcrypto_cipher_free(cipher);
}

static void test_cipher_short_plaintext(void)
{
    Error *err = NULL;
    QCryptoCipher *cipher;
    uint8_t key[32] = { 0 };
    uint8_t plaintext1[20] = { 0 };
    uint8_t ciphertext1[20] = { 0 };
    uint8_t plaintext2[40] = { 0 };
    uint8_t ciphertext2[40] = { 0 };
    int ret;

    cipher = qcrypto_cipher_new(
        QCRYPTO_CIPHER_ALG_AES_256,
        QCRYPTO_CIPHER_MODE_CBC,
        key, sizeof(key),
        &error_abort);
    g_assert(cipher != NULL);

    /* Should report an error as plaintext is shorter
     * than block size
     */
    ret = qcrypto_cipher_encrypt(cipher,
                                 plaintext1,
                                 ciphertext1,
                                 sizeof(plaintext1),
                                 &err);
    g_assert(ret == -1);
    g_assert(err != NULL);

    error_free(err);
    err = NULL;

    /* Should report an error as plaintext is larger than
     * block size, but not a multiple of block size
     */
    ret = qcrypto_cipher_encrypt(cipher,
                                 plaintext2,
                                 ciphertext2,
                                 sizeof(plaintext2),
                                 &err);
    g_assert(ret == -1);
    g_assert(err != NULL);

    error_free(err);
    qcrypto_cipher_free(cipher);
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        g_test_add_data_func(test_data[i].path, &test_data[i], test_cipher);
    }

    g_test_add_func("/crypto/cipher/null-iv",
                    test_cipher_null_iv);

    g_test_add_func("/crypto/cipher/short-plaintext",
                    test_cipher_short_plaintext);

    return g_test_run();
}
