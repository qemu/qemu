/*
 * QEMU Crypto block encryption
 *
 * Copyright (c) 2016 Red Hat, Inc.
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
#include "crypto/init.h"
#include "crypto/block.h"
#include "qemu/buffer.h"
#include "qemu/module.h"
#include "crypto/secret.h"
#ifndef _WIN32
#include <sys/resource.h>
#endif

#if (defined(_WIN32) || defined RUSAGE_THREAD) && \
    (defined(CONFIG_NETTLE) || defined(CONFIG_GCRYPT))
#define TEST_LUKS
#else
#undef TEST_LUKS
#endif

static QCryptoBlockCreateOptions qcow_create_opts = {
    .format = Q_CRYPTO_BLOCK_FORMAT_QCOW,
    .u.qcow = {
        .has_key_secret = true,
        .key_secret = (char *)"sec0",
    },
};

static QCryptoBlockOpenOptions qcow_open_opts = {
    .format = Q_CRYPTO_BLOCK_FORMAT_QCOW,
    .u.qcow = {
        .has_key_secret = true,
        .key_secret = (char *)"sec0",
    },
};


#ifdef TEST_LUKS
static QCryptoBlockOpenOptions luks_open_opts = {
    .format = Q_CRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .has_key_secret = true,
        .key_secret = (char *)"sec0",
    },
};


/* Creation with all default values */
static QCryptoBlockCreateOptions luks_create_opts_default = {
    .format = Q_CRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .has_key_secret = true,
        .key_secret = (char *)"sec0",
    },
};


/* ...and with explicit values */
static QCryptoBlockCreateOptions luks_create_opts_aes256_cbc_plain64 = {
    .format = Q_CRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .has_key_secret = true,
        .key_secret = (char *)"sec0",
        .has_cipher_alg = true,
        .cipher_alg = QCRYPTO_CIPHER_ALG_AES_256,
        .has_cipher_mode = true,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .has_ivgen_alg = true,
        .ivgen_alg = QCRYPTO_IVGEN_ALG_PLAIN64,
    },
};


static QCryptoBlockCreateOptions luks_create_opts_aes256_cbc_essiv = {
    .format = Q_CRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .has_key_secret = true,
        .key_secret = (char *)"sec0",
        .has_cipher_alg = true,
        .cipher_alg = QCRYPTO_CIPHER_ALG_AES_256,
        .has_cipher_mode = true,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .has_ivgen_alg = true,
        .ivgen_alg = QCRYPTO_IVGEN_ALG_ESSIV,
        .has_ivgen_hash_alg = true,
        .ivgen_hash_alg = QCRYPTO_HASH_ALG_SHA256,
        .has_hash_alg = true,
        .hash_alg = QCRYPTO_HASH_ALG_SHA1,
    },
};
#endif /* TEST_LUKS */


static struct QCryptoBlockTestData {
    const char *path;
    QCryptoBlockCreateOptions *create_opts;
    QCryptoBlockOpenOptions *open_opts;

    bool expect_header;

    QCryptoCipherAlgorithm cipher_alg;
    QCryptoCipherMode cipher_mode;
    QCryptoHashAlgorithm hash_alg;

    QCryptoIVGenAlgorithm ivgen_alg;
    QCryptoHashAlgorithm ivgen_hash;

    bool slow;
} test_data[] = {
    {
        .path = "/crypto/block/qcow",
        .create_opts = &qcow_create_opts,
        .open_opts = &qcow_open_opts,

        .expect_header = false,

        .cipher_alg = QCRYPTO_CIPHER_ALG_AES_128,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,

        .ivgen_alg = QCRYPTO_IVGEN_ALG_PLAIN64,
    },
#ifdef TEST_LUKS
    {
        .path = "/crypto/block/luks/default",
        .create_opts = &luks_create_opts_default,
        .open_opts = &luks_open_opts,

        .expect_header = true,

        .cipher_alg = QCRYPTO_CIPHER_ALG_AES_256,
        .cipher_mode = QCRYPTO_CIPHER_MODE_XTS,
        .hash_alg = QCRYPTO_HASH_ALG_SHA256,

        .ivgen_alg = QCRYPTO_IVGEN_ALG_PLAIN64,

        .slow = true,
    },
    {
        .path = "/crypto/block/luks/aes-256-cbc-plain64",
        .create_opts = &luks_create_opts_aes256_cbc_plain64,
        .open_opts = &luks_open_opts,

        .expect_header = true,

        .cipher_alg = QCRYPTO_CIPHER_ALG_AES_256,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .hash_alg = QCRYPTO_HASH_ALG_SHA256,

        .ivgen_alg = QCRYPTO_IVGEN_ALG_PLAIN64,

        .slow = true,
    },
    {
        .path = "/crypto/block/luks/aes-256-cbc-essiv",
        .create_opts = &luks_create_opts_aes256_cbc_essiv,
        .open_opts = &luks_open_opts,

        .expect_header = true,

        .cipher_alg = QCRYPTO_CIPHER_ALG_AES_256,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .hash_alg = QCRYPTO_HASH_ALG_SHA1,

        .ivgen_alg = QCRYPTO_IVGEN_ALG_ESSIV,
        .ivgen_hash = QCRYPTO_HASH_ALG_SHA256,

        .slow = true,
    },
#endif
};


