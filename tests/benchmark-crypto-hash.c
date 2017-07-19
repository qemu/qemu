/*
 * QEMU Crypto hash speed benchmark
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
#include "crypto/hash.h"

static void test_hash_speed(const void *opaque)
{
    size_t chunk_size = (size_t)opaque;
    uint8_t *in = NULL, *out = NULL;
    size_t out_len = 0;
    double total = 0.0;
    struct iovec iov;
    int ret;

    in = g_new0(uint8_t, chunk_size);
    memset(in, g_test_rand_int(), chunk_size);

    iov.iov_base = (char *)in;
    iov.iov_len = chunk_size;

    g_test_timer_start();
    do {
        ret = qcrypto_hash_bytesv(QCRYPTO_HASH_ALG_SHA256,
                                  &iov, 1, &out, &out_len,
                                  NULL);
        g_assert(ret == 0);

        total += chunk_size;
    } while (g_test_timer_elapsed() < 5.0);

    total /= 1024 * 1024; /* to MB */
    g_print("sha256: ");
    g_print("Testing chunk_size %ld bytes ", chunk_size);
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

    for (i = 512; i <= (64 * 1204); i *= 2) {
        memset(name, 0 , sizeof(name));
        snprintf(name, sizeof(name), "/crypto/hash/speed-%lu", i);
        g_test_add_data_func(name, (void *)i, test_hash_speed);
    }

    return g_test_run();
}
