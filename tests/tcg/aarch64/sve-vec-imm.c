/*
 * Vector operations with immediates, at every SVE vector length.
 *
 * Each case first runs an operation with a given element size, then
 * an operation with an immediate at another element size, within the
 * same TB.  This catches TCG backends that encode the immediate from
 * the whole replicated constant, or apply it at a stale element size.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

#define MAXVL 256

static uint8_t in[MAXVL], out[MAXVL];

typedef void test_fn(void);
typedef uint64_t ref_fn(uint64_t x);

#define TEST(NAME, SETUP, OP)                                       \
static void NAME(void)                                              \
{                                                                   \
    asm volatile("ldr z0, [%0]\n\t"                                 \
                 "ldr z1, [%0]\n\t"                                 \
                 "mov z2.d, #0\n\t"                                 \
                 SETUP "\n\t"                                       \
                 OP "\n\t"                                          \
                 "str z0, [%1]"                                     \
                 : : "r"(in), "r"(out) : "z0", "z1", "z2", "memory"); \
}

#define SET_B  "add z1.b, z1.b, z1.b"
#define SET_D  "add z1.d, z1.d, z1.d"

static int64_t sext(uint64_t x, int bits)
{
    return (int64_t)(x << (64 - bits)) >> (64 - bits);
}

static uint64_t sat_s(int64_t x, int bits)
{
    int64_t max = (1ll << (bits - 1)) - 1;
    return x > max ? max : x < -max - 1 ? -max - 1 : x;
}

static uint64_t sat_u(int64_t x, int bits)
{
    int64_t max = (1ll << bits) - 1;
    return x > max ? max : x < 0 ? 0 : x;
}

static uint64_t ror(uint64_t x, int n, int bits)
{
    uint64_t m = bits == 64 ? -1ull : (1ull << bits) - 1;
    return ((x >> n) | (x << (bits - n))) & m;
}

/* Arithmetic immediates (add_vec, sub_vec, saturating ops). */
TEST(add_b5, SET_D, "add z0.b, z0.b, #5")
static uint64_t r_add_b5(uint64_t x) { return x + 5; }
TEST(add_h3, SET_B, "add z0.h, z0.h, #3")
static uint64_t r_add_h3(uint64_t x) { return x + 3; }
TEST(sub_s7, SET_B, "sub z0.s, z0.s, #7")
static uint64_t r_sub_s7(uint64_t x) { return x - 7; }
TEST(subr_h3, SET_B, "subr z0.h, z0.h, #3")
static uint64_t r_subr_h3(uint64_t x) { return 3 - x; }
TEST(sqadd_b3, SET_D, "sqadd z0.b, z0.b, #3")
static uint64_t r_sqadd_b3(uint64_t x) { return sat_s(sext(x, 8) + 3, 8); }
TEST(uqadd_h9, SET_B, "uqadd z0.h, z0.h, #9")
static uint64_t r_uqadd_h9(uint64_t x) { return sat_u(x + 9, 16); }
TEST(sqsub_s4, SET_B, "sqsub z0.s, z0.s, #4")
static uint64_t r_sqsub_s4(uint64_t x) { return sat_s(sext(x, 32) - 4, 32); }
TEST(uqsub_s2, SET_B, "uqsub z0.s, z0.s, #2")
static uint64_t r_uqsub_s2(uint64_t x) { return sat_u((int64_t)x - 2, 32); }

/* Logical immediates (and_vec, or_vec, xor_vec). */
TEST(and_d_f, SET_B, "and z0.d, z0.d, #0xf")
static uint64_t r_and_f(uint64_t x) { return x & 0xf; }
TEST(and_b_f, SET_D, "and z0.b, z0.b, #0xf")
TEST(orr_d_m16, SET_B, "orr z0.d, z0.d, #0xfffffffffffffff0")
static uint64_t r_orr_m16(uint64_t x) { return x | -16ull; }
TEST(eor_b_f, SET_D, "eor z0.b, z0.b, #0xf")
static uint64_t r_eor_f(uint64_t x) { return x ^ 0xf; }
TEST(orr_h_f, SET_D, "orr z0.h, z0.h, #0xf")
static uint64_t r_orr_f(uint64_t x) { return x | 0xf; }
TEST(eor_s_1, SET_B, "eor z0.s, z0.s, #0x1")
static uint64_t r_eor_1(uint64_t x) { return x ^ 1; }

