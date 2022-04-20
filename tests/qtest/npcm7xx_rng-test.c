/*
 * QTest testcase for the Nuvoton NPCM7xx Random Number Generator
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include <math.h>

#include "libqtest-single.h"
#include "qemu/bitops.h"
#include "qemu/cutils.h"

#define RNG_BASE_ADDR   0xf000b000

/* Control and Status Register */
#define RNGCS   0x00
# define DVALID     BIT(1)  /* Data Valid */
# define RNGE       BIT(0)  /* RNG Enable */
/* Data Register */
#define RNGD    0x04
/* Mode Register */
#define RNGMODE 0x08
# define ROSEL_NORMAL   (2) /* RNG only works in this mode */

/* Number of bits to collect for randomness tests. */
#define TEST_INPUT_BITS  (128)

static void dump_buf_if_failed(const uint8_t *buf, size_t size)
{
    if (g_test_failed()) {
        qemu_hexdump(stderr, "", buf, size);
    }
}

static void rng_writeb(unsigned int offset, uint8_t value)
{
    writeb(RNG_BASE_ADDR + offset, value);
}

static uint8_t rng_readb(unsigned int offset)
{
    return readb(RNG_BASE_ADDR + offset);
}

/* Disable RNG and set normal ring oscillator mode. */
static void rng_reset(void)
{
    rng_writeb(RNGCS, 0);
    rng_writeb(RNGMODE, ROSEL_NORMAL);
}

/* Reset RNG and then enable it. */
static void rng_reset_enable(void)
{
    rng_reset();
    rng_writeb(RNGCS, RNGE);
}

/* Wait until Data Valid bit is set. */
static bool rng_wait_ready(void)
{
    /* qemu_guest_getrandom may fail. Assume it won't fail 10 times in a row. */
    int retries = 10;

    while (retries-- > 0) {
        if (rng_readb(RNGCS) & DVALID) {
            return true;
        }
    }

    return false;
}

/*
 * Perform a frequency (monobit) test, as defined by NIST SP 800-22, on the
 * sequence in buf and return the P-value. This represents the probability of a
 * truly random sequence having the same proportion of zeros and ones as the
 * sequence in buf.
 *
 * An RNG which always returns 0x00 or 0xff, or has some bits stuck at 0 or 1,
 * will fail this test. However, an RNG which always returns 0x55, 0xf0 or some
 * other value with an equal number of zeroes and ones will pass.
 */
static double calc_monobit_p(const uint8_t *buf, unsigned int len)
{
    unsigned int i;
    double s_obs;
    int sn = 0;

    for (i = 0; i < len; i++) {
        /*
         * Each 1 counts as 1, each 0 counts as -1.
         * s = cp - (8 - cp) = 2 * cp - 8
         */
        sn += 2 * ctpop8(buf[i]) - 8;
    }

    s_obs = abs(sn) / sqrt(len * BITS_PER_BYTE);

    return erfc(s_obs / sqrt(2));
}

/*
 * Perform a runs test, as defined by NIST SP 800-22, and return the P-value.
 * This represents the probability of a truly random sequence having the same
 * number of runs (i.e. uninterrupted sequences of identical bits) as the
 * sequence in buf.
 */
static double calc_runs_p(const unsigned long *buf, unsigned int nr_bits)
{
    unsigned int j;
    unsigned int k;
    int nr_ones = 0;
    int vn_obs = 0;
    double pi;

    g_assert(nr_bits % BITS_PER_LONG == 0);

    for (j = 0; j < nr_bits / BITS_PER_LONG; j++) {
        nr_ones += __builtin_popcountl(buf[j]);
    }
    pi = (double)nr_ones / nr_bits;

    for (k = 0; k < nr_bits - 1; k++) {
        vn_obs += (test_bit(k, buf) ^ test_bit(k + 1, buf));
    }
    vn_obs += 1;

    return erfc(fabs(vn_obs - 2 * nr_bits * pi * (1.0 - pi))
                / (2 * sqrt(2 * nr_bits) * pi * (1.0 - pi)));
}

/*
 * Verifies that DVALID is clear, and RNGD reads zero, when RNGE is cleared,
 * and DVALID eventually becomes set when RNGE is set.
 */
static void test_enable_disable(void)
{
    /* Disable: DVALID should not be set, and RNGD should read zero */
    rng_reset();
    g_assert_cmphex(rng_readb(RNGCS), ==, 0);
    g_assert_cmphex(rng_readb(RNGD), ==, 0);

    /* Enable: DVALID should be set, but we can't make assumptions about RNGD */
    rng_writeb(RNGCS, RNGE);
    g_assert_true(rng_wait_ready());
    g_assert_cmphex(rng_readb(RNGCS), ==, DVALID | RNGE);

    /* Disable: DVALID should not be set, and RNGD should read zero */
    rng_writeb(RNGCS, 0);
    g_assert_cmphex(rng_readb(RNGCS), ==, 0);
    g_assert_cmphex(rng_readb(RNGD), ==, 0);
}

