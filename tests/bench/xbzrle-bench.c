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

#if defined(CONFIG_AVX512BW_OPT)
#define XBZRLE_PAGE_SIZE 4096
static bool is_cpu_support_avx512bw;
#include "qemu/cpuid.h"
static void __attribute__((constructor)) init_cpu_flag(void)
{
    unsigned max = __get_cpuid_max(0, NULL);
    int a, b, c, d;
    is_cpu_support_avx512bw = false;
    if (max >= 1) {
        __cpuid(1, a, b, c, d);
         /* We must check that AVX is not just available, but usable.  */
        if ((c & bit_OSXSAVE) && (c & bit_AVX) && max >= 7) {
            int bv;
            __asm("xgetbv" : "=a"(bv), "=d"(d) : "c"(0));
            __cpuid_count(7, 0, a, b, c, d);
           /* 0xe6:
            *  XCR0[7:5] = 111b (OPMASK state, upper 256-bit of ZMM0-ZMM15
            *                    and ZMM16-ZMM31 state are enabled by OS)
            *  XCR0[2:1] = 11b (XMM state and YMM state are enabled by OS)
            */
            if ((bv & 0xe6) == 0xe6 && (b & bit_AVX512BW)) {
                is_cpu_support_avx512bw = true;
            }
        }
    }
    return ;
}

struct ResTime {
    float t_raw;
    float t_512;
};


/* Function prototypes
int xbzrle_encode_buffer_avx512(uint8_t *old_buf, uint8_t *new_buf, int slen,
                                uint8_t *dst, int dlen);
*/
static void encode_decode_zero(struct ResTime *res)
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
    dlen512 = xbzrle_encode_buffer_avx512(buffer512, buffer512, XBZRLE_PAGE_SIZE,
                                       compressed512, XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(dlen512 == 0);

    res->t_raw = time_val;
    res->t_512 = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(buffer512);
    g_free(compressed512);

}

static void test_encode_decode_zero_avx512(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    struct ResTime res;
    for (i = 0; i < 10000; i++) {
        encode_decode_zero(&res);
        time_raw += res.t_raw;
        time_512 += res.t_512;
    }
    printf("Zero test:\n");
    printf("Raw xbzrle_encode time is %f ms\n", time_raw);
    printf("512 xbzrle_encode time is %f ms\n", time_512);
}

static void encode_decode_unchanged(struct ResTime *res)
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
    dlen512 = xbzrle_encode_buffer_avx512(test512, test512, XBZRLE_PAGE_SIZE,
                                       compressed512, XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(dlen512 == 0);

    res->t_raw = time_val;
    res->t_512 = time_val512;

    g_free(test);
    g_free(compressed);
    g_free(test512);
    g_free(compressed512);

}

static void test_encode_decode_unchanged_avx512(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    struct ResTime res;
    for (i = 0; i < 10000; i++) {
        encode_decode_unchanged(&res);
        time_raw += res.t_raw;
        time_512 += res.t_512;
    }
    printf("Unchanged test:\n");
    printf("Raw xbzrle_encode time is %f ms\n", time_raw);
    printf("512 xbzrle_encode time is %f ms\n", time_512);
}

static void encode_decode_1_byte(struct ResTime *res)
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
    dlen512 = xbzrle_encode_buffer_avx512(buffer512, test512, XBZRLE_PAGE_SIZE,
                                       compressed512, XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(dlen512 == (uleb128_encode_small(&buf512[0], 4095) + 2));

    rc512 = xbzrle_decode_buffer(compressed512, dlen512, buffer512,
                                 XBZRLE_PAGE_SIZE);
    g_assert(rc512 == XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test512, buffer512, XBZRLE_PAGE_SIZE) == 0);

    res->t_raw = time_val;
    res->t_512 = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

}

static void test_encode_decode_1_byte_avx512(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    struct ResTime res;
    for (i = 0; i < 10000; i++) {
        encode_decode_1_byte(&res);
        time_raw += res.t_raw;
        time_512 += res.t_512;
    }
    printf("1 byte test:\n");
    printf("Raw xbzrle_encode time is %f ms\n", time_raw);
    printf("512 xbzrle_encode time is %f ms\n", time_512);
}