/* Shift immediates, including counts above 15 and 31. */
TEST(lsl_d40, SET_B, "lsl z0.d, z0.d, #40")
static uint64_t r_lsl_d40(uint64_t x) { return x << 40; }
TEST(asr_d63, SET_B, "asr z0.d, z0.d, #63")
static uint64_t r_asr_d63(uint64_t x) { return sext(x, 64) >> 63; }
TEST(lsr_s20, SET_D, "lsr z0.s, z0.s, #20")
static uint64_t r_lsr_s20(uint64_t x) { return x >> 20; }
TEST(lsl_s31, SET_D, "lsl z0.s, z0.s, #31")
static uint64_t r_lsl_s31(uint64_t x) { return x << 31; }
TEST(asr_h15, SET_D, "asr z0.h, z0.h, #15")
static uint64_t r_asr_h15(uint64_t x) { return sext(x, 16) >> 15; }
TEST(xar_d40, SET_B, "xar z0.d, z0.d, z2.d, #40")
static uint64_t r_xar_d40(uint64_t x) { return ror(x, 40, 64); }
TEST(xar_s20, SET_D, "xar z0.s, z0.s, z2.s, #20")
static uint64_t r_xar_s20(uint64_t x) { return ror(x, 20, 32); }

/* Compare with zero (cmp_vec), including the adjusted LT/GE forms. */
TEST(cmeq_h0, SET_B, "cmeq v0.8h, v0.8h, #0")
static uint64_t r_cmeq0(uint64_t x) { return x == 0 ? -1ull : 0; }
TEST(cmgt_b0, SET_D, "cmgt v0.16b, v0.16b, #0")
static uint64_t r_cmgt_b0(uint64_t x) { return sext(x, 8) > 0 ? -1ull : 0; }
TEST(cmlt_b0, SET_D, "cmlt v0.16b, v0.16b, #0")
static uint64_t r_cmlt_b0(uint64_t x) { return sext(x, 8) < 0 ? -1ull : 0; }
TEST(cmlt_h0, SET_B, "cmlt v0.8h, v0.8h, #0")
static uint64_t r_cmlt_h0(uint64_t x) { return sext(x, 16) < 0 ? -1ull : 0; }
TEST(cmge_s0, SET_B, "cmge v0.4s, v0.4s, #0")
static uint64_t r_cmge_s0(uint64_t x) { return sext(x, 32) >= 0 ? -1ull : 0; }
TEST(cmle_d0, SET_B, "cmle v0.2d, v0.2d, #0")
static uint64_t r_cmle_d0(uint64_t x) { return sext(x, 64) <= 0 ? -1ull : 0; }

/* Vector constants (dupi_vec): vmv.v.i, 0/-1, and wide immediates. */
TEST(movi_s5, SET_B, "movi v0.4s, #5")
static uint64_t r_5(uint64_t x) { return 5; }
TEST(movi_h99, SET_D, "movi v0.8h, #0x99")
static uint64_t r_99(uint64_t x) { return 0x99; }
TEST(movi_d_mask, SET_B, "movi v0.2d, #0xff00ff00ff00ff00")
static uint64_t r_mask(uint64_t x) { return 0xff00ff00ff00ff00ull; }
TEST(mvni_s0, SET_D, "mvni v0.4s, #0")
static uint64_t r_m1(uint64_t x) { return -1ull; }