/*
 * Verifies that the RNG only produces data when RNGMODE is set to 'normal'
 * ring oscillator mode.
 */
static void test_rosel(void)
{
    rng_reset_enable();
    g_assert_true(rng_wait_ready());
    rng_writeb(RNGMODE, 0);
    g_assert_false(rng_wait_ready());
    rng_writeb(RNGMODE, ROSEL_NORMAL);
    g_assert_true(rng_wait_ready());
    rng_writeb(RNGMODE, 0);
    g_assert_false(rng_wait_ready());
}

/*
 * Verifies that a continuous sequence of bits collected after enabling the RNG
 * satisfies a monobit test.
 */
static void test_continuous_monobit(void)
{
    uint8_t buf[TEST_INPUT_BITS / BITS_PER_BYTE];
    unsigned int i;

    rng_reset_enable();
    for (i = 0; i < sizeof(buf); i++) {
        g_assert_true(rng_wait_ready());
        buf[i] = rng_readb(RNGD);
    }

    g_assert_cmpfloat(calc_monobit_p(buf, sizeof(buf)), >, 0.01);
    dump_buf_if_failed(buf, sizeof(buf));
}

/*
 * Verifies that a continuous sequence of bits collected after enabling the RNG
 * satisfies a runs test.
 */
static void test_continuous_runs(void)
{
    union {
        unsigned long l[TEST_INPUT_BITS / BITS_PER_LONG];
        uint8_t c[TEST_INPUT_BITS / BITS_PER_BYTE];
    } buf;
    unsigned int i;

    rng_reset_enable();
    for (i = 0; i < sizeof(buf); i++) {
        g_assert_true(rng_wait_ready());
        buf.c[i] = rng_readb(RNGD);
    }

    g_assert_cmpfloat(calc_runs_p(buf.l, sizeof(buf) * BITS_PER_BYTE), >, 0.01);
    dump_buf_if_failed(buf.c, sizeof(buf));
}

/*
 * Verifies that the first data byte collected after enabling the RNG satisfies
 * a monobit test.
 */
static void test_first_byte_monobit(void)
{
    /* Enable, collect one byte, disable. Repeat until we have 100 bits. */
    uint8_t buf[TEST_INPUT_BITS / BITS_PER_BYTE];
    unsigned int i;

    rng_reset();
    for (i = 0; i < sizeof(buf); i++) {
        rng_writeb(RNGCS, RNGE);
        g_assert_true(rng_wait_ready());
        buf[i] = rng_readb(RNGD);
        rng_writeb(RNGCS, 0);
    }

    g_assert_cmpfloat(calc_monobit_p(buf, sizeof(buf)), >, 0.01);
    dump_buf_if_failed(buf, sizeof(buf));
}

/*
 * Verifies that the first data byte collected after enabling the RNG satisfies
 * a runs test.
 */
static void test_first_byte_runs(void)
{
    /* Enable, collect one byte, disable. Repeat until we have 100 bits. */
    union {
        unsigned long l[TEST_INPUT_BITS / BITS_PER_LONG];
        uint8_t c[TEST_INPUT_BITS / BITS_PER_BYTE];
    } buf;
    unsigned int i;

    rng_reset();
    for (i = 0; i < sizeof(buf); i++) {
        rng_writeb(RNGCS, RNGE);
        g_assert_true(rng_wait_ready());
        buf.c[i] = rng_readb(RNGD);
        rng_writeb(RNGCS, 0);
    }

    g_assert_cmpfloat(calc_runs_p(buf.l, sizeof(buf) * BITS_PER_BYTE), >, 0.01);
    dump_buf_if_failed(buf.c, sizeof(buf));
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("npcm7xx_rng/enable_disable", test_enable_disable);
    qtest_add_func("npcm7xx_rng/rosel", test_rosel);
    /*
     * These tests fail intermittently; only run them on explicit
     * request until we figure out why.
     */
    if (getenv("QEMU_TEST_FLAKY_RNG_TESTS")) {
        qtest_add_func("npcm7xx_rng/continuous/monobit", test_continuous_monobit);
        qtest_add_func("npcm7xx_rng/continuous/runs", test_continuous_runs);
        qtest_add_func("npcm7xx_rng/first_byte/monobit", test_first_byte_monobit);
        qtest_add_func("npcm7xx_rng/first_byte/runs", test_first_byte_runs);
    }

    qtest_start("-machine npcm750-evb");
    ret = g_test_run();
    qtest_end();

    return ret;
}
