/*
 * QEMU Crypto hmac speed benchmark
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
#include "crypto/hmac.h"

#define KEY "monkey monkey monkey monkey"

static void test_hmac_speed(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    QCryptoHmac *hmac = NULL;
    uint8_t *in = NULL, *out = NULL;
    size_t out_len = 0;
    double total = 0.0;
    struct iovec iov;
    Error *err = NULL;
    int ret;

    if (!qcrypto_hmac_supports(QCRYPTO_HASH_ALG_SHA256)) {
        return;
    }

    in = g_new0(uint8_t, chunk_size);
    memset(in, g_test_rand_int(), chunk_size);

    iov.iov_base = (char *)in;
    iov.iov_len = chunk_size;

    g_test_timer_start();
    do {
        hmac = qcrypto_hmac_new(QCRYPTO_HASH_ALG_SHA256,
                                (const uint8_t *)KEY, strlen(KEY), &err);
        g_assert(err == NULL);
        g_assert(hmac != NULL);

        ret = qcrypto_hmac_bytesv(hmac, &iov, 1, &out, &out_len, &err);
        g_assert(ret == 0);
        g_assert(err == NULL);

        qcrypto_hmac_free(hmac);

        total += chunk_size;
    } while (g_test_timer_elapsed() < 5.0);

    total /= MiB;
    g_print("hmac(sha256): ");
    g_print("Testing chunk_size %zu bytes ", chunk_size);
    g_print("done: %.2f MB in %.2f secs: ", total, g_test_timer_last());
    g_print("%.2f MB/sec\n", total / g_test_timer_last());

    g_free(out);
    g_free(in);
}

int main(int argc, char **argv)
{
    size_t i;
    char name[64];

    g_test_init(&argc, &argv, NULL);
    g_assert(qcrypto_init(NULL) == 0);

    for (i = 512; i <= 64 * KiB; i *= 2) {
        memset(name, 0 , sizeof(name));
        snprintf(name, sizeof(name), "/crypto/hmac/speed-%zu", i);
        g_test_add_data_func(name, (void *)i, test_hmac_speed);
    }

    return g_test_run();
}
