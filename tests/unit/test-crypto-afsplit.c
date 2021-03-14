/*
 * QEMU Crypto anti-forensic splitter
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
#include "crypto/init.h"
#include "crypto/afsplit.h"

typedef struct QCryptoAFSplitTestData QCryptoAFSplitTestData;
struct QCryptoAFSplitTestData {
    const char *path;
    QCryptoHashAlgorithm hash;
    uint32_t stripes;
    size_t blocklen;
    const uint8_t *key;
    const uint8_t *splitkey;
};

static QCryptoAFSplitTestData test_data[] = {
    {
        .path = "/crypto/afsplit/sha256/5",
        .hash = QCRYPTO_HASH_ALG_SHA256,
        .stripes = 5,
        .blocklen = 32,
        .key = (const uint8_t *)
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
            "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7"
            "\xa8\xa9\xaa\xab\xac\xad\xae\xaf",
        .splitkey = (const uint8_t *)
            "\xfd\xd2\x73\xb1\x7d\x99\x93\x34"
            "\x70\xde\xfa\x07\xc5\xac\x58\xd2"
            "\x30\x67\x2f\x1a\x35\x43\x60\x7d"
            "\x77\x02\xdb\x62\x3c\xcb\x2c\x33"
            "\x48\x08\xb6\xf1\x7c\xa3\x20\xa0"
            "\xad\x2d\x4c\xf3\xcd\x18\x6f\x53"
            "\xf9\xe8\xe7\x59\x27\x3c\xa9\x54"
            "\x61\x87\xb3\xaf\xf6\xf7\x7e\x64"
            "\x86\xaa\x89\x7f\x1f\x9f\xdb\x86"
            "\xf4\xa2\x16\xff\xa3\x4f\x8c\xa1"
            "\x59\xc4\x23\x34\x28\xc4\x77\x71"
            "\x83\xd4\xcd\x8e\x89\x1b\xc7\xc5"
            "\xae\x4d\xa9\xcd\xc9\x72\x85\x70"
            "\x13\x68\x52\x83\xfc\xb8\x11\x72"
            "\xba\x3d\xc6\x4a\x28\xfa\xe2\x86"
            "\x7b\x27\xab\x58\xe1\xa4\xca\xf6"
            "\x9e\xbc\xfe\x0c\x92\x79\xb3\xec"
            "\x1c\x5f\x79\x3b\x0d\x1e\xaa\x1a"
            "\x77\x0f\x70\x19\x4b\xc8\x80\xee"
            "\x27\x7c\x6e\x4a\x91\x96\x5c\xf4"
    },
    {
        .path = "/crypto/afsplit/sha256/5000",
        .hash = QCRYPTO_HASH_ALG_SHA256,
        .stripes = 5000,
        .blocklen = 16,
        .key = (const uint8_t *)
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
    },
    {
        .path = "/crypto/afsplit/sha1/1000",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .stripes = 1000,
        .blocklen = 32,
        .key = (const uint8_t *)
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
            "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7"
            "\xa8\xa9\xaa\xab\xac\xad\xae\xaf",
    },
    {
        .path = "/crypto/afsplit/sha256/big",
        .hash = QCRYPTO_HASH_ALG_SHA256,
        .stripes = 1000,
        .blocklen = 64,
        .key = (const uint8_t *)
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
    },
};


static inline char hex(int i)
{
    if (i < 10) {
        return '0' + i;
    }
    return 'a' + (i - 10);
}

static char *hex_string(const uint8_t *bytes,
                        size_t len)
{
    char *hexstr = g_new0(char, len * 2 + 1);
    size_t i;

    for (i = 0; i < len; i++) {
        hexstr[i * 2] = hex((bytes[i] >> 4) & 0xf);
        hexstr[i * 2 + 1] = hex(bytes[i] & 0xf);
    }
    hexstr[len * 2] = '\0';

    return hexstr;
}

static void test_afsplit(const void *opaque)
{
    const QCryptoAFSplitTestData *data = opaque;
    size_t splitlen = data->blocklen * data->stripes;
    uint8_t *splitkey = g_new0(uint8_t, splitlen);
    uint8_t *key = g_new0(uint8_t, data->blocklen);
    gchar *expect, *actual;

    /* First time we round-trip the key */
    qcrypto_afsplit_encode(data->hash,
                           data->blocklen, data->stripes,
                           data->key, splitkey,
                           &error_abort);

    qcrypto_afsplit_decode(data->hash,
                           data->blocklen, data->stripes,
                           splitkey, key,
                           &error_abort);

    expect = hex_string(data->key, data->blocklen);
    actual = hex_string(key, data->blocklen);

    g_assert_cmpstr(actual, ==, expect);

    g_free(actual);
    g_free(expect);

    /* Second time we merely try decoding a previous split */
    if (data->splitkey) {
        memset(key, 0, data->blocklen);

        qcrypto_afsplit_decode(data->hash,
                               data->blocklen, data->stripes,
                               data->splitkey, key,
                               &error_abort);

        expect = hex_string(data->key, data->blocklen);
        actual = hex_string(key, data->blocklen);

        g_assert_cmpstr(actual, ==, expect);

        g_free(actual);
        g_free(expect);
    }

    g_free(key);
    g_free(splitkey);
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        if (!qcrypto_hash_supports(test_data[i].hash)) {
            continue;
        }
        g_test_add_data_func(test_data[i].path, &test_data[i], test_afsplit);
    }
    return g_test_run();
}
