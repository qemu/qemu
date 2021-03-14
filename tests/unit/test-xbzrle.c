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
#include "qemu-common.h"
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

static void test_encode_decode_zero(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0;
    int dlen = 0;
    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1006);

    for (i = diff_len; i > 0; i--) {
        buffer[1000 + i] = i;
    }

    buffer[1000 + diff_len + 3] = 103;
    buffer[1000 + diff_len + 5] = 105;

    /* encode zero page */
    dlen = xbzrle_encode_buffer(buffer, buffer, XBZRLE_PAGE_SIZE, compressed,
                       XBZRLE_PAGE_SIZE);
    g_assert(dlen == 0);

    g_free(buffer);
    g_free(compressed);
}

static void test_encode_decode_unchanged(void)
{
    uint8_t *compressed = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0;
    int dlen = 0;
    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1006);

    for (i = diff_len; i > 0; i--) {
        test[1000 + i] = i + 4;
    }

    test[1000 + diff_len + 3] = 107;
    test[1000 + diff_len + 5] = 109;

    /* test unchanged buffer */
    dlen = xbzrle_encode_buffer(test, test, XBZRLE_PAGE_SIZE, compressed,
                                XBZRLE_PAGE_SIZE);
    g_assert(dlen == 0);

    g_free(test);
    g_free(compressed);
}

static void test_encode_decode_1_byte(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc(XBZRLE_PAGE_SIZE);
    int dlen = 0, rc = 0;
    uint8_t buf[2];

    test[XBZRLE_PAGE_SIZE - 1] = 1;

    dlen = xbzrle_encode_buffer(buffer, test, XBZRLE_PAGE_SIZE, compressed,
                       XBZRLE_PAGE_SIZE);
    g_assert(dlen == (uleb128_encode_small(&buf[0], 4095) + 2));

    rc = xbzrle_decode_buffer(compressed, dlen, buffer, XBZRLE_PAGE_SIZE);
    g_assert(rc == XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test, buffer, XBZRLE_PAGE_SIZE) == 0);

    g_free(buffer);
    g_free(compressed);
    g_free(test);
}

static void test_encode_decode_overflow(void)
{
    uint8_t *compressed = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0, rc = 0;

    for (i = 0; i < XBZRLE_PAGE_SIZE / 2 - 1; i++) {
        test[i * 2] = 1;
    }

    /* encode overflow */
    rc = xbzrle_encode_buffer(buffer, test, XBZRLE_PAGE_SIZE, compressed,
                              XBZRLE_PAGE_SIZE);
    g_assert(rc == -1);

    g_free(buffer);
    g_free(compressed);
    g_free(test);
}

static void encode_decode_range(void)
{
    uint8_t *buffer = g_malloc0(XBZRLE_PAGE_SIZE);
    uint8_t *compressed = g_malloc(XBZRLE_PAGE_SIZE);
    uint8_t *test = g_malloc0(XBZRLE_PAGE_SIZE);
    int i = 0, rc = 0;
    int dlen = 0;

    int diff_len = g_test_rand_int_range(0, XBZRLE_PAGE_SIZE - 1006);

    for (i = diff_len; i > 0; i--) {
        buffer[1000 + i] = i;
        test[1000 + i] = i + 4;
    }

    buffer[1000 + diff_len + 3] = 103;
    test[1000 + diff_len + 3] = 107;

    buffer[1000 + diff_len + 5] = 105;
    test[1000 + diff_len + 5] = 109;

    /* test encode/decode */
    dlen = xbzrle_encode_buffer(test, buffer, XBZRLE_PAGE_SIZE, compressed,
                                XBZRLE_PAGE_SIZE);

    rc = xbzrle_decode_buffer(compressed, dlen, test, XBZRLE_PAGE_SIZE);
    g_assert(rc < XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test, buffer, XBZRLE_PAGE_SIZE) == 0);

    g_free(buffer);
    g_free(compressed);
    g_free(test);
}

static void test_encode_decode(void)
{
    int i;

    for (i = 0; i < 10000; i++) {
        encode_decode_range();
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_rand_int();
    g_test_add_func("/xbzrle/uleb", test_uleb);
    g_test_add_func("/xbzrle/encode_decode_zero", test_encode_decode_zero);
    g_test_add_func("/xbzrle/encode_decode_unchanged",
                    test_encode_decode_unchanged);
    g_test_add_func("/xbzrle/encode_decode_1_byte", test_encode_decode_1_byte);
    g_test_add_func("/xbzrle/encode_decode_overflow",
                    test_encode_decode_overflow);
    g_test_add_func("/xbzrle/encode_decode", test_encode_decode);

    return g_test_run();
}
