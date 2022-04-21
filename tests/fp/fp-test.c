/*
 * fp-test.c - test QEMU's softfloat implementation using Berkeley's Testfloat
 *
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * This file is derived from testfloat/source/testsoftfloat.c. Its copyright
 * info follows:
 *
 * Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017 The Regents of the
 * University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions, and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions, and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the University nor the names of its contributors may
 *     be used to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef HW_POISON_H
#error Must define HW_POISON_H to work around TARGET_* poisoning
#endif

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include <math.h>
#include "fpu/softfloat.h"
#include "platform.h"

#include "fail.h"
#include "slowfloat.h"
#include "functions.h"
#include "genCases.h"
#include "verCases.h"
#include "writeCase.h"
#include "testLoops.h"

typedef float16_t (*abz_f16)(float16_t, float16_t);
typedef bool (*ab_f16_z_bool)(float16_t, float16_t);
typedef float32_t (*abz_f32)(float32_t, float32_t);
typedef bool (*ab_f32_z_bool)(float32_t, float32_t);
typedef float64_t (*abz_f64)(float64_t, float64_t);
typedef bool (*ab_f64_z_bool)(float64_t, float64_t);
typedef void (*abz_extF80M)(const extFloat80_t *, const extFloat80_t *,
                            extFloat80_t *);
typedef bool (*ab_extF80M_z_bool)(const extFloat80_t *, const extFloat80_t *);
typedef void (*abz_f128M)(const float128_t *, const float128_t *, float128_t *);
typedef bool (*ab_f128M_z_bool)(const float128_t *, const float128_t *);

static const char * const round_mode_names[] = {
    [ROUND_NEAR_EVEN] = "even",
    [ROUND_MINMAG] = "zero",
    [ROUND_MIN] = "down",
    [ROUND_MAX] = "up",
    [ROUND_NEAR_MAXMAG] = "tieaway",
    [ROUND_ODD] = "odd",
};
static unsigned int *test_ops;
static unsigned int n_test_ops;
static unsigned int n_max_errors = 20;
static unsigned int test_round_mode = ROUND_NEAR_EVEN;
static unsigned int *round_modes;
static unsigned int n_round_modes;
static int test_level = 1;
static uint8_t slow_init_flags;
static uint8_t qemu_init_flags;

/* qemu softfloat status */
static float_status qsf;

static const char commands_string[] =
    "operations:\n"
    "    <int>_to_<float>            <float>_add      <float>_eq\n"
    "    <float>_to_<int>            <float>_sub      <float>_le\n"
    "    <float>_to_<int>_r_minMag   <float>_mul      <float>_lt\n"
    "    <float>_to_<float>          <float>_mulAdd   <float>_eq_signaling\n"
    "    <float>_roundToInt          <float>_div      <float>_le_quiet\n"
    "                                <float>_rem      <float>_lt_quiet\n"
    "                                <float>_sqrt\n"
    "    Where <int>: ui32, ui64, i32, i64\n"
    "          <float>: f16, f32, f64, extF80, f128\n"
    "    If no operation is provided, all the above are tested\n"
    "options:\n"
    " -e = max error count per test. Default: 20. Set no limit with 0\n"
    " -f = initial FP exception flags (vioux). Default: none\n"
    " -l = thoroughness level (1 (default), 2)\n"
    " -r = rounding mode (even (default), zero, down, up, tieaway, odd)\n"
    "      Set to 'all' to test all rounding modes, if applicable\n"
    " -s = stop when a test fails";

static void usage_complete(int argc, char *argv[])
{
    fprintf(stderr, "Usage: %s [options] [operation1 ...]\n", argv[0]);
    fprintf(stderr, "%s\n", commands_string);
    exit(EXIT_FAILURE);
}

/* keep wrappers separate but do not bother defining headers for all of them */
#include "wrap.c.inc"

static void not_implemented(void)
{
    fprintf(stderr, "Not implemented.\n");
}

static bool is_allowed(unsigned op, int rmode)
{
    /* odd has not been implemented for any 80-bit ops */
    if (rmode == softfloat_round_odd) {
        switch (op) {
        case EXTF80_TO_UI32:
        case EXTF80_TO_UI64:
        case EXTF80_TO_I32:
        case EXTF80_TO_I64:
        case EXTF80_TO_UI32_R_MINMAG:
        case EXTF80_TO_UI64_R_MINMAG:
        case EXTF80_TO_I32_R_MINMAG:
        case EXTF80_TO_I64_R_MINMAG:
        case EXTF80_TO_F16:
        case EXTF80_TO_F32:
        case EXTF80_TO_F64:
        case EXTF80_TO_F128:
        case EXTF80_ROUNDTOINT:
        case EXTF80_ADD:
        case EXTF80_SUB:
        case EXTF80_MUL:
        case EXTF80_DIV:
        case EXTF80_REM:
        case EXTF80_SQRT:
        case EXTF80_EQ:
        case EXTF80_LE:
        case EXTF80_LT:
        case EXTF80_EQ_SIGNALING:
        case EXTF80_LE_QUIET:
        case EXTF80_LT_QUIET:
        case UI32_TO_EXTF80:
        case UI64_TO_EXTF80:
        case I32_TO_EXTF80:
        case I64_TO_EXTF80:
        case F16_TO_EXTF80:
        case F32_TO_EXTF80:
        case F64_TO_EXTF80:
        case F128_TO_EXTF80:
            return false;
        }
    }
    return true;
}

