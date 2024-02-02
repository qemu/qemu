/*
 * QTests for the Xilinx Versal True Random Number Generator device
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

/* Base Address */
#define TRNG_BASEADDR      (0xf1230000)

/* TRNG_INT_CTRL */
#define R_TRNG_INT_CTRL                 (0x0000)
#define   TRNG_INT_CTRL_CERTF_RST_MASK  (1 << 5)
#define   TRNG_INT_CTRL_DTF_RST_MASK    (1 << 4)
#define   TRNG_INT_CTRL_DONE_RST_MASK   (1 << 3)
#define   TRNG_INT_CTRL_CERTF_EN_MASK   (1 << 2)
#define   TRNG_INT_CTRL_DTF_EN_MASK     (1 << 1)
#define   TRNG_INT_CTRL_DONE_EN_MASK    (1)

/* TRNG_STATUS */
#define R_TRNG_STATUS              (0x0004)
#define   TRNG_STATUS_QCNT_SHIFT   (9)
#define   TRNG_STATUS_QCNT_MASK    (7 << TRNG_STATUS_QCNT_SHIFT)
#define   TRNG_STATUS_CERTF_MASK   (1 << 3)
#define   TRNG_STATUS_DTF_MASK     (1 << 1)
#define   TRNG_STATUS_DONE_MASK    (1)

/* TRNG_CTRL */
#define R_TRNG_CTRL                (0x0008)
#define   TRNG_CTRL_PERSODISABLE_MASK   (1 << 10)
#define   TRNG_CTRL_SINGLEGENMODE_MASK  (1 << 9)
#define   TRNG_CTRL_PRNGMODE_MASK       (1 << 7)
#define   TRNG_CTRL_TSTMODE_MASK        (1 << 6)
#define   TRNG_CTRL_PRNGSTART_MASK      (1 << 5)
#define   TRNG_CTRL_PRNGXS_MASK         (1 << 3)
#define   TRNG_CTRL_TRSSEN_MASK         (1 << 2)
#define   TRNG_CTRL_QERTUEN_MASK        (1 << 1)
#define   TRNG_CTRL_PRNGSRST_MASK       (1)

/* TRNG_EXT_SEED_0 ... _11 */
#define R_TRNG_EXT_SEED_0          (0x0040)
#define R_TRNG_EXT_SEED_11         (R_TRNG_EXT_SEED_0 + 4 * 11)

/* TRNG_PER_STRNG_0 ... 11 */
#define R_TRNG_PER_STRNG_0         (0x0080)
#define R_TRNG_PER_STRNG_11        (R_TRNG_PER_STRNG_0 + 4 * 11)

/* TRNG_CORE_OUTPUT */
#define R_TRNG_CORE_OUTPUT         (0x00c0)

/* TRNG_RESET */
#define R_TRNG_RESET               (0x00d0)
#define   TRNG_RESET_VAL_MASK      (1)

/* TRNG_OSC_EN */
#define R_TRNG_OSC_EN              (0x00d4)
#define   TRNG_OSC_EN_VAL_MASK     (1)

/* TRNG_TRNG_ISR, _IMR, _IER, _IDR */
#define R_TRNG_ISR                 (0x00e0)
#define R_TRNG_IMR                 (0x00e4)
#define R_TRNG_IER                 (0x00e8)
#define R_TRNG_IDR                 (0x00ec)
#define   TRNG_IRQ_SLVERR_MASK     (1 << 1)
#define   TRNG_IRQ_CORE_INT_MASK   (1)

/*
 * End test with a formatted error message, by embedding the message
 * in a GError.
 */
