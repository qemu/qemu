/*
 * QEMU Crypto hmac algorithms tests
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "crypto/init.h"
#include "crypto/hmac.h"

#define INPUT_TEXT1 "ABCDEFGHIJKLMNOPQRSTUVWXY"
#define INPUT_TEXT2 "Zabcdefghijklmnopqrstuvwx"
#define INPUT_TEXT3 "yz0123456789"
#define INPUT_TEXT INPUT_TEXT1 \
              INPUT_TEXT2 \
              INPUT_TEXT3

#define KEY "monkey monkey monkey monkey"

typedef struct QCryptoHmacTestData QCryptoHmacTestData;
struct QCryptoHmacTestData {
    QCryptoHashAlgo alg;
    const char *hex_digest;
};

static QCryptoHmacTestData test_data[] = {
    {
        .alg = QCRYPTO_HASH_ALGO_MD5,
        .hex_digest =
            "ede9cb83679ba82d88fbeae865b3f8fc",
    },
    {
        .alg = QCRYPTO_HASH_ALGO_SHA1,
        .hex_digest =
            "c7b5a631e3aac975c4ededfcd346e469"
            "dbc5f2d1",
    },
    {
        .alg = QCRYPTO_HASH_ALGO_SHA224,
        .hex_digest =
            "5f768179dbb29ca722875d0f461a2e2f"
            "597d0210340a84df1a8e9c63",
    },
    {
        .alg = QCRYPTO_HASH_ALGO_SHA256,
        .hex_digest =
            "3798f363c57afa6edaffe39016ca7bad"
            "efd1e670afb0e3987194307dec3197db",
    },
    {
        .alg = QCRYPTO_HASH_ALGO_SHA384,
        .hex_digest =
            "d218680a6032d33dccd9882d6a6a7164"
            "64f26623be257a9b2919b185294f4a49"
            "9e54b190bfd6bc5cedd2cd05c7e65e82",
    },
    {
        .alg = QCRYPTO_HASH_ALGO_SHA512,
        .hex_digest =
            "835a4f5b3750b4c1fccfa88da2f746a4"
            "900160c9f18964309bb736c13b59491b"
            "8e32d37b724cc5aebb0f554c6338a3b5"
            "94c4ba26862b2dadb59b7ede1d08d53e",
    },
    {
        .alg = QCRYPTO_HASH_ALGO_RIPEMD160,
        .hex_digest =
            "94964ed4c1155b62b668c241d67279e5"
            "8a711676",
    },
#ifdef CONFIG_CRYPTO_SM3
    {
        .alg = QCRYPTO_HASH_ALGO_SM3,
        .hex_digest =
            "760e3799332bc913819b930085360ddb"
    "c05529261313d5b15b75bab4fd7ae91e",
    },
#endif
};

static const char hex[] = "0123456789abcdef";

static void test_hmac_alloc(void)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        QCryptoHmacTestData *data = &test_data[i];
        QCryptoHmac *hmac = NULL;
        uint8_t *result = NULL;
        size_t resultlen = 0;
        const char *exp_output = NULL;
        int ret;
        size_t j;

        if (!qcrypto_hmac_supports(data->alg)) {
            return;
        }

        exp_output = data->hex_digest;

        hmac = qcrypto_hmac_new(data->alg, (const uint8_t *)KEY,
                                strlen(KEY), &error_fatal);
        g_assert(hmac != NULL);

        ret = qcrypto_hmac_bytes(hmac, (const char *)INPUT_TEXT,
                                 strlen(INPUT_TEXT), &result,
                                 &resultlen, &error_fatal);
        g_assert(ret == 0);

        for (j = 0; j < resultlen; j++) {
            g_assert(exp_output[j * 2] == hex[(result[j] >> 4) & 0xf]);
            g_assert(exp_output[j * 2 + 1] == hex[result[j] & 0xf]);
        }

        qcrypto_hmac_free(hmac);

        g_free(result);
    }
}

static void test_hmac_prealloc(void)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        QCryptoHmacTestData *data = &test_data[i];
        QCryptoHmac *hmac = NULL;
        uint8_t *result = NULL, *origresult = NULL;
        size_t resultlen = 0;
        const char *exp_output = NULL;
        int ret;
        size_t j;

        if (!qcrypto_hmac_supports(data->alg)) {
            return;
        }

        exp_output = data->hex_digest;

        resultlen = strlen(exp_output) / 2;
        origresult = result = g_new0(uint8_t, resultlen);

        hmac = qcrypto_hmac_new(data->alg, (const uint8_t *)KEY,
                                strlen(KEY), &error_fatal);
        g_assert(hmac != NULL);

        ret = qcrypto_hmac_bytes(hmac, (const char *)INPUT_TEXT,
                                 strlen(INPUT_TEXT), &result,
                                 &resultlen, &error_fatal);
        g_assert(ret == 0);
        /* Validate that our pre-allocated pointer was not replaced */
        g_assert(result == origresult);

        exp_output = data->hex_digest;
        for (j = 0; j < resultlen; j++) {
            g_assert(exp_output[j * 2] == hex[(result[j] >> 4) & 0xf]);
            g_assert(exp_output[j * 2 + 1] == hex[result[j] & 0xf]);
        }

        qcrypto_hmac_free(hmac);

        g_free(result);
    }
}

