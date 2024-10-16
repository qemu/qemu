/*
 * QEMU Crypto block encryption
 *
 * Copyright (c) 2016 Red Hat, Inc.
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
#include "crypto/block.h"
#include "crypto/block-luks-priv.h"
#include "qemu/buffer.h"
#include "qemu/module.h"
#include "crypto/secret.h"
#ifndef _WIN32
#include <sys/resource.h>
#endif

#if (defined(_WIN32) || defined RUSAGE_THREAD) && \
    (defined(CONFIG_NETTLE) || defined(CONFIG_GCRYPT) || \
     defined(CONFIG_GNUTLS_CRYPTO))
#define TEST_LUKS
#else
#undef TEST_LUKS
#endif

static QCryptoBlockCreateOptions qcow_create_opts = {
    .format = QCRYPTO_BLOCK_FORMAT_QCOW,
    .u.qcow = {
        .key_secret = (char *)"sec0",
    },
};

static QCryptoBlockOpenOptions qcow_open_opts = {
    .format = QCRYPTO_BLOCK_FORMAT_QCOW,
    .u.qcow = {
        .key_secret = (char *)"sec0",
    },
};


#ifdef TEST_LUKS
static QCryptoBlockOpenOptions luks_open_opts = {
    .format = QCRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .key_secret = (char *)"sec0",
    },
};


/* Creation with all default values */
static QCryptoBlockCreateOptions luks_create_opts_default = {
    .format = QCRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .key_secret = (char *)"sec0",
    },
};


/* ...and with explicit values */
static QCryptoBlockCreateOptions luks_create_opts_aes256_cbc_plain64 = {
    .format = QCRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .key_secret = (char *)"sec0",
        .has_cipher_alg = true,
        .cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256,
        .has_cipher_mode = true,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .has_ivgen_alg = true,
        .ivgen_alg = QCRYPTO_IV_GEN_ALGO_PLAIN64,
    },
};


static QCryptoBlockCreateOptions luks_create_opts_aes256_cbc_essiv = {
    .format = QCRYPTO_BLOCK_FORMAT_LUKS,
    .u.luks = {
        .key_secret = (char *)"sec0",
        .has_cipher_alg = true,
        .cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256,
        .has_cipher_mode = true,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .has_ivgen_alg = true,
        .ivgen_alg = QCRYPTO_IV_GEN_ALGO_ESSIV,
        .has_ivgen_hash_alg = true,
        .ivgen_hash_alg = QCRYPTO_HASH_ALGO_SHA256,
        .has_hash_alg = true,
        .hash_alg = QCRYPTO_HASH_ALGO_SHA1,
    },
};
#endif /* TEST_LUKS */


static struct QCryptoBlockTestData {
    const char *path;
    QCryptoBlockCreateOptions *create_opts;
    QCryptoBlockOpenOptions *open_opts;

    bool expect_header;

    QCryptoCipherAlgo cipher_alg;
    QCryptoCipherMode cipher_mode;
    QCryptoHashAlgo hash_alg;

    QCryptoIVGenAlgo ivgen_alg;
    QCryptoHashAlgo ivgen_hash;

    bool slow;
} test_data[] = {
    {
        .path = "/crypto/block/qcow",
        .create_opts = &qcow_create_opts,
        .open_opts = &qcow_open_opts,

        .expect_header = false,

        .cipher_alg = QCRYPTO_CIPHER_ALGO_AES_128,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,

        .ivgen_alg = QCRYPTO_IV_GEN_ALGO_PLAIN64,
    },
#ifdef TEST_LUKS
    {
        .path = "/crypto/block/luks/default",
        .create_opts = &luks_create_opts_default,
        .open_opts = &luks_open_opts,

        .expect_header = true,

        .cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256,
        .cipher_mode = QCRYPTO_CIPHER_MODE_XTS,
        .hash_alg = QCRYPTO_HASH_ALGO_SHA256,

        .ivgen_alg = QCRYPTO_IV_GEN_ALGO_PLAIN64,

        .slow = true,
    },
    {
        .path = "/crypto/block/luks/aes-256-cbc-plain64",
        .create_opts = &luks_create_opts_aes256_cbc_plain64,
        .open_opts = &luks_open_opts,

        .expect_header = true,

        .cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .hash_alg = QCRYPTO_HASH_ALGO_SHA256,

        .ivgen_alg = QCRYPTO_IV_GEN_ALGO_PLAIN64,

        .slow = true,
    },
    {
        .path = "/crypto/block/luks/aes-256-cbc-essiv",
        .create_opts = &luks_create_opts_aes256_cbc_essiv,
        .open_opts = &luks_open_opts,

        .expect_header = true,

        .cipher_alg = QCRYPTO_CIPHER_ALGO_AES_256,
        .cipher_mode = QCRYPTO_CIPHER_MODE_CBC,
        .hash_alg = QCRYPTO_HASH_ALGO_SHA1,

        .ivgen_alg = QCRYPTO_IV_GEN_ALGO_ESSIV,
        .ivgen_hash = QCRYPTO_HASH_ALGO_SHA256,

        .slow = true,
    },
#endif
};


