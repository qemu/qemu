/*
 * QEMU Crypto cipher algorithms
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
#ifndef _WIN32
#include <sys/resource.h>
#endif

#if ((defined(CONFIG_NETTLE) || defined(CONFIG_GCRYPT)) && \
     (defined(_WIN32) || defined(RUSAGE_THREAD)))
#include "crypto/pbkdf.h"

typedef struct QCryptoPbkdfTestData QCryptoPbkdfTestData;
struct QCryptoPbkdfTestData {
    const char *path;
    QCryptoHashAlgorithm hash;
    unsigned int iterations;
    const char *key;
    size_t nkey;
    const char *salt;
    size_t nsalt;
    const char *out;
    size_t nout;
    bool slow;
};

/* This test data comes from cryptsetup package
 *
 *  $SRC/lib/crypto_backend/pbkdf2_generic.c
 *
 * under LGPLv2.1+ license
 */
static QCryptoPbkdfTestData test_data[] = {
    /* RFC 3962 test data */
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter1",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 1,
        .key = "password",
        .nkey = 8,
        .salt = "ATHENA.MIT.EDUraeburn",
        .nsalt = 21,
        .out = "\xcd\xed\xb5\x28\x1b\xb2\xf8\x01"
               "\x56\x5a\x11\x22\xb2\x56\x35\x15"
               "\x0a\xd1\xf7\xa0\x4b\xb9\xf3\xa3"
               "\x33\xec\xc0\xe2\xe1\xf7\x08\x37",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter2",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 2,
        .key = "password",
        .nkey = 8,
        .salt = "ATHENA.MIT.EDUraeburn",
        .nsalt = 21,
        .out = "\x01\xdb\xee\x7f\x4a\x9e\x24\x3e"
               "\x98\x8b\x62\xc7\x3c\xda\x93\x5d"
               "\xa0\x53\x78\xb9\x32\x44\xec\x8f"
               "\x48\xa9\x9e\x61\xad\x79\x9d\x86",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter1200a",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 1200,
        .key = "password",
        .nkey = 8,
        .salt = "ATHENA.MIT.EDUraeburn",
        .nsalt = 21,
        .out = "\x5c\x08\xeb\x61\xfd\xf7\x1e\x4e"
               "\x4e\xc3\xcf\x6b\xa1\xf5\x51\x2b"
               "\xa7\xe5\x2d\xdb\xc5\xe5\x14\x2f"
               "\x70\x8a\x31\xe2\xe6\x2b\x1e\x13",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter5",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 5,
        .key = "password",
        .nkey = 8,
        .salt = "\0224VxxV4\022", /* "\x1234567878563412 */
        .nsalt = 8,
        .out = "\xd1\xda\xa7\x86\x15\xf2\x87\xe6"
               "\xa1\xc8\xb1\x20\xd7\x06\x2a\x49"
               "\x3f\x98\xd2\x03\xe6\xbe\x49\xa6"
               "\xad\xf4\xfa\x57\x4b\x6e\x64\xee",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter1200b",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 64,
        .salt = "pass phrase equals block size",
        .nsalt = 29,
        .out = "\x13\x9c\x30\xc0\x96\x6b\xc3\x2b"
               "\xa5\x5f\xdb\xf2\x12\x53\x0a\xc9"
               "\xc5\xec\x59\xf1\xa4\x52\xf5\xcc"
               "\x9a\xd9\x40\xfe\xa0\x59\x8e\xd1",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter1200c",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 65,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\x9c\xca\xd6\xd4\x68\x77\x0c\xd5"
               "\x1b\x10\xe6\xa6\x87\x21\xbe\x61"
               "\x1a\x8b\x4d\x28\x26\x01\xdb\x3b"
               "\x36\xbe\x92\x46\x91\x5e\xc8\x2a",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/rfc3962/sha1/iter50",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 50,
        .key = "\360\235\204\236", /* g-clef ("\xf09d849e) */
        .nkey = 4,
        .salt = "EXAMPLE.COMpianist",
        .nsalt = 18,
        .out = "\x6b\x9c\xf2\x6d\x45\x45\x5a\x43"
               "\xa5\xb8\xbb\x27\x6a\x40\x3b\x39"
               "\xe7\xfe\x37\xa0\xc4\x1e\x02\xc2"
               "\x81\xff\x30\x69\xe1\xe9\x4f\x52",
        .nout = 32
    },

    /* RFC-6070 test data */
    {
        .path = "/crypto/pbkdf/rfc6070/sha1/iter1",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 1,
        .key = "password",
        .nkey = 8,
        .salt = "salt",
        .nsalt = 4,
        .out = "\x0c\x60\xc8\x0f\x96\x1f\x0e\x71\xf3\xa9"
               "\xb5\x24\xaf\x60\x12\x06\x2f\xe0\x37\xa6",
        .nout = 20
    },
    {
        .path = "/crypto/pbkdf/rfc6070/sha1/iter2",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 2,
        .key = "password",
        .nkey = 8,
        .salt = "salt",
        .nsalt = 4,
        .out = "\xea\x6c\x01\x4d\xc7\x2d\x6f\x8c\xcd\x1e"
               "\xd9\x2a\xce\x1d\x41\xf0\xd8\xde\x89\x57",
        .nout = 20
    },
    {
        .path = "/crypto/pbkdf/rfc6070/sha1/iter4096",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 4096,
        .key = "password",
        .nkey = 8,
        .salt = "salt",
        .nsalt = 4,
        .out = "\x4b\x00\x79\x01\xb7\x65\x48\x9a\xbe\xad"
               "\x49\xd9\x26\xf7\x21\xd0\x65\xa4\x29\xc1",
        .nout = 20
    },
    {
        .path = "/crypto/pbkdf/rfc6070/sha1/iter16777216",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 16777216,
        .key = "password",
        .nkey = 8,
        .salt = "salt",
        .nsalt = 4,
        .out = "\xee\xfe\x3d\x61\xcd\x4d\xa4\xe4\xe9\x94"
               "\x5b\x3d\x6b\xa2\x15\x8c\x26\x34\xe9\x84",
        .nout = 20,
        .slow = true,
    },
    {
        .path = "/crypto/pbkdf/rfc6070/sha1/iter4096a",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 4096,
        .key = "passwordPASSWORDpassword",
        .nkey = 24,
        .salt = "saltSALTsaltSALTsaltSALTsaltSALTsalt",
        .nsalt = 36,
        .out = "\x3d\x2e\xec\x4f\xe4\x1c\x84\x9b\x80\xc8"
               "\xd8\x36\x62\xc0\xe4\x4a\x8b\x29\x1a\x96"
               "\x4c\xf2\xf0\x70\x38",
        .nout = 25
    },
    {
        .path = "/crypto/pbkdf/rfc6070/sha1/iter4096b",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 4096,
        .key = "pass\0word",
        .nkey = 9,
        .salt = "sa\0lt",
        .nsalt = 5,
        .out = "\x56\xfa\x6a\xa7\x55\x48\x09\x9d\xcc\x37"
               "\xd7\xf0\x34\x25\xe0\xc3",
        .nout = 16
    },

    /* non-RFC misc test data */
    {
        /* empty password test. */
        .path = "/crypto/pbkdf/nonrfc/sha1/iter2",
        .hash = QCRYPTO_HASH_ALG_SHA1,
        .iterations = 2,
        .key = "",
        .nkey = 0,
        .salt = "salt",
        .nsalt = 4,
        .out = "\x13\x3a\x4c\xe8\x37\xb4\xd2\x52\x1e\xe2"
               "\xbf\x03\xe1\x1c\x71\xca\x79\x4e\x07\x97",
        .nout = 20
    },
    {
        /* Password exceeds block size test */
        .path = "/crypto/pbkdf/nonrfc/sha256/iter1200",
        .hash = QCRYPTO_HASH_ALG_SHA256,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 65,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\x22\x34\x4b\xc4\xb6\xe3\x26\x75"
               "\xa8\x09\x0f\x3e\xa8\x0b\xe0\x1d"
               "\x5f\x95\x12\x6a\x2c\xdd\xc3\xfa"
               "\xcc\x4a\x5e\x6d\xca\x04\xec\x58",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/nonrfc/sha512/iter1200",
        .hash = QCRYPTO_HASH_ALG_SHA512,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 129,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\x0f\xb2\xed\x2c\x0e\x6e\xfb\x7d"
               "\x7d\x8e\xdd\x58\x01\xb4\x59\x72"
               "\x99\x92\x16\x30\x5e\xa4\x36\x8d"
               "\x76\x14\x80\xf3\xe3\x7a\x22\xb9",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/nonrfc/sha224/iter1200",
        .hash = QCRYPTO_HASH_ALG_SHA224,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 129,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\x13\x3b\x88\x0c\x0e\x52\xa2\x41"
               "\x49\x33\x35\xa6\xc3\x83\xae\x23"
               "\xf6\x77\x43\x9e\x5b\x30\x92\x3e"
               "\x4a\x3a\xaa\x24\x69\x3c\xed\x20",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/nonrfc/sha384/iter1200",
        .hash = QCRYPTO_HASH_ALG_SHA384,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 129,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\xfe\xe3\xe1\x84\xc9\x25\x3e\x10"
               "\x47\xc8\x7d\x53\xc6\xa5\xe3\x77"
               "\x29\x41\x76\xbd\x4b\xe3\x9b\xac"
               "\x05\x6c\x11\xdd\x17\xc5\x93\x80",
        .nout = 32
    },
    {
        .path = "/crypto/pbkdf/nonrfc/ripemd160/iter1200",
        .hash = QCRYPTO_HASH_ALG_RIPEMD160,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 129,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\xd6\xcb\xd8\xa7\xdb\x0c\xa2\x2a"
               "\x23\x5e\x47\xaf\xdb\xda\xa8\xef"
               "\xe4\x01\x0d\x6f\xb5\x33\xc8\xbd"
               "\xce\xbf\x91\x14\x8b\x5c\x48\x41",
        .nout = 32
    },
