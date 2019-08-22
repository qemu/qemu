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
#include "qemu/units.h"
#include "crypto/init.h"
#include "crypto/hash.h"

typedef struct QCryptoHashOpts {
    size_t chunk_size;
    QCryptoHashAlgorithm alg;
} QCryptoHashOpts;

static void test_hash_speed(const void *opaque)
{
    const QCryptoHashOpts *opts = opaque;
    uint8_t *in = NULL, *out = NULL;
    size_t out_len = 0;
    const size_t total = 2 * GiB;
    size_t remain;
    struct iovec iov;
    int ret;

    in = g_new0(uint8_t, opts->chunk_size);
    memset(in, g_test_rand_int(), opts->chunk_size);

    iov.iov_base = (char *)in;
    iov.iov_len = opts->chunk_size;

    g_test_timer_start();
    remain = total;
    while (remain) {
        ret = qcrypto_hash_bytesv(opts->alg,
                                  &iov, 1, &out, &out_len,
                                  NULL);
        g_assert(ret == 0);

        remain -= opts->chunk_size;
    }
    g_test_timer_elapsed();

    g_print("%.2f MB/sec ", (double)total / MiB / g_test_timer_last());

    g_free(out);
    g_free(in);
}

int main(int argc, char **argv)
{
    char name[64];

    g_test_init(&argc, &argv, NULL);
    g_assert(qcrypto_init(NULL) == 0);

#define TEST_ONE(a, c)                                          \
    QCryptoHashOpts opts ## a ## c = {                          \
        .alg = QCRYPTO_HASH_ALG_ ## a, .chunk_size = c,         \
    };                                                          \
    memset(name, 0 , sizeof(name));                             \
    snprintf(name, sizeof(name),                                \
             "/crypto/benchmark/hash/%s/bufsize-%d",            \
             QCryptoHashAlgorithm_str(QCRYPTO_HASH_ALG_ ## a),  \
             c);                                                \
    if (qcrypto_hash_supports(QCRYPTO_HASH_ALG_ ## a))          \
        g_test_add_data_func(name,                              \
                             &opts ## a ## c,                   \
                             test_hash_speed);

    TEST_ONE(MD5, 512);
    TEST_ONE(MD5, 1024);
    TEST_ONE(MD5, 4096);
    TEST_ONE(MD5, 16384);

    TEST_ONE(SHA1, 512);
    TEST_ONE(SHA1, 1024);
    TEST_ONE(SHA1, 4096);
    TEST_ONE(SHA1, 16384);

    TEST_ONE(SHA224, 512);
    TEST_ONE(SHA224, 1024);
    TEST_ONE(SHA224, 4096);
    TEST_ONE(SHA224, 16384);

    TEST_ONE(SHA384, 512);
    TEST_ONE(SHA384, 1024);
    TEST_ONE(SHA384, 4096);
    TEST_ONE(SHA384, 16384);

    TEST_ONE(SHA256, 512);
    TEST_ONE(SHA256, 1024);
    TEST_ONE(SHA256, 4096);
    TEST_ONE(SHA256, 16384);

    TEST_ONE(SHA512, 512);
    TEST_ONE(SHA512, 1024);
    TEST_ONE(SHA512, 4096);
    TEST_ONE(SHA512, 16384);

    TEST_ONE(RIPEMD160, 512);
    TEST_ONE(RIPEMD160, 1024);
    TEST_ONE(RIPEMD160, 4096);
    TEST_ONE(RIPEMD160, 16384);

    return g_test_run();
}