static void do_testfloat(int op, int rmode, bool exact)
{
    abz_f16 true_abz_f16;
    abz_f16 subj_abz_f16;
    ab_f16_z_bool true_f16_z_bool;
    ab_f16_z_bool subj_f16_z_bool;
    abz_f32 true_abz_f32;
    abz_f32 subj_abz_f32;
    ab_f32_z_bool true_ab_f32_z_bool;
    ab_f32_z_bool subj_ab_f32_z_bool;
    abz_f64 true_abz_f64;
    abz_f64 subj_abz_f64;
    ab_f64_z_bool true_ab_f64_z_bool;
    ab_f64_z_bool subj_ab_f64_z_bool;
    abz_extF80M true_abz_extF80M;
    abz_extF80M subj_abz_extF80M;
    ab_extF80M_z_bool true_ab_extF80M_z_bool;
    ab_extF80M_z_bool subj_ab_extF80M_z_bool;
    abz_f128M true_abz_f128M;
    abz_f128M subj_abz_f128M;
    ab_f128M_z_bool true_ab_f128M_z_bool;
    ab_f128M_z_bool subj_ab_f128M_z_bool;

    fputs(">> Testing ", stderr);
    verCases_writeFunctionName(stderr);
    fputs("\n", stderr);

    if (!is_allowed(op, rmode)) {
        not_implemented();
        return;
    }

    switch (op) {
    case UI32_TO_F16:
        test_a_ui32_z_f16(slow_ui32_to_f16, qemu_ui32_to_f16);
        break;
    case UI32_TO_F32:
        test_a_ui32_z_f32(slow_ui32_to_f32, qemu_ui32_to_f32);
        break;
    case UI32_TO_F64:
        test_a_ui32_z_f64(slow_ui32_to_f64, qemu_ui32_to_f64);
        break;
    case UI32_TO_EXTF80:
        not_implemented();
        break;
    case UI32_TO_F128:
        not_implemented();
        break;
    case UI64_TO_F16:
        test_a_ui64_z_f16(slow_ui64_to_f16, qemu_ui64_to_f16);
        break;
    case UI64_TO_F32:
        test_a_ui64_z_f32(slow_ui64_to_f32, qemu_ui64_to_f32);
        break;
    case UI64_TO_F64:
        test_a_ui64_z_f64(slow_ui64_to_f64, qemu_ui64_to_f64);
        break;
    case UI64_TO_EXTF80:
        not_implemented();
        break;
    case UI64_TO_F128:
        test_a_ui64_z_f128(slow_ui64_to_f128M, qemu_ui64_to_f128M);
        break;
    case I32_TO_F16:
        test_a_i32_z_f16(slow_i32_to_f16, qemu_i32_to_f16);
        break;
    case I32_TO_F32:
        test_a_i32_z_f32(slow_i32_to_f32, qemu_i32_to_f32);
        break;
    case I32_TO_F64:
        test_a_i32_z_f64(slow_i32_to_f64, qemu_i32_to_f64);
        break;
    case I32_TO_EXTF80:
        test_a_i32_z_extF80(slow_i32_to_extF80M, qemu_i32_to_extF80M);
        break;
    case I32_TO_F128:
        test_a_i32_z_f128(slow_i32_to_f128M, qemu_i32_to_f128M);
        break;
    case I64_TO_F16:
        test_a_i64_z_f16(slow_i64_to_f16, qemu_i64_to_f16);
        break;
    case I64_TO_F32:
        test_a_i64_z_f32(slow_i64_to_f32, qemu_i64_to_f32);
        break;
    case I64_TO_F64:
        test_a_i64_z_f64(slow_i64_to_f64, qemu_i64_to_f64);
        break;
    case I64_TO_EXTF80:
        test_a_i64_z_extF80(slow_i64_to_extF80M, qemu_i64_to_extF80M);
        break;
    case I64_TO_F128:
        test_a_i64_z_f128(slow_i64_to_f128M, qemu_i64_to_f128M);
        break;
    case F16_TO_UI32:
        test_a_f16_z_ui32_rx(slow_f16_to_ui32, qemu_f16_to_ui32, rmode, exact);
        break;
    case F16_TO_UI64:
        test_a_f16_z_ui64_rx(slow_f16_to_ui64, qemu_f16_to_ui64, rmode, exact);
        break;
    case F16_TO_I32:
        test_a_f16_z_i32_rx(slow_f16_to_i32, qemu_f16_to_i32, rmode, exact);
        break;
    case F16_TO_I64:
        test_a_f16_z_i64_rx(slow_f16_to_i64, qemu_f16_to_i64, rmode, exact);
        break;
    case F16_TO_UI32_R_MINMAG:
        test_a_f16_z_ui32_x(slow_f16_to_ui32_r_minMag,
                            qemu_f16_to_ui32_r_minMag, exact);
        break;
    case F16_TO_UI64_R_MINMAG:
        test_a_f16_z_ui64_x(slow_f16_to_ui64_r_minMag,
                            qemu_f16_to_ui64_r_minMag, exact);
        break;
    case F16_TO_I32_R_MINMAG:
        test_a_f16_z_i32_x(slow_f16_to_i32_r_minMag, qemu_f16_to_i32_r_minMag,
                           exact);
        break;
    case F16_TO_I64_R_MINMAG:
        test_a_f16_z_i64_x(slow_f16_to_i64_r_minMag, qemu_f16_to_i64_r_minMag,
                           exact);
        break;
    case F16_TO_F32:
        test_a_f16_z_f32(slow_f16_to_f32, qemu_f16_to_f32);
        break;
    case F16_TO_F64:
        test_a_f16_z_f64(slow_f16_to_f64, qemu_f16_to_f64);
        break;
    case F16_TO_EXTF80:
        not_implemented();
        break;
    case F16_TO_F128:
        not_implemented();
        break;
    case F16_ROUNDTOINT:
        test_az_f16_rx(slow_f16_roundToInt, qemu_f16_roundToInt, rmode, exact);
        break;
    case F16_ADD:
        true_abz_f16 = slow_f16_add;
        subj_abz_f16 = qemu_f16_add;
        goto test_abz_f16;
    case F16_SUB:
        true_abz_f16 = slow_f16_sub;
        subj_abz_f16 = qemu_f16_sub;
        goto test_abz_f16;
    case F16_MUL:
        true_abz_f16 = slow_f16_mul;
        subj_abz_f16 = qemu_f16_mul;
        goto test_abz_f16;
    case F16_DIV:
        true_abz_f16 = slow_f16_div;
        subj_abz_f16 = qemu_f16_div;
        goto test_abz_f16;
    case F16_REM:
        not_implemented();
        break;
    test_abz_f16:
        test_abz_f16(true_abz_f16, subj_abz_f16);
        break;
    case F16_MULADD:
        test_abcz_f16(slow_f16_mulAdd, qemu_f16_mulAdd);
        break;
    case F16_SQRT:
        test_az_f16(slow_f16_sqrt, qemu_f16_sqrt);
        break;
    case F16_EQ:
        true_f16_z_bool = slow_f16_eq;
        subj_f16_z_bool = qemu_f16_eq;
        goto test_ab_f16_z_bool;
    case F16_LE:
        true_f16_z_bool = slow_f16_le;
        subj_f16_z_bool = qemu_f16_le;
        goto test_ab_f16_z_bool;
    case F16_LT:
        true_f16_z_bool = slow_f16_lt;
        subj_f16_z_bool = qemu_f16_lt;
        goto test_ab_f16_z_bool;
    case F16_EQ_SIGNALING:
        true_f16_z_bool = slow_f16_eq_signaling;
        subj_f16_z_bool = qemu_f16_eq_signaling;
        goto test_ab_f16_z_bool;
    case F16_LE_QUIET:
        true_f16_z_bool = slow_f16_le_quiet;
        subj_f16_z_bool = qemu_f16_le_quiet;
        goto test_ab_f16_z_bool;
    case F16_LT_QUIET:
        true_f16_z_bool = slow_f16_lt_quiet;
        subj_f16_z_bool = qemu_f16_lt_quiet;
    test_ab_f16_z_bool:
        test_ab_f16_z_bool(true_f16_z_bool, subj_f16_z_bool);
        break;
    case F32_TO_UI32:
        test_a_f32_z_ui32_rx(slow_f32_to_ui32, qemu_f32_to_ui32, rmode, exact);
        break;
    case F32_TO_UI64:
        test_a_f32_z_ui64_rx(slow_f32_to_ui64, qemu_f32_to_ui64, rmode, exact);
        break;
    case F32_TO_I32:
        test_a_f32_z_i32_rx(slow_f32_to_i32, qemu_f32_to_i32, rmode, exact);
        break;
    case F32_TO_I64:
        test_a_f32_z_i64_rx(slow_f32_to_i64, qemu_f32_to_i64, rmode, exact);
        break;
    case F32_TO_UI32_R_MINMAG:
        test_a_f32_z_ui32_x(slow_f32_to_ui32_r_minMag,
                            qemu_f32_to_ui32_r_minMag, exact);
        break;
    case F32_TO_UI64_R_MINMAG:
        test_a_f32_z_ui64_x(slow_f32_to_ui64_r_minMag,
                            qemu_f32_to_ui64_r_minMag, exact);
        break;
    case F32_TO_I32_R_MINMAG:
        test_a_f32_z_i32_x(slow_f32_to_i32_r_minMag, qemu_f32_to_i32_r_minMag,
                           exact);
        break;
    case F32_TO_I64_R_MINMAG:
        test_a_f32_z_i64_x(slow_f32_to_i64_r_minMag, qemu_f32_to_i64_r_minMag,
                           exact);
        break;
    case F32_TO_F16:
        test_a_f32_z_f16(slow_f32_to_f16, qemu_f32_to_f16);
        break;
    case F32_TO_F64:
        test_a_f32_z_f64(slow_f32_to_f64, qemu_f32_to_f64);
        break;
    case F32_TO_EXTF80:
        test_a_f32_z_extF80(slow_f32_to_extF80M, qemu_f32_to_extF80M);
        break;
    case F32_TO_F128:
        test_a_f32_z_f128(slow_f32_to_f128M, qemu_f32_to_f128M);
        break;
    case F32_ROUNDTOINT:
        test_az_f32_rx(slow_f32_roundToInt, qemu_f32_roundToInt, rmode, exact);
        break;
    case F32_ADD:
        true_abz_f32 = slow_f32_add;
        subj_abz_f32 = qemu_f32_add;
        goto test_abz_f32;
    case F32_SUB:
        true_abz_f32 = slow_f32_sub;
        subj_abz_f32 = qemu_f32_sub;
        goto test_abz_f32;
    case F32_MUL:
        true_abz_f32 = slow_f32_mul;
        subj_abz_f32 = qemu_f32_mul;
        goto test_abz_f32;
    case F32_DIV:
        true_abz_f32 = slow_f32_div;
        subj_abz_f32 = qemu_f32_div;
        goto test_abz_f32;
    case F32_REM:
        true_abz_f32 = slow_f32_rem;
        subj_abz_f32 = qemu_f32_rem;
    test_abz_f32:
        test_abz_f32(true_abz_f32, subj_abz_f32);
        break;
    case F32_MULADD:
        test_abcz_f32(slow_f32_mulAdd, qemu_f32_mulAdd);
        break;
    case F32_SQRT:
        test_az_f32(slow_f32_sqrt, qemu_f32_sqrt);
        break;
    case F32_EQ:
        true_ab_f32_z_bool = slow_f32_eq;
        subj_ab_f32_z_bool = qemu_f32_eq;
        goto test_ab_f32_z_bool;
    case F32_LE:
        true_ab_f32_z_bool = slow_f32_le;
        subj_ab_f32_z_bool = qemu_f32_le;
        goto test_ab_f32_z_bool;
    case F32_LT:
        true_ab_f32_z_bool = slow_f32_lt;
        subj_ab_f32_z_bool = qemu_f32_lt;
        goto test_ab_f32_z_bool;
    case F32_EQ_SIGNALING:
        true_ab_f32_z_bool = slow_f32_eq_signaling;
        subj_ab_f32_z_bool = qemu_f32_eq_signaling;
        goto test_ab_f32_z_bool;
    case F32_LE_QUIET:
        true_ab_f32_z_bool = slow_f32_le_quiet;
        subj_ab_f32_z_bool = qemu_f32_le_quiet;
        goto test_ab_f32_z_bool;
    case F32_LT_QUIET:
        true_ab_f32_z_bool = slow_f32_lt_quiet;
        subj_ab_f32_z_bool = qemu_f32_lt_quiet;
    test_ab_f32_z_bool:
        test_ab_f32_z_bool(true_ab_f32_z_bool, subj_ab_f32_z_bool);
        break;
    case F64_TO_UI32:
        test_a_f64_z_ui32_rx(slow_f64_to_ui32, qemu_f64_to_ui32, rmode, exact);
        break;
    case F64_TO_UI64:
        test_a_f64_z_ui64_rx(slow_f64_to_ui64, qemu_f64_to_ui64, rmode, exact);
        break;
    case F64_TO_I32:
        test_a_f64_z_i32_rx(slow_f64_to_i32, qemu_f64_to_i32, rmode, exact);
        break;
    case F64_TO_I64:
        test_a_f64_z_i64_rx(slow_f64_to_i64, qemu_f64_to_i64, rmode, exact);
        break;
    case F64_TO_UI32_R_MINMAG:
        test_a_f64_z_ui32_x(slow_f64_to_ui32_r_minMag,
                            qemu_f64_to_ui32_r_minMag, exact);
        break;
    case F64_TO_UI64_R_MINMAG:
        test_a_f64_z_ui64_x(slow_f64_to_ui64_r_minMag,
                            qemu_f64_to_ui64_r_minMag, exact);
        break;
    case F64_TO_I32_R_MINMAG:
        test_a_f64_z_i32_x(slow_f64_to_i32_r_minMag, qemu_f64_to_i32_r_minMag,
                           exact);
        break;
    case F64_TO_I64_R_MINMAG:
        test_a_f64_z_i64_x(slow_f64_to_i64_r_minMag, qemu_f64_to_i64_r_minMag,
                           exact);
        break;
    case F64_TO_F16:
        test_a_f64_z_f16(slow_f64_to_f16, qemu_f64_to_f16);
        break;
    case F64_TO_F32:
        test_a_f64_z_f32(slow_f64_to_f32, qemu_f64_to_f32);
        break;
    case F64_TO_EXTF80:
        test_a_f64_z_extF80(slow_f64_to_extF80M, qemu_f64_to_extF80M);
        break;
    case F64_TO_F128:
        test_a_f64_z_f128(slow_f64_to_f128M, qemu_f64_to_f128M);
        break;
    case F64_ROUNDTOINT:
        test_az_f64_rx(slow_f64_roundToInt, qemu_f64_roundToInt, rmode, exact);
        break;
    case F64_ADD:
        true_abz_f64 = slow_f64_add;
        subj_abz_f64 = qemu_f64_add;
        goto test_abz_f64;
    case F64_SUB:
        true_abz_f64 = slow_f64_sub;
        subj_abz_f64 = qemu_f64_sub;
        goto test_abz_f64;
    case F64_MUL:
        true_abz_f64 = slow_f64_mul;
        subj_abz_f64 = qemu_f64_mul;
        goto test_abz_f64;
    case F64_DIV:
        true_abz_f64 = slow_f64_div;
        subj_abz_f64 = qemu_f64_div;
        goto test_abz_f64;
    case F64_REM:
        true_abz_f64 = slow_f64_rem;
        subj_abz_f64 = qemu_f64_rem;
    test_abz_f64:
        test_abz_f64(true_abz_f64, subj_abz_f64);
        break;
    case F64_MULADD:
        test_abcz_f64(slow_f64_mulAdd, qemu_f64_mulAdd);
        break;
    case F64_SQRT:
        test_az_f64(slow_f64_sqrt, qemu_f64_sqrt);
        break;
    case F64_EQ:
        true_ab_f64_z_bool = slow_f64_eq;
        subj_ab_f64_z_bool = qemu_f64_eq;
        goto test_ab_f64_z_bool;
    case F64_LE:
        true_ab_f64_z_bool = slow_f64_le;
        subj_ab_f64_z_bool = qemu_f64_le;
        goto test_ab_f64_z_bool;
    case F64_LT:
        true_ab_f64_z_bool = slow_f64_lt;
        subj_ab_f64_z_bool = qemu_f64_lt;
        goto test_ab_f64_z_bool;
    case F64_EQ_SIGNALING:
        true_ab_f64_z_bool = slow_f64_eq_signaling;
        subj_ab_f64_z_bool = qemu_f64_eq_signaling;
        goto test_ab_f64_z_bool;
    case F64_LE_QUIET:
        true_ab_f64_z_bool = slow_f64_le_quiet;
        subj_ab_f64_z_bool = qemu_f64_le_quiet;
        goto test_ab_f64_z_bool;
    case F64_LT_QUIET:
        true_ab_f64_z_bool = slow_f64_lt_quiet;
        subj_ab_f64_z_bool = qemu_f64_lt_quiet;
    test_ab_f64_z_bool:
        test_ab_f64_z_bool(true_ab_f64_z_bool, subj_ab_f64_z_bool);
        break;
    case EXTF80_TO_UI32:
        not_implemented();
        break;
    case EXTF80_TO_UI64:
        not_implemented();
        break;
    case EXTF80_TO_I32:
        test_a_extF80_z_i32_rx(slow_extF80M_to_i32, qemu_extF80M_to_i32, rmode,
                               exact);
        break;
    case EXTF80_TO_I64:
        test_a_extF80_z_i64_rx(slow_extF80M_to_i64, qemu_extF80M_to_i64, rmode,
                               exact);
        break;
    case EXTF80_TO_UI32_R_MINMAG:
        not_implemented();
        break;
    case EXTF80_TO_UI64_R_MINMAG:
        not_implemented();
        break;
    case EXTF80_TO_I32_R_MINMAG:
        test_a_extF80_z_i32_x(slow_extF80M_to_i32_r_minMag,
                              qemu_extF80M_to_i32_r_minMag, exact);
        break;
    case EXTF80_TO_I64_R_MINMAG:
        test_a_extF80_z_i64_x(slow_extF80M_to_i64_r_minMag,
                              qemu_extF80M_to_i64_r_minMag, exact);
        break;
    case EXTF80_TO_F16:
        not_implemented();
        break;
    case EXTF80_TO_F32:
        test_a_extF80_z_f32(slow_extF80M_to_f32, qemu_extF80M_to_f32);
        break;
    case EXTF80_TO_F64:
        test_a_extF80_z_f64(slow_extF80M_to_f64, qemu_extF80M_to_f64);
        break;
    case EXTF80_TO_F128:
        test_a_extF80_z_f128(slow_extF80M_to_f128M, qemu_extF80M_to_f128M);
        break;
    case EXTF80_ROUNDTOINT:
        test_az_extF80_rx(slow_extF80M_roundToInt, qemu_extF80M_roundToInt,
                          rmode, exact);
        break;
    case EXTF80_ADD:
        true_abz_extF80M = slow_extF80M_add;
        subj_abz_extF80M = qemu_extF80M_add;
        goto test_abz_extF80;
    case EXTF80_SUB:
        true_abz_extF80M = slow_extF80M_sub;
        subj_abz_extF80M = qemu_extF80M_sub;
        goto test_abz_extF80;
    case EXTF80_MUL:
        true_abz_extF80M = slow_extF80M_mul;
        subj_abz_extF80M = qemu_extF80M_mul;
        goto test_abz_extF80;
    case EXTF80_DIV:
        true_abz_extF80M = slow_extF80M_div;
        subj_abz_extF80M = qemu_extF80M_div;
        goto test_abz_extF80;
    case EXTF80_REM:
        true_abz_extF80M = slow_extF80M_rem;
        subj_abz_extF80M = qemu_extF80M_rem;
    test_abz_extF80:
        test_abz_extF80(true_abz_extF80M, subj_abz_extF80M);
        break;
    case EXTF80_SQRT:
        test_az_extF80(slow_extF80M_sqrt, qemu_extF80M_sqrt);
        break;
    case EXTF80_EQ:
        true_ab_extF80M_z_bool = slow_extF80M_eq;
        subj_ab_extF80M_z_bool = qemu_extF80M_eq;
        goto test_ab_extF80_z_bool;
    case EXTF80_LE:
        true_ab_extF80M_z_bool = slow_extF80M_le;
        subj_ab_extF80M_z_bool = qemu_extF80M_le;
        goto test_ab_extF80_z_bool;
    case EXTF80_LT:
        true_ab_extF80M_z_bool = slow_extF80M_lt;
        subj_ab_extF80M_z_bool = qemu_extF80M_lt;
        goto test_ab_extF80_z_bool;
    case EXTF80_EQ_SIGNALING:
        true_ab_extF80M_z_bool = slow_extF80M_eq_signaling;
        subj_ab_extF80M_z_bool = qemu_extF80M_eq_signaling;
        goto test_ab_extF80_z_bool;
    case EXTF80_LE_QUIET:
        true_ab_extF80M_z_bool = slow_extF80M_le_quiet;
        subj_ab_extF80M_z_bool = qemu_extF80M_le_quiet;
        goto test_ab_extF80_z_bool;
    case EXTF80_LT_QUIET:
        true_ab_extF80M_z_bool = slow_extF80M_lt_quiet;
        subj_ab_extF80M_z_bool = qemu_extF80M_lt_quiet;
    test_ab_extF80_z_bool:
        test_ab_extF80_z_bool(true_ab_extF80M_z_bool, subj_ab_extF80M_z_bool);
        break;
    case F128_TO_UI32:
        test_a_f128_z_ui32_rx(slow_f128M_to_ui32, qemu_f128M_to_ui32, rmode,
                              exact);
        break;
    case F128_TO_UI64:
        test_a_f128_z_ui64_rx(slow_f128M_to_ui64, qemu_f128M_to_ui64, rmode,
                              exact);
        break;
    case F128_TO_I32:
        test_a_f128_z_i32_rx(slow_f128M_to_i32, qemu_f128M_to_i32, rmode,
                             exact);
        break;
    case F128_TO_I64:
        test_a_f128_z_i64_rx(slow_f128M_to_i64, qemu_f128M_to_i64, rmode,
                             exact);
        break;
    case F128_TO_UI32_R_MINMAG:
        test_a_f128_z_ui32_x(slow_f128M_to_ui32_r_minMag,
                             qemu_f128M_to_ui32_r_minMag, exact);
        break;
    case F128_TO_UI64_R_MINMAG:
        test_a_f128_z_ui64_x(slow_f128M_to_ui64_r_minMag,
                             qemu_f128M_to_ui64_r_minMag, exact);
        break;
    case F128_TO_I32_R_MINMAG:
        test_a_f128_z_i32_x(slow_f128M_to_i32_r_minMag,
                            qemu_f128M_to_i32_r_minMag, exact);
        break;
    case F128_TO_I64_R_MINMAG:
        test_a_f128_z_i64_x(slow_f128M_to_i64_r_minMag,
                            qemu_f128M_to_i64_r_minMag, exact);
        break;
    case F128_TO_F16:
        not_implemented();
        break;
    case F128_TO_F32:
        test_a_f128_z_f32(slow_f128M_to_f32, qemu_f128M_to_f32);
        break;
    case F128_TO_F64:
        test_a_f128_z_f64(slow_f128M_to_f64, qemu_f128M_to_f64);
        break;
    case F128_TO_EXTF80:
        test_a_f128_z_extF80(slow_f128M_to_extF80M, qemu_f128M_to_extF80M);
        break;
    case F128_ROUNDTOINT:
        test_az_f128_rx(slow_f128M_roundToInt, qemu_f128M_roundToInt, rmode,
                        exact);
        break;
    case F128_ADD:
        true_abz_f128M = slow_f128M_add;
        subj_abz_f128M = qemu_f128M_add;
        goto test_abz_f128;
    case F128_SUB:
        true_abz_f128M = slow_f128M_sub;
        subj_abz_f128M = qemu_f128M_sub;
        goto test_abz_f128;
    case F128_MUL:
        true_abz_f128M = slow_f128M_mul;
        subj_abz_f128M = qemu_f128M_mul;
        goto test_abz_f128;
    case F128_DIV:
        true_abz_f128M = slow_f128M_div;
        subj_abz_f128M = qemu_f128M_div;
        goto test_abz_f128;
    case F128_REM:
        true_abz_f128M = slow_f128M_rem;
        subj_abz_f128M = qemu_f128M_rem;
    test_abz_f128:
        test_abz_f128(true_abz_f128M, subj_abz_f128M);
        break;
    case F128_MULADD:
        test_abcz_f128(slow_f128M_mulAdd, qemu_f128M_mulAdd);
        break;
    case F128_SQRT:
        test_az_f128(slow_f128M_sqrt, qemu_f128M_sqrt);
        break;
    case F128_EQ:
        true_ab_f128M_z_bool = slow_f128M_eq;
        subj_ab_f128M_z_bool = qemu_f128M_eq;
        goto test_ab_f128_z_bool;
    case F128_LE:
        true_ab_f128M_z_bool = slow_f128M_le;
        subj_ab_f128M_z_bool = qemu_f128M_le;
        goto test_ab_f128_z_bool;
    case F128_LT:
        true_ab_f128M_z_bool = slow_f128M_lt;
        subj_ab_f128M_z_bool = qemu_f128M_lt;
        goto test_ab_f128_z_bool;
    case F128_EQ_SIGNALING:
        true_ab_f128M_z_bool = slow_f128M_eq_signaling;
        subj_ab_f128M_z_bool = qemu_f128M_eq_signaling;
        goto test_ab_f128_z_bool;
    case F128_LE_QUIET:
        true_ab_f128M_z_bool = slow_f128M_le_quiet;
        subj_ab_f128M_z_bool = qemu_f128M_le_quiet;
        goto test_ab_f128_z_bool;
    case F128_LT_QUIET:
        true_ab_f128M_z_bool = slow_f128M_lt_quiet;
        subj_ab_f128M_z_bool = qemu_f128M_lt_quiet;
    test_ab_f128_z_bool:
        test_ab_f128_z_bool(true_ab_f128M_z_bool, subj_ab_f128M_z_bool);
        break;
    }
    if ((verCases_errorStop && verCases_anyErrors)) {
        verCases_exitWithStatus();
    }
}