#if 0
    {
        .path = "/crypto/pbkdf/nonrfc/whirlpool/iter1200",
        .hash = QCRYPTO_HASH_ALG_WHIRLPOOL,
        .iterations = 1200,
        .key = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
        .nkey = 65,
        .salt = "pass phrase exceeds block size",
        .nsalt = 30,
        .out = "\x9c\x1c\x74\xf5\x88\x26\xe7\x6a"
               "\x53\x58\xf4\x0c\x39\xe7\x80\x89"
               "\x07\xc0\x31\x19\x9a\x50\xa2\x48"
               "\xf1\xd9\xfe\x78\x64\xe5\x84\x50",
        .nout = 32
    }
#endif
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

static void test_pbkdf(const void *opaque)
{
    const QCryptoPbkdfTestData *data = opaque;
    size_t nout = data->nout;
    uint8_t *out = g_new0(uint8_t, nout);
    gchar *expect, *actual;

    qcrypto_pbkdf2(data->hash,
                   (uint8_t *)data->key, data->nkey,
                   (uint8_t *)data->salt, data->nsalt,
                   data->iterations,
                   (uint8_t *)out, nout,
                   &error_abort);

    expect = hex_string((const uint8_t *)data->out, data->nout);
    actual = hex_string(out, nout);

    g_assert_cmpstr(actual, ==, expect);

    g_free(actual);
    g_free(expect);
    g_free(out);
}


static void test_pbkdf_timing(void)
{
    uint8_t key[32];
    uint8_t salt[32];
    int iters;

    memset(key, 0x5d, sizeof(key));
    memset(salt, 0x7c, sizeof(salt));

    iters = qcrypto_pbkdf2_count_iters(QCRYPTO_HASH_ALG_SHA256,
                                       key, sizeof(key),
                                       salt, sizeof(salt),
                                       32,
                                       &error_abort);

    g_assert(iters >= (1 << 15));
}


int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        if (!test_data[i].slow ||
            g_test_slow()) {
            g_test_add_data_func(test_data[i].path, &test_data[i], test_pbkdf);
        }
    }

    if (g_test_slow()) {
        g_test_add_func("/crypt0/pbkdf/timing", test_pbkdf_timing);
    }

    return g_test_run();
}
#else
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    return g_test_run();
}
#endif
