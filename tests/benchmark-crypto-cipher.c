/*
 * QEMU Crypto cipher speed benchmark
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "crypto/init.h"
#include "crypto/cipher.h"

static void test_cipher_speed(size_t chunk_size,
                              QCryptoCipherMode mode,
                              QCryptoCipherAlgorithm alg)
{
    QCryptoCipher *cipher;
    Error *err = NULL;
    double total = 0.0;
    uint8_t *key = NULL, *iv = NULL;
    uint8_t *plaintext = NULL, *ciphertext = NULL;
    size_t nkey;
    size_t niv;

    if (!qcrypto_cipher_supports(alg, mode)) {
        return;
    }

    nkey = qcrypto_cipher_get_key_len(alg);
    niv = qcrypto_cipher_get_iv_len(alg, mode);
    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        nkey *= 2;
    }

    key = g_new0(uint8_t, nkey);
    memset(key, g_test_rand_int(), nkey);

    iv = g_new0(uint8_t, niv);
    memset(iv, g_test_rand_int(), niv);

    ciphertext = g_new0(uint8_t, chunk_size);

    plaintext = g_new0(uint8_t, chunk_size);
    memset(plaintext, g_test_rand_int(), chunk_size);

    cipher = qcrypto_cipher_new(alg, mode,
                                key, nkey, &err);
    g_assert(cipher != NULL);

    if (mode != QCRYPTO_CIPHER_MODE_ECB)
        g_assert(qcrypto_cipher_setiv(cipher,
                                      iv, niv,
                                      &err) == 0);

    g_test_timer_start();
    do {
        g_assert(qcrypto_cipher_encrypt(cipher,
                                        plaintext,
                                        ciphertext,
                                        chunk_size,
                                        &err) == 0);
        total += chunk_size;
    } while (g_test_timer_elapsed() < 1.0);

    total /= MiB;
    g_print("Enc chunk %zu bytes ", chunk_size);
    g_print("%.2f MB/sec ", total / g_test_timer_last());

    total = 0.0;
    g_test_timer_start();
    do {
        g_assert(qcrypto_cipher_decrypt(cipher,
                                        plaintext,
                                        ciphertext,
                                        chunk_size,
                                        &err) == 0);
        total += chunk_size;
    } while (g_test_timer_elapsed() < 1.0);

    total /= MiB;
    g_print("Dec chunk %zu bytes ", chunk_size);
    g_print("%.2f MB/sec ", total / g_test_timer_last());

    qcrypto_cipher_free(cipher);
    g_free(plaintext);
    g_free(ciphertext);
    g_free(iv);
    g_free(key);
}


static void test_cipher_speed_ecb_aes_128(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_ECB,
                      QCRYPTO_CIPHER_ALG_AES_128);
}

static void test_cipher_speed_ecb_aes_256(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_ECB,
                      QCRYPTO_CIPHER_ALG_AES_256);
}

static void test_cipher_speed_cbc_aes_128(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_CBC,
                      QCRYPTO_CIPHER_ALG_AES_128);
}

static void test_cipher_speed_cbc_aes_256(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_CBC,
                      QCRYPTO_CIPHER_ALG_AES_256);
}

static void test_cipher_speed_ctr_aes_128(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_CTR,
                      QCRYPTO_CIPHER_ALG_AES_128);
}

static void test_cipher_speed_ctr_aes_256(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_CTR,
                      QCRYPTO_CIPHER_ALG_AES_256);
}

static void test_cipher_speed_xts_aes_128(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_XTS,
                      QCRYPTO_CIPHER_ALG_AES_128);
}

static void test_cipher_speed_xts_aes_256(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    test_cipher_speed(chunk_size,
                      QCRYPTO_CIPHER_MODE_XTS,
                      QCRYPTO_CIPHER_ALG_AES_256);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert(qcrypto_init(NULL) == 0);

#define ADD_TEST(mode, cipher, keysize, chunk)                          \
    g_test_add_data_func(                                               \
        "/crypto/cipher/" #mode "-" #cipher "-" #keysize "/chunk-" #chunk, \
        (void *)chunk,                                                  \
        test_cipher_speed_ ## mode ## _ ## cipher ## _ ## keysize)

#define ADD_TESTS(chunk)                        \
    do {                                        \
        ADD_TEST(ecb, aes, 128, chunk);         \
        ADD_TEST(ecb, aes, 256, chunk);         \
        ADD_TEST(cbc, aes, 128, chunk);         \
        ADD_TEST(cbc, aes, 256, chunk);         \
        ADD_TEST(ctr, aes, 128, chunk);         \
        ADD_TEST(ctr, aes, 256, chunk);         \
        ADD_TEST(xts, aes, 128, chunk);         \
        ADD_TEST(xts, aes, 256, chunk);         \
    } while (0)

    ADD_TESTS(512);
    ADD_TESTS(4096);
    ADD_TESTS(16384);
    ADD_TESTS(65536);

    return g_test_run();
}