static void test_hmac_iov(void)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        QCryptoHmacTestData *data = &test_data[i];
        QCryptoHmac *hmac = NULL;
        uint8_t *result = NULL;
        size_t resultlen = 0;
        const char *exp_output = NULL;
        int ret;
        size_t j;
        struct iovec iov[3] = {
            { .iov_base = (char *)INPUT_TEXT1, .iov_len = strlen(INPUT_TEXT1) },
            { .iov_base = (char *)INPUT_TEXT2, .iov_len = strlen(INPUT_TEXT2) },
            { .iov_base = (char *)INPUT_TEXT3, .iov_len = strlen(INPUT_TEXT3) },
        };

        if (!qcrypto_hmac_supports(data->alg)) {
            return;
        }

        exp_output = data->hex_digest;

        hmac = qcrypto_hmac_new(data->alg, (const uint8_t *)KEY,
                                strlen(KEY), &error_fatal);
        g_assert(hmac != NULL);

        ret = qcrypto_hmac_bytesv(hmac, iov, 3, &result,
                                  &resultlen, &error_fatal);
        g_assert(ret == 0);

        for (j = 0; j < resultlen; j++) {
            g_assert(exp_output[j * 2] == hex[(result[j] >> 4) & 0xf]);
            g_assert(exp_output[j * 2 + 1] == hex[result[j] & 0xf]);
        }

        qcrypto_hmac_free(hmac);

        g_free(result);
    }
}

static void test_hmac_digest(void)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        QCryptoHmacTestData *data = &test_data[i];
        QCryptoHmac *hmac = NULL;
        uint8_t *result = NULL;
        const char *exp_output = NULL;
        int ret;

        if (!qcrypto_hmac_supports(data->alg)) {
            return;
        }

        exp_output = data->hex_digest;

        hmac = qcrypto_hmac_new(data->alg, (const uint8_t *)KEY,
                                strlen(KEY), &error_fatal);
        g_assert(hmac != NULL);

        ret = qcrypto_hmac_digest(hmac, (const char *)INPUT_TEXT,
                                  strlen(INPUT_TEXT), (char **)&result,
                                  &error_fatal);
        g_assert(ret == 0);

        g_assert_cmpstr((const char *)result, ==, exp_output);

        qcrypto_hmac_free(hmac);

        g_free(result);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    g_test_add_func("/crypto/hmac/iov", test_hmac_iov);
    g_test_add_func("/crypto/hmac/alloc", test_hmac_alloc);
    g_test_add_func("/crypto/hmac/prealloc", test_hmac_prealloc);
    g_test_add_func("/crypto/hmac/digest", test_hmac_digest);

    return g_test_run();
}