static int test_block_read_func(QCryptoBlock *block,
                                size_t offset,
                                uint8_t *buf,
                                size_t buflen,
                                void *opaque,
                                Error **errp)
{
    Buffer *header = opaque;

    g_assert_cmpint(offset + buflen, <=, header->capacity);

    memcpy(buf, header->buffer + offset, buflen);

    return 0;
}


static int test_block_init_func(QCryptoBlock *block,
                                size_t headerlen,
                                void *opaque,
                                Error **errp)
{
    Buffer *header = opaque;

    g_assert_cmpint(header->capacity, ==, 0);

    buffer_reserve(header, headerlen);

    return 0;
}


static int test_block_write_func(QCryptoBlock *block,
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

    return 0;
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
                               0,
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
                             NULL);
    g_assert(blk == NULL);

    /* Ensure we can't open without the secret, unless NO_IO */
    blk = qcrypto_block_open(data->open_opts, NULL,
                             test_block_read_func,
                             &header,
                             QCRYPTO_BLOCK_OPEN_NO_IO,
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
                             &error_abort);
    g_assert(blk);

    test_block_assert_setup(data, blk);

    qcrypto_block_free(blk);

    object_unparent(sec);

    buffer_free(&header);
}


#ifdef TEST_LUKS
typedef const char *(*LuksHeaderDoBadStuff)(QCryptoBlockLUKSHeader *hdr);

static void
test_luks_bad_header(gconstpointer data)
{
    LuksHeaderDoBadStuff badstuff = data;
    QCryptoBlock *blk;
    Buffer buf;
    Object *sec = test_block_secret();
    QCryptoBlockLUKSHeader hdr;
    Error *err = NULL;
    const char *msg;

    memset(&buf, 0, sizeof(buf));
    buffer_init(&buf, "header");

    /* Correctly create the volume initially */
    blk = qcrypto_block_create(&luks_create_opts_default, NULL,
                               test_block_init_func,
                               test_block_write_func,
                               &buf,
                               0,
                               &error_abort);
    g_assert(blk);

    qcrypto_block_free(blk);

    /* Mangle it in some unpleasant way */
    g_assert(buf.offset >= sizeof(hdr));
    memcpy(&hdr, buf.buffer, sizeof(hdr));
    qcrypto_block_luks_to_disk_endian(&hdr);

    msg = badstuff(&hdr);

    qcrypto_block_luks_from_disk_endian(&hdr);
    memcpy(buf.buffer, &hdr, sizeof(hdr));

    /* Check that we fail to open it again */
    blk = qcrypto_block_open(&luks_open_opts, NULL,
                             test_block_read_func,
                             &buf,
                             0,
                             &err);
    g_assert(!blk);
    g_assert(err);

    g_assert_cmpstr(error_get_pretty(err), ==, msg);
    error_free(err);

    object_unparent(sec);

    buffer_free(&buf);
}

static const char *luks_bad_null_term_cipher_name(QCryptoBlockLUKSHeader *hdr)
{
    /* Replace NUL termination with spaces */
    char *offset = hdr->cipher_name + strlen(hdr->cipher_name);
    memset(offset, ' ', sizeof(hdr->cipher_name) - (offset - hdr->cipher_name));

    return "LUKS header cipher name is not NUL terminated";
}

static const char *luks_bad_null_term_cipher_mode(QCryptoBlockLUKSHeader *hdr)
{
    /* Replace NUL termination with spaces */
    char *offset = hdr->cipher_mode + strlen(hdr->cipher_mode);
    memset(offset, ' ', sizeof(hdr->cipher_mode) - (offset - hdr->cipher_mode));

    return "LUKS header cipher mode is not NUL terminated";
}

static const char *luks_bad_null_term_hash_spec(QCryptoBlockLUKSHeader *hdr)
{
    /* Replace NUL termination with spaces */
    char *offset = hdr->hash_spec + strlen(hdr->hash_spec);
    memset(offset, ' ', sizeof(hdr->hash_spec) - (offset - hdr->hash_spec));

    return "LUKS header hash spec is not NUL terminated";
}

static const char *luks_bad_cipher_name_empty(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_name, "", 1);

    return "Algorithm '' with key size 32 bytes not supported";
}

static const char *luks_bad_cipher_name_unknown(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_name, "aess", 5);

    return "Algorithm 'aess' with key size 32 bytes not supported";
}

static const char *luks_bad_cipher_xts_size(QCryptoBlockLUKSHeader *hdr)
{
    hdr->master_key_len = 33;

    return "XTS cipher key length should be a multiple of 2";
}

static const char *luks_bad_cipher_cbc_size(QCryptoBlockLUKSHeader *hdr)
{
    hdr->master_key_len = 33;
    memcpy(hdr->cipher_mode, "cbc-essiv", 10);

    return "Algorithm 'aes' with key size 33 bytes not supported";
}

static const char *luks_bad_cipher_mode_empty(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "", 1);

    return "Unexpected cipher mode string format ''";
}

static const char *luks_bad_cipher_mode_unknown(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "xfs", 4);

    return "Unexpected cipher mode string format 'xfs'";
}

