/*
 * Xor Based Zero Run Length Encoding unit tests.
 *
 * Copyright 2013 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Orit Wasserman  <owasserm@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "../migration/xbzrle.h"

#define XBZRLE_PAGE_SIZE 4096

static void test_uleb(void)
{
    uint32_t i, val;
    uint8_t buf[2];
    int encode_ret, decode_ret;

    for (i = 0; i <= 0x3fff; i++) {
        encode_ret = uleb128_encode_small(&buf[0], i);
        decode_ret = uleb128_decode_small(&buf[0], &val);
        g_assert(encode_ret == decode_ret);
        g_assert(i == val);
    }

    /* decode invalid value */
    buf[0] = 0x80;
    buf[1] = 0x80;

    decode_ret = uleb128_decode_small(&buf[0], &val);
    g_assert(decode_ret == -1);
    g_assert(val == 0);
}

static float * test_encode_decode_zero(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *buffer512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed512 = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0;
    int dlen = 0, dlen512 = 0;
    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1006);

    for (i = diff_len; i > 0; i--) {
        buffer[1000 + i] = i;
        buffer512[1000 + i] = i;
    }

    buffer[1000 + diff_len + 3] = 103;
    buffer[1000 + diff_len + 5] = 105;

    buffer512[1000 + diff_len + 3] = 103;
    buffer512[1000 + diff_len + 5] = 105;

    /* encode zero page */
    time_t t_start, t_end, t_start512, t_end512;
    t_start = clock();
    dlen = xbzrle_encode_buffer(buffer, buffer, XBZRLE_PAGE_SIZE, compressed,
                       XBZRLE_PAGE_SIZE);
    t_end = clock();
    float time_val = difftime(t_end, t_start);
    g_assert(dlen == 0);

    t_start512 = clock();
    dlen512 = xbzrle_encode_buffer_512(buffer512, buffer512, XBZRLE_PAGE_SIZE, compressed512,
                       XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(dlen512 == 0);

    static float result_zero[2];
    result_zero[0] = time_val;
    result_zero[1] = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(buffer512);
    g_free(compressed512);

    return result_zero;
}

static void test_encode_decode_zero_range(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    float *res;
    for (i = 0; i < 10000; i++) {
        res = test_encode_decode_zero();
        time_raw += res[0];
        time_512 += res[1];
    }
    printf("Zero test:\n");
    printf("Raw xbzrle_encode time is %f ms \n",time_raw);
    printf("512 xbzrle_encode time is %f ms \n", time_512);
}

static float * test_encode_decode_unchanged(void)
{
    uint8_t *compressed = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test512 = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0;
    int dlen = 0, dlen512 = 0;
    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1006);

    for (i = diff_len; i > 0; i--) {
        test[1000 + i] = i + 4;
        test512[1000 + i] = i + 4;
    }

    test[1000 + diff_len + 3] = 107;
    test[1000 + diff_len + 5] = 109;

    test512[1000 + diff_len + 3] = 107;
    test512[1000 + diff_len + 5] = 109;

    /* test unchanged buffer */
    time_t t_start, t_end, t_start512, t_end512;
    t_start = clock();
    dlen = xbzrle_encode_buffer(test, test, XBZRLE_PAGE_SIZE, compressed,
                                XBZRLE_PAGE_SIZE);
    t_end = clock();
    float time_val = difftime(t_end, t_start);
    g_assert(dlen == 0);

    t_start512 = clock();
    dlen512 = xbzrle_encode_buffer_512(test512, test512, XBZRLE_PAGE_SIZE, compressed512,
                                XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(dlen512 == 0);

    static float result_unchanged[2];
    result_unchanged[0] = time_val;
    result_unchanged[1] = time_val512;

    g_free(test);
    g_free(compressed);
    g_free(test512);
    g_free(compressed512);

    return result_unchanged;
}

static void test_encode_decode_unchanged_range(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    float *res;
    for (i = 0; i < 10000; i++) {
        res = test_encode_decode_unchanged();
        time_raw += res[0];
        time_512 += res[1];
    }
    printf("Unchanged test:\n");
    printf("Raw xbzrle_encode time is %f ms \n",time_raw);
    printf("512 xbzrle_encode time is %f ms \n", time_512);
}

