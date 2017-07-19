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
#include "crypto/init.h"
#include "crypto/cipher.h"

static void test_cipher_speed(const void *opaque)
{
    QCryptoCipher *cipher;
    Error *err = NULL;
    double total = 0.0;
    size_t chunk_size = (size_t)opaque;
    uint8_t *key = NULL, *iv = NULL;
    uint8_t *plaintext = NULL, *ciphertext = NULL;
    size_t nkey = qcrypto_cipher_get_key_len(QCRYPTO_CIPHER_ALG_AES_128);
    size_t niv = qcrypto_cipher_get_iv_len(QCRYPTO_CIPHER_ALG_AES_128,
                                           QCRYPTO_CIPHER_MODE_CBC);

    key = g_new0(uint8_t, nkey);
    memset(key, g_test_rand_int(), nkey);

    iv = g_new0(uint8_t, niv);
    memset(iv, g_test_rand_int(), niv);

    ciphertext = g_new0(uint8_t, chunk_size);

    plaintext = g_new0(uint8_t, chunk_size);
    memset(plaintext, g_test_rand_int(), chunk_size);

    cipher = qcrypto_cipher_new(QCRYPTO_CIPHER_ALG_AES_128,
                                QCRYPTO_CIPHER_MODE_CBC,
                                key, nkey, &err);
    g_assert(cipher != NULL);

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
    } while (g_test_timer_elapsed() < 5.0);

    total /= 1024 * 1024; /* to MB */

    g_print("cbc(aes128): ");
    g_print("Testing chunk_size %ld bytes ", chunk_size);
    g_print("done: %.2f MB in %.2f secs: ", total, g_test_timer_last());
    g_print("%.2f MB/sec\n", total / g_test_timer_last());

    qcrypto_cipher_free(cipher);
    g_free(plaintext);
    g_free(ciphertext);
    g_free(iv);
    g_free(key);
}

int main(int argc, char **argv)
{
    size_t i;
    char name[64];

    g_test_init(&argc, &argv, NULL);
    g_assert(qcrypto_init(NULL) == 0);

    for (i = 512; i <= (64 * 1204); i *= 2) {
        memset(name, 0 , sizeof(name));
        snprintf(name, sizeof(name), "/crypto/cipher/speed-%lu", i);
        g_test_add_data_func(name, (void *)i, test_cipher_speed);
    }

    return g_test_run();
}