static const struct {
    const char *name;
    test_fn *fn;
    ref_fn *ref;
    int esz;        /* element size in bytes */
    int neon;       /* AdvSIMD op: only the low 16 bytes, rest zeroed */
} tests[] = {
    { "add .d; add .b, #5", add_b5, r_add_b5, 1 },
    { "add .b; add .h, #3", add_h3, r_add_h3, 2 },
    { "add .b; sub .s, #7", sub_s7, r_sub_s7, 4 },
    { "add .b; subr .h, #3", subr_h3, r_subr_h3, 2 },
    { "add .d; sqadd .b, #3", sqadd_b3, r_sqadd_b3, 1 },
    { "add .b; uqadd .h, #9", uqadd_h9, r_uqadd_h9, 2 },
    { "add .b; sqsub .s, #4", sqsub_s4, r_sqsub_s4, 4 },
    { "add .b; uqsub .s, #2", uqsub_s2, r_uqsub_s2, 4 },

    { "add .b; and .d, #0xf", and_d_f, r_and_f, 8 },
    { "add .d; and .b, #0xf", and_b_f, r_and_f, 1 },
    { "add .b; orr .d, #-16", orr_d_m16, r_orr_m16, 8 },
    { "add .d; eor .b, #0xf", eor_b_f, r_eor_f, 1 },
    { "add .d; orr .h, #0xf", orr_h_f, r_orr_f, 2 },
    { "add .b; eor .s, #1", eor_s_1, r_eor_1, 4 },

    { "add .b; lsl .d, #40", lsl_d40, r_lsl_d40, 8 },
    { "add .b; asr .d, #63", asr_d63, r_asr_d63, 8 },
    { "add .d; lsr .s, #20", lsr_s20, r_lsr_s20, 4 },
    { "add .d; lsl .s, #31", lsl_s31, r_lsl_s31, 4 },
    { "add .d; asr .h, #15", asr_h15, r_asr_h15, 2 },
    { "add .b; xar .d, #40", xar_d40, r_xar_d40, 8 },
    { "add .d; xar .s, #20", xar_s20, r_xar_s20, 4 },

    { "add .b; cmeq .8h, #0", cmeq_h0, r_cmeq0, 2, 1 },
    { "add .d; cmgt .16b, #0", cmgt_b0, r_cmgt_b0, 1, 1 },
    { "add .d; cmlt .16b, #0", cmlt_b0, r_cmlt_b0, 1, 1 },
    { "add .b; cmlt .8h, #0", cmlt_h0, r_cmlt_h0, 2, 1 },
    { "add .b; cmge .4s, #0", cmge_s0, r_cmge_s0, 4, 1 },
    { "add .b; cmle .2d, #0", cmle_d0, r_cmle_d0, 8, 1 },

    { "add .b; movi .4s, #5", movi_s5, r_5, 4, 1 },
    { "add .d; movi .8h, #0x99", movi_h99, r_99, 2, 1 },
    { "add .b; movi .2d, #0xff00...", movi_d_mask, r_mask, 8, 1 },
    { "add .d; mvni .4s, #0", mvni_s0, r_m1, 4, 1 },
};

static int check(int vl, int t)
{
    int esz = tests[t].esz;
    int n = tests[t].neon ? 16 : vl;
    uint64_t mask = esz == 8 ? -1ull : (1ull << (esz * 8)) - 1;

    for (int i = 0; i < vl; i += esz) {
        uint64_t x = 0, got = 0, exp = 0;

        memcpy(&x, in + i, esz);
        memcpy(&got, out + i, esz);
        if (i < n) {
            exp = tests[t].ref(x) & mask;
        }
        if (got != exp) {
            fprintf(stderr, "vl %d: %s: byte %d: got 0x%llx, "
                    "expected 0x%llx\n", vl, tests[t].name, i,
                    (unsigned long long)got, (unsigned long long)exp);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    int err = 0, nvl = 0;

    for (int i = 0; i < MAXVL; i++) {
        in[i] = 0xa5 ^ (i * 0x37);
    }
    /* Zero, saturating and sign-boundary elements in the low 16 bytes. */
    in[0] = 0x7e;                       /* sqadd .b overflow */
    in[2] = in[3] = 0;                  /* cmeq .h, zero */
    in[8] = 1;                          /* uqsub .s underflow */
    in[9] = in[10] = in[11] = 0;
    in[12] = 0xfa; in[13] = 0xff;       /* uqadd .h overflow */

    for (int vl = 16; vl <= MAXVL; vl *= 2) {
        if (prctl(PR_SVE_SET_VL, vl, 0, 0, 0, 0) != vl) {
            continue;
        }
        nvl++;
        for (int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
            memset(out, 0xee, sizeof(out));
            tests[t].fn();
            err |= check(vl, t);
        }
    }
    if (nvl == 0) {
        fprintf(stderr, "no SVE vector length available\n");
        return 1;
    }
    return err;
}