static void encode_decode_overflow(struct ResTime *res)
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
    rc512 = xbzrle_encode_buffer_avx512(buffer512, test512, XBZRLE_PAGE_SIZE,
                                     compressed512, XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    g_assert(rc512 == -1);

    res->t_raw = time_val;
    res->t_512 = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

}

static void test_encode_decode_overflow_avx512(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    struct ResTime res;
    for (i = 0; i < 10000; i++) {
        encode_decode_overflow(&res);
        time_raw += res.t_raw;
        time_512 += res.t_512;
    }
    printf("Overflow test:\n");
    printf("Raw xbzrle_encode time is %f ms\n", time_raw);
    printf("512 xbzrle_encode time is %f ms\n", time_512);
}

static void encode_decode_range_avx512(struct ResTime *res)
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
    dlen512 = xbzrle_encode_buffer_avx512(test512, buffer512, XBZRLE_PAGE_SIZE,
                                       compressed512, XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    rc512 = xbzrle_decode_buffer(compressed512, dlen512, test512, XBZRLE_PAGE_SIZE);
    g_assert(rc512 < XBZRLE_PAGE_SIZE);
    g_assert(memcmp(test512, buffer512, XBZRLE_PAGE_SIZE) == 0);

    res->t_raw = time_val;
    res->t_512 = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

}

static void test_encode_decode_avx512(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    struct ResTime res;
    for (i = 0; i < 10000; i++) {
        encode_decode_range_avx512(&res);
        time_raw += res.t_raw;
        time_512 += res.t_512;
    }
    printf("Encode decode test:\n");
    printf("Raw xbzrle_encode time is %f ms\n", time_raw);
    printf("512 xbzrle_encode time is %f ms\n", time_512);
}

static void encode_decode_random(struct ResTime *res)
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
    /* store the index of diff */
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
    dlen512 = xbzrle_encode_buffer_avx512(test512, buffer512, XBZRLE_PAGE_SIZE,
                                       compressed512, XBZRLE_PAGE_SIZE);
    t_end512 = clock();
    float time_val512 = difftime(t_end512, t_start512);
    rc512 = xbzrle_decode_buffer(compressed512, dlen512, test512, XBZRLE_PAGE_SIZE);
    g_assert(rc512 < XBZRLE_PAGE_SIZE);

    res->t_raw = time_val;
    res->t_512 = time_val512;

    g_free(buffer);
    g_free(compressed);
    g_free(test);
    g_free(buffer512);
    g_free(compressed512);
    g_free(test512);

}

static void test_encode_decode_random_avx512(void)
{
    int i;
    float time_raw = 0.0, time_512 = 0.0;
    struct ResTime res;
    for (i = 0; i < 10000; i++) {
        encode_decode_random(&res);
        time_raw += res.t_raw;
        time_512 += res.t_512;
    }
    printf("Random test:\n");
    printf("Raw xbzrle_encode time is %f ms\n", time_raw);
    printf("512 xbzrle_encode time is %f ms\n", time_512);
}
#endif

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_rand_int();
    #if defined(CONFIG_AVX512BW_OPT)
    if (likely(is_cpu_support_avx512bw)) {
        g_test_add_func("/xbzrle/encode_decode_zero", test_encode_decode_zero_avx512);
        g_test_add_func("/xbzrle/encode_decode_unchanged",
                        test_encode_decode_unchanged_avx512);
        g_test_add_func("/xbzrle/encode_decode_1_byte", test_encode_decode_1_byte_avx512);
        g_test_add_func("/xbzrle/encode_decode_overflow",
                        test_encode_decode_overflow_avx512);
        g_test_add_func("/xbzrle/encode_decode", test_encode_decode_avx512);
        g_test_add_func("/xbzrle/encode_decode_random", test_encode_decode_random_avx512);
    }
    #endif
    return g_test_run();
}
