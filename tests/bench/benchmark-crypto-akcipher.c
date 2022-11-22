/*
 * QEMU Crypto akcipher speed benchmark
 *
 * Copyright (c) 2022 Bytedance
 *
 * Authors:
 *    lei he <helei.sig11@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "crypto/init.h"
#include "crypto/akcipher.h"
#include "standard-headers/linux/virtio_crypto.h"

#include "test_akcipher_keys.inc"

static QCryptoAkCipher *create_rsa_akcipher(const uint8_t *priv_key,
                                            size_t keylen,
                                            QCryptoRSAPaddingAlgorithm padding,
                                            QCryptoHashAlgorithm hash)
{
    QCryptoAkCipherOptions opt;

    opt.alg = QCRYPTO_AKCIPHER_ALG_RSA;
    opt.u.rsa.padding_alg = padding;
    opt.u.rsa.hash_alg = hash;
    return qcrypto_akcipher_new(&opt, QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE,
                                priv_key, keylen, &error_abort);
}

static void test_rsa_speed(const uint8_t *priv_key, size_t keylen,
                           size_t key_size)
{
#define BYTE 8
#define SHA1_DGST_LEN 20
#define SIGN_TIMES 10000
#define VERIFY_TIMES 100000
#define PADDING QCRYPTO_RSA_PADDING_ALG_PKCS1
#define HASH QCRYPTO_HASH_ALG_SHA1

    g_autoptr(QCryptoAkCipher) rsa =
        create_rsa_akcipher(priv_key, keylen, PADDING, HASH);
    g_autofree uint8_t *dgst = NULL;
    g_autofree uint8_t *signature = NULL;
    size_t count;

    dgst = g_new0(uint8_t, SHA1_DGST_LEN);
    memset(dgst, g_test_rand_int(), SHA1_DGST_LEN);
    signature = g_new0(uint8_t, key_size / BYTE);

    g_test_message("benchmark rsa%zu (%s-%s) sign...", key_size,
                   QCryptoRSAPaddingAlgorithm_str(PADDING),
                   QCryptoHashAlgorithm_str(HASH));
    g_test_timer_start();
    for (count = 0; count < SIGN_TIMES; ++count) {
        g_assert(qcrypto_akcipher_sign(rsa, dgst, SHA1_DGST_LEN,
                                       signature, key_size / BYTE,
                                       &error_abort) > 0);
    }
    g_test_timer_elapsed();
    g_test_message("rsa%zu (%s-%s) sign %zu times in %.2f seconds,"
                   " %.2f times/sec ",
                   key_size,  QCryptoRSAPaddingAlgorithm_str(PADDING),
                   QCryptoHashAlgorithm_str(HASH),
                   count, g_test_timer_last(),
                   (double)count / g_test_timer_last());

    g_test_message("benchmark rsa%zu (%s-%s) verification...", key_size,
                   QCryptoRSAPaddingAlgorithm_str(PADDING),
                   QCryptoHashAlgorithm_str(HASH));
    g_test_timer_start();
    for (count = 0; count < VERIFY_TIMES; ++count) {
        g_assert(qcrypto_akcipher_verify(rsa, signature, key_size / BYTE,
                                         dgst, SHA1_DGST_LEN,
                                         &error_abort) == 0);
    }
    g_test_timer_elapsed();
    g_test_message("rsa%zu (%s-%s) verify %zu times in %.2f seconds,"
                   " %.2f times/sec ",
                   key_size, QCryptoRSAPaddingAlgorithm_str(PADDING),
                   QCryptoHashAlgorithm_str(HASH),
                   count, g_test_timer_last(),
                   (double)count / g_test_timer_last());
}

static void test_rsa_1024_speed(const void *opaque)
{
    size_t key_size = (size_t)opaque;
    test_rsa_speed(rsa1024_priv_key, sizeof(rsa1024_priv_key), key_size);
}

static void test_rsa_2048_speed(const void *opaque)
{
    size_t key_size = (size_t)opaque;
    test_rsa_speed(rsa2048_priv_key, sizeof(rsa2048_priv_key), key_size);
}

static void test_rsa_4096_speed(const void *opaque)
{
    size_t key_size = (size_t)opaque;
    test_rsa_speed(rsa4096_priv_key, sizeof(rsa4096_priv_key), key_size);
}

int main(int argc, char **argv)
{
    char *alg = NULL;
    char *size = NULL;
    g_test_init(&argc, &argv, NULL);
    g_assert(qcrypto_init(NULL) == 0);

#define ADD_TEST(asym_alg, keysize)                    \
    if ((!alg || g_str_equal(alg, #asym_alg)) &&       \
        (!size || g_str_equal(size, #keysize)))        \
        g_test_add_data_func(                          \
        "/crypto/akcipher/" #asym_alg "-" #keysize,    \
        (void *)keysize,                               \
        test_ ## asym_alg ## _ ## keysize ## _speed)

    if (argc >= 2) {
        alg = argv[1];
    }
    if (argc >= 3) {
        size = argv[2];
    }

    ADD_TEST(rsa, 1024);
    ADD_TEST(rsa, 2048);
    ADD_TEST(rsa, 4096);

    return g_test_run();
}