static ssize_t test_block_read_func(QCryptoBlock *block,
                                    size_t offset,
                                    uint8_t *buf,
                                    size_t buflen,
                                    void *opaque,
                                    Error **errp)
{
    Buffer *header = opaque;

    g_assert_cmpint(offset + buflen, <=, header->capacity);

    memcpy(buf, header->buffer + offset, buflen);

    return buflen;
}


static ssize_t test_block_init_func(QCryptoBlock *block,
                                    size_t headerlen,
                                    void *opaque,
                                    Error **errp)
{
    Buffer *header = opaque;

    g_assert_cmpint(header->capacity, ==, 0);

    buffer_reserve(header, headerlen);

    return headerlen;
}


static ssize_t test_block_write_func(QCryptoBlock *block,
                                     size_t offset,
                                     const uint8_t *buf,
                                     size_t buflen,
                                     void *opaque,
                                     Error **errp)
{
    Buffer *header = opaque;

    g_assert_cmpint(buflen + offset, <=, header->capacity);

    memcpy(header->buffer + offset, buf, buflen);
    header->offset = offset + buflen;

    return buflen;
}


static Object *test_block_secret(void)
{
    return object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "123456",
        NULL);
}

static void test_block_assert_setup(const struct QCryptoBlockTestData *data,
                                    QCryptoBlock *blk)
{
    QCryptoIVGen *ivgen;
    QCryptoCipher *cipher;

    ivgen = qcrypto_block_get_ivgen(blk);
    cipher = qcrypto_block_get_cipher(blk);

    g_assert(ivgen);
    g_assert(cipher);

    g_assert_cmpint(data->cipher_alg, ==, cipher->alg);
    g_assert_cmpint(data->cipher_mode, ==, cipher->mode);
    g_assert_cmpint(data->hash_alg, ==,
                    qcrypto_block_get_kdf_hash(blk));

    g_assert_cmpint(data->ivgen_alg, ==,
                    qcrypto_ivgen_get_algorithm(ivgen));
    g_assert_cmpint(data->ivgen_hash, ==,
                    qcrypto_ivgen_get_hash(ivgen));
}


static void test_block(gconstpointer opaque)
{
    const struct QCryptoBlockTestData *data = opaque;
    QCryptoBlock *blk;
    Buffer header;
    Object *sec = test_block_secret();

    memset(&header, 0, sizeof(header));
    buffer_init(&header, "header");

    blk = qcrypto_block_create(data->create_opts, NULL,
                               test_block_init_func,
                               test_block_write_func,
                               &header,
                               &error_abort);
    g_assert(blk);

    if (data->expect_header) {
        g_assert_cmpint(header.capacity, >, 0);
    } else {
        g_assert_cmpint(header.capacity, ==, 0);
    }

    test_block_assert_setup(data, blk);

    qcrypto_block_free(blk);
    object_unparent(sec);

    /* Ensure we can't open without the secret */
    blk = qcrypto_block_open(data->open_opts, NULL,
                             test_block_read_func,
                             &header,
                             0,
                             1,
                             NULL);
    g_assert(blk == NULL);

    /* Ensure we can't open without the secret, unless NO_IO */
    blk = qcrypto_block_open(data->open_opts, NULL,
                             test_block_read_func,
                             &header,
                             QCRYPTO_BLOCK_OPEN_NO_IO,
                             1,
                             &error_abort);

    g_assert(qcrypto_block_get_cipher(blk) == NULL);
    g_assert(qcrypto_block_get_ivgen(blk) == NULL);

    qcrypto_block_free(blk);


    /* Now open for real with secret */
    sec = test_block_secret();
    blk = qcrypto_block_open(data->open_opts, NULL,
                             test_block_read_func,
                             &header,
                             0,
                             1,
                             &error_abort);
    g_assert(blk);

    test_block_assert_setup(data, blk);

    qcrypto_block_free(blk);

    object_unparent(sec);

    buffer_free(&header);
}


int main(int argc, char **argv)
{
    gsize i;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        if (test_data[i].open_opts->format == Q_CRYPTO_BLOCK_FORMAT_LUKS &&
            !qcrypto_hash_supports(test_data[i].hash_alg)) {
            continue;
        }
        if (!test_data[i].slow ||
            g_test_slow()) {
            g_test_add_data_func(test_data[i].path, &test_data[i], test_block);
        }
    }

    return g_test_run();
}
