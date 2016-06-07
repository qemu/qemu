/*
 * QEMU Crypto hash algorithms
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

#include "crypto/init.h"
#include "crypto/hash.h"

#define INPUT_TEXT "Hiss hisss Hissss hiss Hiss hisss Hiss hiss"
#define INPUT_TEXT1 "Hiss hisss "
#define INPUT_TEXT2 "Hissss hiss "
#define INPUT_TEXT3 "Hiss hisss Hiss hiss"

#define OUTPUT_MD5 "628d206371563035ab8ef62f492bdec9"
#define OUTPUT_SHA1 "b2e74f26758a3a421e509cee045244b78753cc02"
#define OUTPUT_SHA256 "bc757abb0436586f392b437e5dd24096" \
                      "f7f224de6b74d4d86e2abc6121b160d0"

#define OUTPUT_MD5_B64 "Yo0gY3FWMDWrjvYvSSveyQ=="
#define OUTPUT_SHA1_B64 "sudPJnWKOkIeUJzuBFJEt4dTzAI="
#define OUTPUT_SHA256_B64 "vHV6uwQ2WG85K0N+XdJAlvfyJN5rdNTYbiq8YSGxYNA="

static const char *expected_outputs[] = {
    [QCRYPTO_HASH_ALG_MD5] = OUTPUT_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = OUTPUT_SHA1,
    [QCRYPTO_HASH_ALG_SHA256] = OUTPUT_SHA256,
};
static const char *expected_outputs_b64[] = {
    [QCRYPTO_HASH_ALG_MD5] = OUTPUT_MD5_B64,
    [QCRYPTO_HASH_ALG_SHA1] = OUTPUT_SHA1_B64,
    [QCRYPTO_HASH_ALG_SHA256] = OUTPUT_SHA256_B64,
};
static const int expected_lens[] = {
    [QCRYPTO_HASH_ALG_MD5] = 16,
    [QCRYPTO_HASH_ALG_SHA1] = 20,
    [QCRYPTO_HASH_ALG_SHA256] = 32,
};

static const char hex[] = "0123456789abcdef";

/* Test with dynamic allocation */
static void test_hash_alloc(void)
{
    size_t i;

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        uint8_t *result = NULL;
        size_t resultlen = 0;
        int ret;
        size_t j;

        ret = qcrypto_hash_bytes(i,
                                 INPUT_TEXT,
                                 strlen(INPUT_TEXT),
                                 &result,
                                 &resultlen,
                                 NULL);
        g_assert(ret == 0);
        g_assert(resultlen == expected_lens[i]);

        for (j = 0; j < resultlen; j++) {
            g_assert(expected_outputs[i][j * 2] == hex[(result[j] >> 4) & 0xf]);
            g_assert(expected_outputs[i][j * 2 + 1] == hex[result[j] & 0xf]);
        }
        g_free(result);
    }
}

/* Test with caller preallocating */
static void test_hash_prealloc(void)
{
    size_t i;

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        uint8_t *result;
        size_t resultlen;
        int ret;
        size_t j;

        resultlen = expected_lens[i];
        result = g_new0(uint8_t, resultlen);

        ret = qcrypto_hash_bytes(i,
                                 INPUT_TEXT,
                                 strlen(INPUT_TEXT),
                                 &result,
                                 &resultlen,
                                 NULL);
        g_assert(ret == 0);

        g_assert(resultlen == expected_lens[i]);
        for (j = 0; j < resultlen; j++) {
            g_assert(expected_outputs[i][j * 2] == hex[(result[j] >> 4) & 0xf]);
            g_assert(expected_outputs[i][j * 2 + 1] == hex[result[j] & 0xf]);
        }
        g_free(result);
    }
}


/* Test with dynamic allocation */
static void test_hash_iov(void)
{
    size_t i;

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        struct iovec iov[3] = {
            { .iov_base = (char *)INPUT_TEXT1, .iov_len = strlen(INPUT_TEXT1) },
            { .iov_base = (char *)INPUT_TEXT2, .iov_len = strlen(INPUT_TEXT2) },
            { .iov_base = (char *)INPUT_TEXT3, .iov_len = strlen(INPUT_TEXT3) },
        };
        uint8_t *result = NULL;
        size_t resultlen = 0;
        int ret;
        size_t j;

        ret = qcrypto_hash_bytesv(i,
                                  iov, 3,
                                  &result,
                                  &resultlen,
                                  NULL);
        g_assert(ret == 0);
        g_assert(resultlen == expected_lens[i]);
        for (j = 0; j < resultlen; j++) {
            g_assert(expected_outputs[i][j * 2] == hex[(result[j] >> 4) & 0xf]);
            g_assert(expected_outputs[i][j * 2 + 1] == hex[result[j] & 0xf]);
        }
        g_free(result);
    }
}


/* Test with printable hashing */
static void test_hash_digest(void)
{
    size_t i;

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        int ret;
        char *digest;
        size_t digestsize;

        digestsize = qcrypto_hash_digest_len(i);

        g_assert_cmpint(digestsize * 2, ==, strlen(expected_outputs[i]));

        ret = qcrypto_hash_digest(i,
                                  INPUT_TEXT,
                                  strlen(INPUT_TEXT),
                                  &digest,
                                  NULL);
        g_assert(ret == 0);
        g_assert(g_str_equal(digest, expected_outputs[i]));
        g_free(digest);
    }
}

/* Test with base64 encoding */
static void test_hash_base64(void)
{
    size_t i;

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        int ret;
        char *digest;

        ret = qcrypto_hash_base64(i,
                                  INPUT_TEXT,
                                  strlen(INPUT_TEXT),
                                  &digest,
                                  NULL);
        g_assert(ret == 0);
        g_assert(g_str_equal(digest, expected_outputs_b64[i]));
        g_free(digest);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/crypto/hash/iov", test_hash_iov);
    g_test_add_func("/crypto/hash/alloc", test_hash_alloc);
    g_test_add_func("/crypto/hash/prealloc", test_hash_prealloc);
    g_test_add_func("/crypto/hash/digest", test_hash_digest);
    g_test_add_func("/crypto/hash/base64", test_hash_base64);
    return g_test_run();
}