static unsigned int test_name_to_op(const char *arg)
{
    unsigned int i;

    /* counting begins at 1 */
    for (i = 1; i < NUM_FUNCTIONS; i++) {
        const char *name = functionInfos[i].namePtr;

        if (name && !strcmp(name, arg)) {
            return i;
        }
    }
    return 0;
}

static unsigned int round_name_to_mode(const char *name)
{
    int i;

    /* counting begins at 1 */
    for (i = 1; i < NUM_ROUNDINGMODES; i++) {
        if (!strcmp(round_mode_names[i], name)) {
            return i;
        }
    }
    return 0;
}

static int set_init_flags(const char *flags)
{
    const char *p;

    for (p = flags; *p != '\0'; p++) {
        switch (*p) {
        case 'v':
            slow_init_flags |= softfloat_flag_invalid;
            qemu_init_flags |= float_flag_invalid;
            break;
        case 'i':
            slow_init_flags |= softfloat_flag_infinite;
            qemu_init_flags |= float_flag_divbyzero;
            break;
        case 'o':
            slow_init_flags |= softfloat_flag_overflow;
            qemu_init_flags |= float_flag_overflow;
            break;
        case 'u':
            slow_init_flags |= softfloat_flag_underflow;
            qemu_init_flags |= float_flag_underflow;
            break;
        case 'x':
            slow_init_flags |= softfloat_flag_inexact;
            qemu_init_flags |= float_flag_inexact;
            break;
        default:
            return 1;
        }
    }
    return 0;
}