static float * test_encode_decode_1_byte(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc(XBZRLE_PAGE_SIZE);
    uint8_t *buffer512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed512 = g_malloc(XBZRLE_PAGE_SIZE);
    int dlen = 0, rc = 0, dlen512 = 0, rc512 = 0;
    uint8_t buf[2];
    uint8_t buf512[2];

    test[XBZRLE_PAGE_SIZE - 1] = 1;
    test512[XBZRLE_PAGE_SIZE - 1] = 1;

    time_t t_start, t_end, t_start512, t_end512;
    t_start = clock();
    dlen = xbzrle_encode_buffer(buffer, test, XBZRLE_PAGE_SIZE, compressed,
                       XBZRLE_PAGE_SIZE);
    t_end = clock();
    float time_val = difftime(t_end, t_start);
    g_assert(dlen == (uleb128_encode_small(&buf[0], 4095) + 2));

    rc = xbzrle_decode_buffer(compressed, dlen, buffer, XBZRLE_PAGE_SIZE);
    g_assert(rc == XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test, buffer, XBZRLE_PAGE_SIZE) == 0);

    t_start512 = clock();
    dlen512 = xbzrle_encode_buffer(buffer512, test512, XBZRLE_PAGE_SIZE, compressed512,
                       XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(dlen512 == (uleb128_encode_small(&buf512[0], 4095) + 2));

    rc512 = xbzrle_decode_buffer(compressed512, dlen512, buffer512, XBZRLE_PAGE_SIZE);
    g_assert(rc512 == XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test512, buffer512, XBZRLE_PAGE_SIZE) == 0);

    static float result_1_byte[2];
    result_1_byte[0] = time_val;
    result_1_byte[1] = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

    return result_1_byte;
}

static void test_encode_decode_1_byte_range(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    float *res;
    for (i = 0; i < 10000; i++) {
        res = test_encode_decode_1_byte();
        time_raw += res[0];
        time_512 += res[1];
    }
    printf("1 byte test:\n");
    printf("Raw xbzrle_encode time is %f ms \n",time_raw);
    printf("512 xbzrle_encode time is %f ms \n", time_512);
}

static float * test_encode_decode_overflow(void)
{
    uint8_t *compressed = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *buffer512 = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0, rc = 0, rc512 = 0;

    for (i = 0; i < XBZRLE_PAGE_SIZE / 2 - 1; i++) {
        test[i * 2] = 1;
        test512[i * 2] = 1;
    }

    /* encode overflow */
    time_t t_start, t_end, t_start512, t_end512;
    t_start = clock();
    rc = xbzrle_encode_buffer(buffer, test, XBZRLE_PAGE_SIZE, compressed,
                              XBZRLE_PAGE_SIZE);
    t_end = clock();
    float time_val = difftime(t_end, t_start);
    g_assert(rc == -1);

    t_start512 = clock();
    rc512 = xbzrle_encode_buffer_512(buffer512, test512, XBZRLE_PAGE_SIZE, compressed512,
                              XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(rc512 == -1);

    static float result_overflow[2];
    result_overflow[0] = time_val;
    result_overflow[1] = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

    return result_overflow;
}

static void test_encode_decode_overflow_range(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    float *res;
    for (i = 0; i < 10000; i++) {
        res = test_encode_decode_overflow();
        time_raw += res[0];
        time_512 += res[1];
    }
    printf("Overflow test:\n");
    printf("Raw xbzrle_encode time is %f ms \n",time_raw);
    printf("512 xbzrle_encode time is %f ms \n", time_512);
}

static float * encode_decode_range(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *buffer512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed512 = g_malloc(XBZRLE_PAGE_SIZE);
    uint8_t *test512 = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0, rc = 0, rc512 = 0;
    int dlen = 0, dlen512 = 0;

    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1006);

    for (i = diff_len; i > 0; i--) {
        buffer[1000 + i] = i;
        test[1000 + i] = i + 4;
        buffer512[1000 + i] = i;
        test512[1000 + i] = i + 4;
    }

    buffer[1000 + diff_len + 3] = 103;
    test[1000 + diff_len + 3] = 107;

    buffer[1000 + diff_len + 5] = 105;
    test[1000 + diff_len + 5] = 109;

    buffer512[1000 + diff_len + 3] = 103;
    test512[1000 + diff_len + 3] = 107;

    buffer512[1000 + diff_len + 5] = 105;
    test512[1000 + diff_len + 5] = 109;

    /* test encode/decode */
    time_t t_start, t_end, t_start512, t_end512;
    t_start = clock();
    dlen = xbzrle_encode_buffer(test, buffer, XBZRLE_PAGE_SIZE, compressed,
                                XBZRLE_PAGE_SIZE);
    t_end = clock();
    float time_val = difftime(t_end, t_start);
    rc = xbzrle_decode_buffer(compressed, dlen, test, XBZRLE_PAGE_SIZE);
    g_assert(rc < XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test, buffer, XBZRLE_PAGE_SIZE) == 0);

    t_start512 = clock();
    dlen512 = xbzrle_encode_buffer_512(test512, buffer512, XBZRLE_PAGE_SIZE, compressed512,
                                XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    rc512 = xbzrle_decode_buffer(compressed512, dlen512, test512, XBZRLE_PAGE_SIZE);
    g_assert(rc512 < XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test512, buffer512, XBZRLE_PAGE_SIZE) == 0);

    static float result_range[2];
    result_range[0] = time_val;
    result_range[1] = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

    return result_range;
}