#define TRNG_FAILED(FMT, ...)                           \
    do {                                                \
        g_autoptr(GError) err = g_error_new(            \
            g_quark_from_static_string(trng_qname), 0,  \
            FMT, ## __VA_ARGS__);                       \
        g_assert_no_error(err);                         \
    } while (0)

static const gchar trng_qname[] = "xlnx-versal-trng-test";

static const uint32_t prng_seed[12] = {
    0x01234567, 0x12345678, 0x23456789, 0x3456789a, 0x456789ab, 0x56789abc,
    0x76543210, 0x87654321, 0x98765432, 0xa9876543, 0xba987654, 0xfedcba98,
};

static const uint32_t pers_str[12] = {
    0x76543210, 0x87654321, 0x98765432, 0xa9876543, 0xba987654, 0xfedcba98,
    0x01234567, 0x12345678, 0x23456789, 0x3456789a, 0x456789ab, 0x56789abc,
};

static void trng_test_start(void)
{
    qtest_start("-machine xlnx-versal-virt");
}

static void trng_test_stop(void)
{
    qtest_end();
}

static void trng_test_set_uint_prop(const char *name, uint64_t value)
{
    const char *path = "/machine/xlnx-versal/trng";
    QDict *response;

    response = qmp("{ 'execute': 'qom-set',"
                    " 'arguments': {"
                       " 'path': %s,"
                       " 'property': %s,"
                       " 'value': %llu"
                      "} }", path,
                   name, (unsigned long long)value);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void trng_write(unsigned ra, uint32_t val)
{
    writel(TRNG_BASEADDR + ra, val);
}

static uint32_t trng_read(unsigned ra)
{
    return readl(TRNG_BASEADDR + ra);
}

static void trng_bit_set(unsigned ra, uint32_t bits)
{
    trng_write(ra, (trng_read(ra) | bits));
}

static void trng_bit_clr(unsigned ra, uint32_t bits)
{
    trng_write(ra, (trng_read(ra) & ~bits));
}

static void trng_ctrl_set(uint32_t bits)
{
    trng_bit_set(R_TRNG_CTRL, bits);
}

static void trng_ctrl_clr(uint32_t bits)
{
    trng_bit_clr(R_TRNG_CTRL, bits);
}

static uint32_t trng_status(void)
{
    return trng_read(R_TRNG_STATUS);
}

static unsigned trng_qcnt(void)
{
    uint32_t sta = trng_status();

    return (sta & TRNG_STATUS_QCNT_MASK) >> TRNG_STATUS_QCNT_SHIFT;
}

static const char *trng_info(void)
{
    uint32_t sta = trng_status();
    uint32_t ctl = trng_read(R_TRNG_CTRL);

    static char info[64];

    snprintf(info, sizeof(info), "; status=0x%x, ctrl=0x%x", sta, ctl);
    return info;
}

static void trng_check_status(uint32_t status_mask, const char *act)
{
    uint32_t clear_mask = 0;
    uint32_t status;

    /*
     * Only selected bits are events in R_TRNG_STATUS, and
     * clear them needs to go through R_INT_CTRL.
     */
    if (status_mask & TRNG_STATUS_CERTF_MASK) {
        clear_mask |= TRNG_INT_CTRL_CERTF_RST_MASK;
    }
    if (status_mask & TRNG_STATUS_DTF_MASK) {
        clear_mask |= TRNG_INT_CTRL_DTF_RST_MASK;
    }
    if (status_mask & TRNG_STATUS_DONE_MASK) {
        clear_mask |= TRNG_INT_CTRL_DONE_RST_MASK;
    }

    status = trng_status();
    if ((status & status_mask) != status_mask) {
        TRNG_FAILED("%s: Status bitmask 0x%x failed to be 1%s",
                    act, status_mask, trng_info());
    }

    /* Remove event */
    trng_bit_set(R_TRNG_INT_CTRL, clear_mask);

    if (!!(trng_read(R_TRNG_STATUS) & status_mask)) {
        TRNG_FAILED("%s: Event 0x%0x stuck at 1 after clear: %s",
                    act, status_mask, trng_info());
    }
}

static void trng_check_done_status(const char *act)
{
    trng_check_status(TRNG_STATUS_DONE_MASK, act);
}

static void trng_check_dtf_status(void)
{
    trng_check_status(TRNG_STATUS_DTF_MASK, "DTF injection");
}

static void trng_check_certf_status(void)
{
    trng_check_status(TRNG_STATUS_CERTF_MASK, "CERTF injection");
}

static void trng_reset(void)
{
    trng_write(R_TRNG_RESET, TRNG_RESET_VAL_MASK);
    trng_write(R_TRNG_RESET, 0);
}

static void trng_load(unsigned r0, const uint32_t *b384)
{
    static const uint32_t zero[12] = { 0 };
    unsigned k;

    if (!b384) {
        b384 = zero;
    }

    for (k = 0; k < 12; k++) {
        trng_write(r0 + 4 * k, b384[k]);
    }
}

static void trng_reseed(const uint32_t *seed)
{
    const char *act;
    uint32_t ctl;

    ctl = TRNG_CTRL_PRNGSTART_MASK |
          TRNG_CTRL_PRNGXS_MASK |
          TRNG_CTRL_TRSSEN_MASK;

    trng_ctrl_clr(ctl | TRNG_CTRL_PRNGMODE_MASK);

    if (seed) {
        trng_load(R_TRNG_EXT_SEED_0, seed);
        act = "Reseed PRNG";
        ctl &= ~TRNG_CTRL_TRSSEN_MASK;
    } else {
        trng_write(R_TRNG_OSC_EN, TRNG_OSC_EN_VAL_MASK);
        act = "Reseed TRNG";
        ctl &= ~TRNG_CTRL_PRNGXS_MASK;
    }

    trng_ctrl_set(ctl);
    trng_check_done_status(act);
    trng_ctrl_clr(TRNG_CTRL_PRNGSTART_MASK);
}

static void trng_generate(bool auto_enb)
{
    uint32_t ctl;

    ctl = TRNG_CTRL_PRNGSTART_MASK | TRNG_CTRL_SINGLEGENMODE_MASK;
    trng_ctrl_clr(ctl);

    if (auto_enb) {
        ctl &= ~TRNG_CTRL_SINGLEGENMODE_MASK;
    }

    trng_ctrl_set(ctl | TRNG_CTRL_PRNGMODE_MASK);

    trng_check_done_status("Generate");
    g_assert(trng_qcnt() != 7);
}

static size_t trng_collect(uint32_t *rnd, size_t cnt)
{
    size_t i;

    for (i = 0; i < cnt; i++) {
        if (trng_qcnt() == 0) {
            return i;
        }

        rnd[i] = trng_read(R_TRNG_CORE_OUTPUT);
    }

    return i;
}

/* These tests all generate 512 bits of random data with the device */
#define TEST_DATA_WORDS (512 / 32)

static void trng_test_autogen(void)
{
    const size_t cnt = TEST_DATA_WORDS;
    uint32_t rng[TEST_DATA_WORDS], prng[TEST_DATA_WORDS];
    size_t n;

    trng_reset();

    /* PRNG run #1 */
    trng_reseed(prng_seed);
    trng_generate(true);

    n = trng_collect(prng, cnt);
    if (n != cnt) {
        TRNG_FAILED("PRNG_1 Auto-gen test failed: expected = %u, got = %u",
                    (unsigned)cnt, (unsigned)n);
    }

    /* TRNG, should not match PRNG */
    trng_reseed(NULL);
    trng_generate(true);

    n = trng_collect(rng, cnt);
    if (n != cnt) {
        TRNG_FAILED("TRNG Auto-gen test failed: expected = %u, got = %u",
                    (unsigned)cnt, (unsigned)n);
    }

    /* PRNG #2: should matches run #1 */
    trng_reseed(prng_seed);
    trng_generate(true);

    n = trng_collect(rng, cnt);
    if (n != cnt) {
        TRNG_FAILED("PRNG_2 Auto-gen test failed: expected = %u, got = %u",
                    (unsigned)cnt, (unsigned)n);
    }

    if (memcmp(rng, prng, sizeof(rng))) {
        TRNG_FAILED("PRNG_2 Auto-gen test failed: does not match PRNG_1");
    }
}

static void trng_test_oneshot(void)
{
    const size_t cnt = TEST_DATA_WORDS;
    uint32_t rng[TEST_DATA_WORDS];
    size_t n;

    trng_reset();

    /* PRNG run #1 */
    trng_reseed(prng_seed);
    trng_generate(false);

    n = trng_collect(rng, cnt);
    if (n == cnt) {
        TRNG_FAILED("PRNG_1 One-shot gen test failed");
    }

    /* TRNG, should not match PRNG */
    trng_reseed(NULL);
    trng_generate(false);

    n = trng_collect(rng, cnt);
    if (n == cnt) {
        TRNG_FAILED("TRNG One-shot test failed");
    }
}

static void trng_test_per_str(void)
{
    const size_t cnt = TEST_DATA_WORDS;
    uint32_t rng[TEST_DATA_WORDS], prng[TEST_DATA_WORDS];
    size_t n;

    trng_reset();

    /* #1: disabled */
    trng_ctrl_set(TRNG_CTRL_PERSODISABLE_MASK);
    trng_reseed(prng_seed);
    trng_ctrl_clr(TRNG_CTRL_PERSODISABLE_MASK);

    trng_generate(true);
    n = trng_collect(prng, cnt);
    g_assert_cmpuint(n, ==, cnt);

    /* #2: zero string should match personalization disabled */
    trng_load(R_TRNG_PER_STRNG_0, NULL);
    trng_reseed(prng_seed);

    trng_generate(true);
    n = trng_collect(rng, cnt);
    g_assert_cmpuint(n, ==, cnt);

    if (memcmp(rng, prng, sizeof(rng))) {
        TRNG_FAILED("Failed: PER_DISABLE != PER_STRNG_ALL_ZERO");
    }

    /* #3: non-zero string should not match personalization disabled */
    trng_load(R_TRNG_PER_STRNG_0, pers_str);
    trng_reseed(prng_seed);

    trng_generate(true);
    n = trng_collect(rng, cnt);
    g_assert_cmpuint(n, ==, cnt);

    if (!memcmp(rng, prng, sizeof(rng))) {
        TRNG_FAILED("Failed: PER_DISABLE == PER_STRNG_NON_ZERO");
    }
}

static void trng_test_forced_prng(void)
{
    const char *prop = "forced-prng";
    const uint64_t seed = 0xdeadbeefbad1bad0ULL;

    const size_t cnt = TEST_DATA_WORDS;
    uint32_t rng[TEST_DATA_WORDS], prng[TEST_DATA_WORDS];
    size_t n;

    trng_reset();
    trng_test_set_uint_prop(prop, seed);

    /* TRNG run #1 */
    trng_reset();
    trng_reseed(NULL);
    trng_generate(true);

    n = trng_collect(prng, cnt);
    g_assert_cmpuint(n, ==, cnt);

    /* TRNG run #2 should match run #1 */
    trng_reset();
    trng_reseed(NULL);
    trng_generate(true);

    n = trng_collect(rng, cnt);
    g_assert_cmpuint(n, ==, cnt);

    if (memcmp(rng, prng, sizeof(rng))) {
        TRNG_FAILED("Forced-prng test failed: results do not match");
    }
}

static void trng_test_fault_events(void)
{
    const char *prop = "fips-fault-events";

    trng_reset();

    /* Fault events only when TRSS is enabled */
    trng_write(R_TRNG_OSC_EN, TRNG_OSC_EN_VAL_MASK);
    trng_ctrl_set(TRNG_CTRL_TRSSEN_MASK);

    trng_test_set_uint_prop(prop, TRNG_STATUS_CERTF_MASK);
    trng_check_certf_status();

    trng_test_set_uint_prop(prop, TRNG_STATUS_DTF_MASK);
    trng_check_dtf_status();

    trng_reset();
}

int main(int argc, char **argv)
{
    int rc;

    g_test_init(&argc, &argv, NULL);

    #define TRNG_TEST_ADD(n) \
            qtest_add_func("/hw/misc/xlnx-versal-trng/" #n, trng_test_ ## n);
    TRNG_TEST_ADD(autogen);
    TRNG_TEST_ADD(oneshot);
    TRNG_TEST_ADD(per_str);
    TRNG_TEST_ADD(forced_prng);
    TRNG_TEST_ADD(fault_events);
    #undef TRNG_TEST_ADD

    trng_test_start();
    rc = g_test_run();
    trng_test_stop();

    return rc;
}