static uint_fast8_t slow_clear_flags(void)
{
    uint8_t prev = slowfloat_exceptionFlags;

    slowfloat_exceptionFlags = slow_init_flags;
    return prev;
}

static uint_fast8_t qemu_clear_flags(void)
{
    uint8_t prev = qemu_flags_to_sf(qsf.float_exception_flags);

    qsf.float_exception_flags = qemu_init_flags;
    return prev;
}

static void parse_args(int argc, char *argv[])
{
    unsigned int i;
    int c;

    for (;;) {
        c = getopt(argc, argv, "he:f:l:r:s");
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'h':
            usage_complete(argc, argv);
            exit(EXIT_SUCCESS);
        case 'e':
            if (qemu_strtoui(optarg, NULL, 0, &n_max_errors)) {
                fprintf(stderr, "fatal: invalid max error count\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            if (set_init_flags(optarg)) {
                fprintf(stderr, "fatal: flags must be a subset of 'vioux'\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'l':
            if (qemu_strtoi(optarg, NULL, 0, &test_level)) {
                fprintf(stderr, "fatal: invalid test level\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'r':
            if (!strcmp(optarg, "all")) {
                test_round_mode = 0;
            } else {
                test_round_mode = round_name_to_mode(optarg);
                if (test_round_mode == 0) {
                    fprintf(stderr, "fatal: invalid rounding mode\n");
                    exit(EXIT_FAILURE);
                }
            }
            break;
        case 's':
            verCases_errorStop = true;
            break;
        case '?':
            /* invalid option or missing argument; getopt prints error info */
            exit(EXIT_FAILURE);
        }
    }

    /* set rounding modes */
    if (test_round_mode == 0) {
        /* test all rounding modes; note that counting begins at 1 */
        n_round_modes = NUM_ROUNDINGMODES - 1;
        round_modes = g_malloc_n(n_round_modes, sizeof(*round_modes));
        for (i = 0; i < n_round_modes; i++) {
            round_modes[i] = i + 1;
        }
    } else {
        n_round_modes = 1;
        round_modes = g_malloc(sizeof(*round_modes));
        round_modes[0] = test_round_mode;
    }

    /* set test ops */
    if (optind == argc) {
        /* test all ops; note that counting begins at 1 */
        n_test_ops = NUM_FUNCTIONS - 1;
        test_ops = g_malloc_n(n_test_ops, sizeof(*test_ops));
        for (i = 0; i < n_test_ops; i++) {
            test_ops[i] = i + 1;
        }
    } else {
        n_test_ops = argc - optind;
        test_ops = g_malloc_n(n_test_ops, sizeof(*test_ops));
        for (i = 0; i < n_test_ops; i++) {
            const char *name = argv[i + optind];
            unsigned int op = test_name_to_op(name);

            if (op == 0) {
                fprintf(stderr, "fatal: invalid op '%s'\n", name);
                exit(EXIT_FAILURE);
            }
            test_ops[i] = op;
        }
    }
}

static G_NORETURN
void run_test(void)
{
    unsigned int i;

    genCases_setLevel(test_level);
    verCases_maxErrorCount = n_max_errors;

    testLoops_trueFlagsFunction = slow_clear_flags;
    testLoops_subjFlagsFunction = qemu_clear_flags;

    for (i = 0; i < n_test_ops; i++) {
        unsigned int op = test_ops[i];
        int j;

        if (functionInfos[op].namePtr == NULL) {
            continue;
        }
        verCases_functionNamePtr = functionInfos[op].namePtr;

        for (j = 0; j < n_round_modes; j++) {
            int attrs = functionInfos[op].attribs;
            int round = round_modes[j];
            int rmode = roundingModes[round];
            int k;

            verCases_roundingCode = 0;
            slowfloat_roundingMode = rmode;
            qsf.float_rounding_mode = sf_rounding_to_qemu(rmode);

            if (attrs & (FUNC_ARG_ROUNDINGMODE | FUNC_EFF_ROUNDINGMODE)) {
                /* print rounding mode if the op is affected by it */
                verCases_roundingCode = round;
            } else if (j > 0) {
                /* if the op is not sensitive to rounding, move on */
                break;
            }

            /* QEMU doesn't have !exact */
            verCases_exact = true;
            verCases_usesExact = !!(attrs & FUNC_ARG_EXACT);

            for (k = 0; k < 3; k++) {
                FloatX80RoundPrec qsf_prec80 = floatx80_precision_x;
                int prec80 = 80;
                int l;

                if (k == 1) {
                    prec80 = 64;
                    qsf_prec80 = floatx80_precision_d;
                } else if (k == 2) {
                    prec80 = 32;
                    qsf_prec80 = floatx80_precision_s;
                }

                verCases_roundingPrecision = 0;
                slow_extF80_roundingPrecision = prec80;
                qsf.floatx80_rounding_precision = qsf_prec80;

                if (attrs & FUNC_EFF_ROUNDINGPRECISION) {
                    verCases_roundingPrecision = prec80;
                } else if (k > 0) {
                    /* if the op is not sensitive to prec80, move on */
                    break;
                }

                /* note: the count begins at 1 */
                for (l = 1; l < NUM_TININESSMODES; l++) {
                    int tmode = tininessModes[l];

                    verCases_tininessCode = 0;
                    slowfloat_detectTininess = tmode;
                    qsf.tininess_before_rounding = sf_tininess_to_qemu(tmode);

                    if (attrs & FUNC_EFF_TININESSMODE ||
                        ((attrs & FUNC_EFF_TININESSMODE_REDUCEDPREC) &&
                         prec80 && prec80 < 80)) {
                        verCases_tininessCode = l;
                    } else if (l > 1) {
                        /* if the op is not sensitive to tininess, move on */
                        break;
                    }

                    do_testfloat(op, rmode, true);
                }
            }
        }
    }
    verCases_exitWithStatus();
    /* old compilers might miss that we exited */
    g_assert_not_reached();
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    fail_programName = argv[0];
    run_test(); /* does not return */
}