static void test_encode_decode(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    float *res;
    for (i = 0; i < 10000; i++) {
        res = encode_decode_range();
        time_raw += res[0];
        time_512 += res[1];
    }
    printf("Encode decode test:\n");
    printf("Raw xbzrle_encode time is %f ms \n",time_raw);
    printf("512 xbzrle_encode time is %f ms \n", time_512);
}

static float * encode_decode_random(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *buffer512 = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed512 = g_malloc(XBZRLE_PAGE_SIZE);
    uint8_t *test512 = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0, rc = 0, rc512 = 0; 
    int dlen = 0, dlen512 = 0;

    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1);
    // store the index of diff
    int dirty_index[diff_len];
    for (int j = 0; j < diff_len; j++) {
        dirty_index[j] = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1);
    }
    for (i = diff_len - 1; i >= 0; i--) {
        buffer[dirty_index[i]] = i;
        test[dirty_index[i]] = i + 4;
        buffer512[dirty_index[i]] = i;
        test512[dirty_index[i]] = i + 4;
    }

    time_t t_start, t_end, t_start512, t_end512;
    t_start = clock();
    dlen = xbzrle_encode_buffer(test, buffer, XBZRLE_PAGE_SIZE, compressed,
                                XBZRLE_PAGE_SIZE);
    t_end = clock();
    float time_val = difftime(t_end, t_start);
    rc = xbzrle_decode_buffer(compressed, dlen, test, XBZRLE_PAGE_SIZE);
    g_assert(rc < XBZRLE_PAGE_SIZE);

    t_start512 = clock();
    dlen512 = xbzrle_encode_buffer_512(test512, buffer512, XBZRLE_PAGE_SIZE, compressed512,
                                XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    rc512 = xbzrle_decode_buffer(compressed512, dlen512, test512, XBZRLE_PAGE_SIZE);
    g_assert(rc512 < XBZRLE_PAGE_SIZE);

    static float result_random[2];
    result_random[0] = time_val;
    result_random[1] = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

    return result_random;
}

static void test_encode_decode_random(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    float *res;
    for (i = 0; i < 10000; i++) {
        res = encode_decode_random();
        time_raw += res[0];
        time_512 += res[1];
    }
    printf("Random test: \n");
    printf("Raw xbzrle_encode time is %f ms \n",time_raw);
    printf("512 xbzrle_encode time is %f ms \n", time_512);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_rand_int();
    g_test_add_func("/xbzrle/uleb", test_uleb);
    g_test_add_func("/xbzrle/encode_decode_zero", test_encode_decode_zero_range);
    g_test_add_func("/xbzrle/encode_decode_unchanged",
                    test_encode_decode_unchanged_range);
    g_test_add_func("/xbzrle/encode_decode_1_byte", test_encode_decode_1_byte_range);
    g_test_add_func("/xbzrle/encode_decode_overflow",
                    test_encode_decode_overflow_range);
    g_test_add_func("/xbzrle/encode_decode", test_encode_decode);
    g_test_add_func("/xbzrle/encode_decode_random", test_encode_decode_random);

    return g_test_run();
}
