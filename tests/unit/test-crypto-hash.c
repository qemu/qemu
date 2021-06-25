/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#include "crypto/init.h"
#include "crypto/hash.h"

#define INPUT_TEXT "Hiss hisss Hissss hiss Hiss hisss Hiss hiss"
#define INPUT_TEXT1 "Hiss hisss "
#define INPUT_TEXT2 "Hissss hiss "
#define INPUT_TEXT3 "Hiss hisss Hiss hiss"

#define OUTPUT_MD5 "628d206371563035ab8ef62f492bdec9"
#define OUTPUT_SHA1 "b2e74f26758a3a421e509cee045244b78753cc02"
#define OUTPUT_SHA224 "e2f7415aad33ef79f6516b0986d7175f" \
                      "9ca3389a85bf6cfed078737b"
#define OUTPUT_SHA256 "bc757abb0436586f392b437e5dd24096" \
                      "f7f224de6b74d4d86e2abc6121b160d0"
#define OUTPUT_SHA384 "887ce52efb4f46700376356583b7e279" \
                      "4f612bd024e4495087ddb946c448c69d" \
                      "56dbf7152a94a5e63a80f3ba9f0eed78"
#define OUTPUT_SHA512 "3a90d79638235ec6c4c11bebd84d83c0" \
                      "549bc1e84edc4b6ec7086487641256cb" \
                      "63b54e4cb2d2032b393994aa263c0dbb" \
                      "e00a9f2fe9ef6037352232a1eec55ee7"
#define OUTPUT_RIPEMD160 "f3d658fad3fdfb2b52c9369cf0d441249ddfa8a0"

#define OUTPUT_MD5_B64 "Yo0gY3FWMDWrjvYvSSveyQ=="
#define OUTPUT_SHA1_B64 "sudPJnWKOkIeUJzuBFJEt4dTzAI="
#define OUTPUT_SHA224_B64 "4vdBWq0z73n2UWsJhtcXX5yjOJqFv2z+0Hhzew=="
#define OUTPUT_SHA256_B64 "vHV6uwQ2WG85K0N+XdJAlvfyJN5rdNTYbiq8YSGxYNA="
#define OUTPUT_SHA384_B64 "iHzlLvtPRnADdjVlg7fieU9hK9Ak5ElQh925RsRI" \
                          "xp1W2/cVKpSl5jqA87qfDu14"
#define OUTPUT_SHA512_B64 "OpDXljgjXsbEwRvr2E2DwFSbwehO3Etuxwhkh2QS" \
                          "VstjtU5MstIDKzk5lKomPA274AqfL+nvYDc1IjKh" \
                          "7sVe5w=="
#define OUTPUT_RIPEMD160_B64 "89ZY+tP9+ytSyTac8NRBJJ3fqKA="

static const char *expected_outputs[] = {
    [QCRYPTO_HASH_ALG_MD5] = OUTPUT_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = OUTPUT_SHA1,
    [QCRYPTO_HASH_ALG_SHA224] = OUTPUT_SHA224,
    [QCRYPTO_HASH_ALG_SHA256] = OUTPUT_SHA256,
    [QCRYPTO_HASH_ALG_SHA384] = OUTPUT_SHA384,
    [QCRYPTO_HASH_ALG_SHA512] = OUTPUT_SHA512,
    [QCRYPTO_HASH_ALG_RIPEMD160] = OUTPUT_RIPEMD160,
};
static const char *expected_outputs_b64[] = {
    [QCRYPTO_HASH_ALG_MD5] = OUTPUT_MD5_B64,
    [QCRYPTO_HASH_ALG_SHA1] = OUTPUT_SHA1_B64,
    [QCRYPTO_HASH_ALG_SHA224] = OUTPUT_SHA224_B64,
    [QCRYPTO_HASH_ALG_SHA256] = OUTPUT_SHA256_B64,
    [QCRYPTO_HASH_ALG_SHA384] = OUTPUT_SHA384_B64,
    [QCRYPTO_HASH_ALG_SHA512] = OUTPUT_SHA512_B64,
    [QCRYPTO_HASH_ALG_RIPEMD160] = OUTPUT_RIPEMD160_B64,
};
static const int expected_lens[] = {
    [QCRYPTO_HASH_ALG_MD5] = 16,
    [QCRYPTO_HASH_ALG_SHA1] = 20,
    [QCRYPTO_HASH_ALG_SHA224] = 28,
    [QCRYPTO_HASH_ALG_SHA256] = 32,
    [QCRYPTO_HASH_ALG_SHA384] = 48,
    [QCRYPTO_HASH_ALG_SHA512] = 64,
    [QCRYPTO_HASH_ALG_RIPEMD160] = 20,
};

static const char hex[] = "0123456789abcdef";

/* Test with dynamic allocation */
static void test_hash_alloc(void)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        uint8_t *result = NULL;
        size_t resultlen = 0;
        int ret;
        size_t j;

        if (!qcrypto_hash_supports(i)) {
            continue;
        }

        ret = qcrypto_hash_bytes(i,
                                 INPUT_TEXT,
                                 strlen(INPUT_TEXT),
                                 &result,
                                 &resultlen,
                                 &error_fatal);
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

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        uint8_t *result;
        size_t resultlen;
        int ret;
        size_t j;

        if (!qcrypto_hash_supports(i)) {
            continue;
        }

        resultlen = expected_lens[i];
        result = g_new0(uint8_t, resultlen);

        ret = qcrypto_hash_bytes(i,
                                 INPUT_TEXT,
                                 strlen(INPUT_TEXT),
                                 &result,
                                 &resultlen,
                                 &error_fatal);
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

        if (!qcrypto_hash_supports(i)) {
            continue;
        }

        ret = qcrypto_hash_bytesv(i,
                                  iov, 3,
                                  &result,
                                  &resultlen,
                                  &error_fatal);
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

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        int ret;
        char *digest;
        size_t digestsize;

        if (!qcrypto_hash_supports(i)) {
            continue;
        }

        digestsize = qcrypto_hash_digest_len(i);

        g_assert_cmpint(digestsize * 2, ==, strlen(expected_outputs[i]));

        ret = qcrypto_hash_digest(i,
                                  INPUT_TEXT,
                                  strlen(INPUT_TEXT),
                                  &digest,
                                  &error_fatal);
        g_assert(ret == 0);
        g_assert_cmpstr(digest, ==, expected_outputs[i]);
        g_free(digest);
    }
}

/* Test with base64 encoding */
static void test_hash_base64(void)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS(expected_outputs) ; i++) {
        int ret;
        char *digest;

        if (!qcrypto_hash_supports(i)) {
            continue;
        }

        ret = qcrypto_hash_base64(i,
                                  INPUT_TEXT,
                                  strlen(INPUT_TEXT),
                                  &digest,
                                  &error_fatal);
        g_assert(ret == 0);
        g_assert_cmpstr(digest, ==, expected_outputs_b64[i]);
        g_free(digest);
    }
}

int main(int argc, char **argv)
{
    int ret = qcrypto_init(&error_fatal);
    g_assert(ret == 0);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/crypto/hash/iov", test_hash_iov);
    g_test_add_func("/crypto/hash/alloc", test_hash_alloc);
    g_test_add_func("/crypto/hash/prealloc", test_hash_prealloc);
    g_test_add_func("/crypto/hash/digest", test_hash_digest);
    g_test_add_func("/crypto/hash/base64", test_hash_base64);
    return g_test_run();
}