static const char *luks_bad_ivgen_separator(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "xts:plain64", 12);

    return "Unexpected cipher mode string format 'xts:plain64'";
}

static const char *luks_bad_ivgen_name_empty(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "xts-", 5);

    return "IV generator '' not supported";
}

static const char *luks_bad_ivgen_name_unknown(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "xts-plain65", 12);

    return "IV generator 'plain65' not supported";
}

static const char *luks_bad_ivgen_hash_empty(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "xts-plain65:", 13);

    return "Hash algorithm '' not supported";
}

static const char *luks_bad_ivgen_hash_unknown(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->cipher_mode, "xts-plain65:sha257", 19);

    return "Hash algorithm 'sha257' not supported";
}

static const char *luks_bad_hash_spec_empty(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->hash_spec, "", 1);

    return "Hash algorithm '' not supported";
}

static const char *luks_bad_hash_spec_unknown(QCryptoBlockLUKSHeader *hdr)
{
    memcpy(hdr->hash_spec, "sha2566", 8);

    return "Hash algorithm 'sha2566' not supported";
}

static const char *luks_bad_stripes(QCryptoBlockLUKSHeader *hdr)
{
    hdr->key_slots[0].stripes = 3999;

    return "Keyslot 0 is corrupted (stripes 3999 != 4000)";
}

static const char *luks_bad_key_overlap_header(QCryptoBlockLUKSHeader *hdr)
{
    hdr->key_slots[0].key_offset_sector = 2;

    return "Keyslot 0 is overlapping with the LUKS header";
}

static const char *luks_bad_key_overlap_key(QCryptoBlockLUKSHeader *hdr)
{
    hdr->key_slots[0].key_offset_sector = hdr->key_slots[1].key_offset_sector;

    return "Keyslots 0 and 1 are overlapping in the header";
}

static const char *luks_bad_key_overlap_payload(QCryptoBlockLUKSHeader *hdr)
{
    hdr->key_slots[0].key_offset_sector = hdr->payload_offset_sector + 42;

    return "Keyslot 0 is overlapping with the encrypted payload";
}

static const char *luks_bad_payload_overlap_header(QCryptoBlockLUKSHeader *hdr)
{
    hdr->payload_offset_sector = 2;

    return "LUKS payload is overlapping with the header";
}

static const char *luks_bad_key_iterations(QCryptoBlockLUKSHeader *hdr)
{
    hdr->key_slots[0].iterations = 0;

    return "Keyslot 0 iteration count is zero";
}

static const char *luks_bad_iterations(QCryptoBlockLUKSHeader *hdr)
{
    hdr->master_key_iterations = 0;

    return "LUKS key iteration count is zero";
}
#endif

int main(int argc, char **argv)
{
    gsize i;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    for (i = 0; i < G_N_ELEMENTS(test_data); i++) {
        if (test_data[i].open_opts->format == QCRYPTO_BLOCK_FORMAT_LUKS &&
            !qcrypto_hash_supports(test_data[i].hash_alg)) {
            continue;
        }
        if (!test_data[i].slow ||
            g_test_slow()) {
            g_test_add_data_func(test_data[i].path, &test_data[i], test_block);
        }
    }

#ifdef TEST_LUKS
    if (g_test_slow()) {
        g_test_add_data_func("/crypto/block/luks/bad/cipher-name-nul-term",
                             luks_bad_null_term_cipher_name,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-mode-nul-term",
                             luks_bad_null_term_cipher_mode,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/hash-spec-nul-term",
                             luks_bad_null_term_hash_spec,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-name-empty",
                             luks_bad_cipher_name_empty,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-name-unknown",
                             luks_bad_cipher_name_unknown,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-xts-size",
                             luks_bad_cipher_xts_size,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-cbc-size",
                             luks_bad_cipher_cbc_size,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-mode-empty",
                             luks_bad_cipher_mode_empty,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/cipher-mode-unknown",
                             luks_bad_cipher_mode_unknown,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/ivgen-separator",
                             luks_bad_ivgen_separator,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/ivgen-name-empty",
                             luks_bad_ivgen_name_empty,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/ivgen-name-unknown",
                             luks_bad_ivgen_name_unknown,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/ivgen-hash-empty",
                             luks_bad_ivgen_hash_empty,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/ivgen-hash-unknown",
                             luks_bad_ivgen_hash_unknown,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/hash-spec-empty",
                             luks_bad_hash_spec_empty,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/hash-spec-unknown",
                             luks_bad_hash_spec_unknown,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/stripes",
                             luks_bad_stripes,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/key-overlap-header",
                             luks_bad_key_overlap_header,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/key-overlap-key",
                             luks_bad_key_overlap_key,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/key-overlap-payload",
                             luks_bad_key_overlap_payload,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/payload-overlap-header",
                             luks_bad_payload_overlap_header,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/iterations",
                             luks_bad_iterations,
                             test_luks_bad_header);
        g_test_add_data_func("/crypto/block/luks/bad/key-iterations",
                             luks_bad_key_iterations,
                             test_luks_bad_header);
    }
#endif

    return g_test_run();
}
