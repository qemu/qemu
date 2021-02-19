/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "fpu/softfloat.h"
#include "macros.h"
#include "conv_emu.h"

#define LL_MAX_POS 0x7fffffffffffffffULL
#define MAX_POS 0x7fffffffU

static uint64_t conv_f64_to_8u_n(float64 in, int will_negate,
                                 float_status *fp_status)
{
    uint8_t sign = float64_is_neg(in);
    if (float64_is_infinity(in)) {
        float_raise(float_flag_invalid, fp_status);
        if (float64_is_neg(in)) {
            return 0ULL;
        } else {
            return ~0ULL;
        }
    }
    if (float64_is_any_nan(in)) {
        float_raise(float_flag_invalid, fp_status);
        return ~0ULL;
    }
    if (float64_is_zero(in)) {
        return 0;
    }
    if (sign) {
        float_raise(float_flag_invalid, fp_status);
        return 0;
    }
    if (float64_lt(in, float64_half, fp_status)) {
        /* Near zero, captures large fracshifts, denorms, etc */
        float_raise(float_flag_inexact, fp_status);
        switch (get_float_rounding_mode(fp_status)) {
        case float_round_down:
            if (will_negate) {
                return 1;
            } else {
                return 0;
            }
        case float_round_up:
            if (!will_negate) {
                return 1;
            } else {
                return 0;
            }
        default:
            return 0;    /* nearest or towards zero */
        }
    }
    return float64_to_uint64(in, fp_status);
}

static void clr_float_exception_flags(uint8_t flag, float_status *fp_status)
{
    uint8_t flags = fp_status->float_exception_flags;
    flags &= ~flag;
    set_float_exception_flags(flags, fp_status);
}

static uint32_t conv_df_to_4u_n(float64 fp64, int will_negate,
                                float_status *fp_status)
{
    uint64_t tmp;
    tmp = conv_f64_to_8u_n(fp64, will_negate, fp_status);
    if (tmp > 0x00000000ffffffffULL) {
        clr_float_exception_flags(float_flag_inexact, fp_status);
        float_raise(float_flag_invalid, fp_status);
        return ~0U;
    }
    return (uint32_t)tmp;
}

uint64_t conv_df_to_8u(float64 in, float_status *fp_status)
{
    return conv_f64_to_8u_n(in, 0, fp_status);
}

uint32_t conv_df_to_4u(float64 in, float_status *fp_status)
{
    return conv_df_to_4u_n(in, 0, fp_status);
}

int64_t conv_df_to_8s(float64 in, float_status *fp_status)
{
    uint8_t sign = float64_is_neg(in);
    uint64_t tmp;
    if (float64_is_any_nan(in)) {
        float_raise(float_flag_invalid, fp_status);
        return -1;
    }
    if (sign) {
        float64 minus_fp64 = float64_abs(in);
        tmp = conv_f64_to_8u_n(minus_fp64, 1, fp_status);
    } else {
        tmp = conv_f64_to_8u_n(in, 0, fp_status);
    }
    if (tmp > (LL_MAX_POS + sign)) {
        clr_float_exception_flags(float_flag_inexact, fp_status);
        float_raise(float_flag_invalid, fp_status);
        tmp = (LL_MAX_POS + sign);
    }
    if (sign) {
        return -tmp;
    } else {
        return tmp;
    }
}

int32_t conv_df_to_4s(float64 in, float_status *fp_status)
{
    uint8_t sign = float64_is_neg(in);
    uint64_t tmp;
    if (float64_is_any_nan(in)) {
        float_raise(float_flag_invalid, fp_status);
        return -1;
    }
    if (sign) {
        float64 minus_fp64 = float64_abs(in);
        tmp = conv_f64_to_8u_n(minus_fp64, 1, fp_status);
    } else {
        tmp = conv_f64_to_8u_n(in, 0, fp_status);
    }
    if (tmp > (MAX_POS + sign)) {
        clr_float_exception_flags(float_flag_inexact, fp_status);
        float_raise(float_flag_invalid, fp_status);
        tmp = (MAX_POS + sign);
    }
    if (sign) {
        return -tmp;
    } else {
        return tmp;
    }
}

uint64_t conv_sf_to_8u(float32 in, float_status *fp_status)
{
    float64 fp64 = float32_to_float64(in, fp_status);
    return conv_df_to_8u(fp64, fp_status);
}

uint32_t conv_sf_to_4u(float32 in, float_status *fp_status)
{
    float64 fp64 = float32_to_float64(in, fp_status);
    return conv_df_to_4u(fp64, fp_status);
}

int64_t conv_sf_to_8s(float32 in, float_status *fp_status)
{
    float64 fp64 = float32_to_float64(in, fp_status);
    return conv_df_to_8s(fp64, fp_status);
}

int32_t conv_sf_to_4s(float32 in, float_status *fp_status)
{
    float64 fp64 = float32_to_float64(in, fp_status);
    return conv_df_to_4s(fp64, fp_status);
}
