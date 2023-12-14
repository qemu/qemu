/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "fpu/softfloat.h"
#include "cpu.h"
#include "fma_emu.h"
#include "arch.h"
#include "macros.h"

#define SF_BIAS        127
#define SF_MAXEXP      254
#define SF_MANTBITS    23
#define float32_nan    make_float32(0xffffffff)

/*
 * These three tables are used by the cabacdecbin instruction
 */
const uint8_t rLPS_table_64x4[64][4] = {
    {128, 176, 208, 240},
    {128, 167, 197, 227},
    {128, 158, 187, 216},
    {123, 150, 178, 205},
    {116, 142, 169, 195},
    {111, 135, 160, 185},
    {105, 128, 152, 175},
    {100, 122, 144, 166},
    {95, 116, 137, 158},
    {90, 110, 130, 150},
    {85, 104, 123, 142},
    {81, 99, 117, 135},
    {77, 94, 111, 128},
    {73, 89, 105, 122},
    {69, 85, 100, 116},
    {66, 80, 95, 110},
    {62, 76, 90, 104},
    {59, 72, 86, 99},
    {56, 69, 81, 94},
    {53, 65, 77, 89},
    {51, 62, 73, 85},
    {48, 59, 69, 80},
    {46, 56, 66, 76},
    {43, 53, 63, 72},
    {41, 50, 59, 69},
    {39, 48, 56, 65},
    {37, 45, 54, 62},
    {35, 43, 51, 59},
    {33, 41, 48, 56},
    {32, 39, 46, 53},
    {30, 37, 43, 50},
    {29, 35, 41, 48},
    {27, 33, 39, 45},
    {26, 31, 37, 43},
    {24, 30, 35, 41},
    {23, 28, 33, 39},
    {22, 27, 32, 37},
    {21, 26, 30, 35},
    {20, 24, 29, 33},
    {19, 23, 27, 31},
    {18, 22, 26, 30},
    {17, 21, 25, 28},
    {16, 20, 23, 27},
    {15, 19, 22, 25},
    {14, 18, 21, 24},
    {14, 17, 20, 23},
    {13, 16, 19, 22},
    {12, 15, 18, 21},
    {12, 14, 17, 20},
    {11, 14, 16, 19},
    {11, 13, 15, 18},
    {10, 12, 15, 17},
    {10, 12, 14, 16},
    {9, 11, 13, 15},
    {9, 11, 12, 14},
    {8, 10, 12, 14},
    {8, 9, 11, 13},
    {7, 9, 11, 12},
    {7, 9, 10, 12},
    {7, 8, 10, 11},
    {6, 8, 9, 11},
    {6, 7, 9, 10},
    {6, 7, 8, 9},
    {2, 2, 2, 2}
};

const uint8_t AC_next_state_MPS_64[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 62, 62, 63
};


const uint8_t AC_next_state_LPS_64[64] = {
    0, 0, 1, 2, 2, 4, 4, 5, 6, 7,
    8, 9, 9, 11, 11, 12, 13, 13, 15, 15,
    16, 16, 18, 18, 19, 19, 21, 21, 22, 22,
    23, 24, 24, 25, 26, 26, 27, 27, 28, 29,
    29, 30, 30, 30, 31, 32, 32, 33, 33, 33,
    34, 34, 35, 35, 35, 36, 36, 36, 37, 37,
    37, 38, 38, 63
};

#define BITS_MASK_8 0x5555555555555555ULL
#define PAIR_MASK_8 0x3333333333333333ULL
#define NYBL_MASK_8 0x0f0f0f0f0f0f0f0fULL
#define BYTE_MASK_8 0x00ff00ff00ff00ffULL
#define HALF_MASK_8 0x0000ffff0000ffffULL
#define WORD_MASK_8 0x00000000ffffffffULL

uint64_t interleave(uint32_t odd, uint32_t even)
{
    /* Convert to long long */
    uint64_t myodd = odd;
    uint64_t myeven = even;
    /* First, spread bits out */
    myodd = (myodd | (myodd << 16)) & HALF_MASK_8;
    myeven = (myeven | (myeven << 16)) & HALF_MASK_8;
    myodd = (myodd | (myodd << 8)) & BYTE_MASK_8;
    myeven = (myeven | (myeven << 8)) & BYTE_MASK_8;
    myodd = (myodd | (myodd << 4)) & NYBL_MASK_8;
    myeven = (myeven | (myeven << 4)) & NYBL_MASK_8;
    myodd = (myodd | (myodd << 2)) & PAIR_MASK_8;
    myeven = (myeven | (myeven << 2)) & PAIR_MASK_8;
    myodd = (myodd | (myodd << 1)) & BITS_MASK_8;
    myeven = (myeven | (myeven << 1)) & BITS_MASK_8;
    /* Now OR together */
    return myeven | (myodd << 1);
}

uint64_t deinterleave(uint64_t src)
{
    /* Get odd and even bits */
    uint64_t myodd = ((src >> 1) & BITS_MASK_8);
    uint64_t myeven = (src & BITS_MASK_8);

    /* Unspread bits */
    myeven = (myeven | (myeven >> 1)) & PAIR_MASK_8;
    myodd = (myodd | (myodd >> 1)) & PAIR_MASK_8;
    myeven = (myeven | (myeven >> 2)) & NYBL_MASK_8;
    myodd = (myodd | (myodd >> 2)) & NYBL_MASK_8;
    myeven = (myeven | (myeven >> 4)) & BYTE_MASK_8;
    myodd = (myodd | (myodd >> 4)) & BYTE_MASK_8;
    myeven = (myeven | (myeven >> 8)) & HALF_MASK_8;
    myodd = (myodd | (myodd >> 8)) & HALF_MASK_8;
    myeven = (myeven | (myeven >> 16)) & WORD_MASK_8;
    myodd = (myodd | (myodd >> 16)) & WORD_MASK_8;

    /* Return odd bits in upper half */
    return myeven | (myodd << 32);
}

int32_t conv_round(int32_t a, int n)
{
    int64_t val;

    if (n == 0) {
        val = a;
    } else if ((a & ((1 << (n - 1)) - 1)) == 0) {    /* N-1..0 all zero? */
        /* Add LSB from int part */
        val = ((fSE32_64(a)) + (int64_t) (((uint32_t) ((1 << n) & a)) >> 1));
    } else {
        val = ((fSE32_64(a)) + (1 << (n - 1)));
    }

    val = val >> n;
    return (int32_t)val;
}

/* Floating Point Stuff */

static const FloatRoundMode softfloat_roundingmodes[] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_down,
    float_round_up,
};

void arch_fpop_start(CPUHexagonState *env)
{
    set_float_exception_flags(0, &env->fp_status);
    set_float_rounding_mode(
        softfloat_roundingmodes[fREAD_REG_FIELD(USR, USR_FPRND)],
        &env->fp_status);
}

#ifdef CONFIG_USER_ONLY
/*
 * Hexagon Linux kernel only sets the relevant bits in USR (user status
 * register).  The exception isn't raised to user mode, so we don't
 * model it in qemu user mode.
 */
#define RAISE_FP_EXCEPTION   do {} while (0)
#endif

#define SOFTFLOAT_TEST_FLAG(FLAG, MYF, MYE) \
    do { \
        if (flags & FLAG) { \
            if (GET_USR_FIELD(USR_##MYF) == 0) { \
                SET_USR_FIELD(USR_##MYF, 1); \
                if (GET_USR_FIELD(USR_##MYE)) { \
                    RAISE_FP_EXCEPTION; \
                } \
            } \
        } \
    } while (0)

void arch_fpop_end(CPUHexagonState *env)
{
    const bool pkt_need_commit = true;
    int flags = get_float_exception_flags(&env->fp_status);
    if (flags != 0) {
        SOFTFLOAT_TEST_FLAG(float_flag_inexact, FPINPF, FPINPE);
        SOFTFLOAT_TEST_FLAG(float_flag_divbyzero, FPDBZF, FPDBZE);
        SOFTFLOAT_TEST_FLAG(float_flag_invalid, FPINVF, FPINVE);
        SOFTFLOAT_TEST_FLAG(float_flag_overflow, FPOVFF, FPOVFE);
        SOFTFLOAT_TEST_FLAG(float_flag_underflow, FPUNFF, FPUNFE);
    }
}

int arch_sf_recip_common(float32 *Rs, float32 *Rt, float32 *Rd, int *adjust,
                         float_status *fp_status)
{
    int n_exp;
    int d_exp;
    int ret = 0;
    float32 RsV, RtV, RdV;
    int PeV = 0;
    RsV = *Rs;
    RtV = *Rt;
    if (float32_is_any_nan(RsV) && float32_is_any_nan(RtV)) {
        if (extract32(RsV & RtV, 22, 1) == 0) {
            float_raise(float_flag_invalid, fp_status);
        }
        RdV = RsV = RtV = float32_nan;
    } else if (float32_is_any_nan(RsV)) {
        if (extract32(RsV, 22, 1) == 0) {
            float_raise(float_flag_invalid, fp_status);
        }
        RdV = RsV = RtV = float32_nan;
    } else if (float32_is_any_nan(RtV)) {
        /* or put NaN in num/den fixup? */
        if (extract32(RtV, 22, 1) == 0) {
            float_raise(float_flag_invalid, fp_status);
        }
        RdV = RsV = RtV = float32_nan;
    } else if (float32_is_infinity(RsV) && float32_is_infinity(RtV)) {
        /* or put Inf in num fixup? */
        RdV = RsV = RtV = float32_nan;
        float_raise(float_flag_invalid, fp_status);
    } else if (float32_is_zero(RsV) && float32_is_zero(RtV)) {
        /* or put zero in num fixup? */
        RdV = RsV = RtV = float32_nan;
        float_raise(float_flag_invalid, fp_status);
    } else if (float32_is_zero(RtV)) {
        /* or put Inf in num fixup? */
        uint8_t RsV_sign = float32_is_neg(RsV);
        uint8_t RtV_sign = float32_is_neg(RtV);
        /* Check that RsV is NOT infinite before we overwrite it */
        if (!float32_is_infinity(RsV)) {
            float_raise(float_flag_divbyzero, fp_status);
        }
        RsV = infinite_float32(RsV_sign ^ RtV_sign);
        RtV = float32_one;
        RdV = float32_one;
    } else if (float32_is_infinity(RtV)) {
        RsV = make_float32(0x80000000 & (RsV ^ RtV));
        RtV = float32_one;
        RdV = float32_one;
    } else if (float32_is_zero(RsV)) {
        /* Does this just work itself out? */
        /* No, 0/Inf causes problems. */
        RsV = make_float32(0x80000000 & (RsV ^ RtV));
        RtV = float32_one;
        RdV = float32_one;
    } else if (float32_is_infinity(RsV)) {
        uint8_t RsV_sign = float32_is_neg(RsV);
        uint8_t RtV_sign = float32_is_neg(RtV);
        RsV = infinite_float32(RsV_sign ^ RtV_sign);
        RtV = float32_one;
        RdV = float32_one;
    } else {
        PeV = 0x00;
        /* Basic checks passed */
        n_exp = float32_getexp_raw(RsV);
        d_exp = float32_getexp_raw(RtV);
        if ((n_exp - d_exp + SF_BIAS) <= SF_MANTBITS) {
            /* Near quotient underflow / inexact Q */
            PeV = 0x80;
            RtV = float32_scalbn(RtV, -64, fp_status);
            RsV = float32_scalbn(RsV, 64, fp_status);
        } else if ((n_exp - d_exp + SF_BIAS) > (SF_MAXEXP - 24)) {
            /* Near quotient overflow */
            PeV = 0x40;
            RtV = float32_scalbn(RtV, 32, fp_status);
            RsV = float32_scalbn(RsV, -32, fp_status);
        } else if (n_exp <= SF_MANTBITS + 2) {
            RtV = float32_scalbn(RtV, 64, fp_status);
            RsV = float32_scalbn(RsV, 64, fp_status);
        } else if (d_exp <= 1) {
            RtV = float32_scalbn(RtV, 32, fp_status);
            RsV = float32_scalbn(RsV, 32, fp_status);
        } else if (d_exp > 252) {
            RtV = float32_scalbn(RtV, -32, fp_status);
            RsV = float32_scalbn(RsV, -32, fp_status);
        }
        RdV = 0;
        ret = 1;
    }
    *Rs = RsV;
    *Rt = RtV;
    *Rd = RdV;
    *adjust = PeV;
    return ret;
}

int arch_sf_invsqrt_common(float32 *Rs, float32 *Rd, int *adjust,
                           float_status *fp_status)
{
    float32 RsV, RdV;
    int PeV = 0;
    int r_exp;
    int ret = 0;
    RsV = *Rs;
    if (float32_is_any_nan(RsV)) {
        if (extract32(RsV, 22, 1) == 0) {
            float_raise(float_flag_invalid, fp_status);
        }
        RdV = RsV = float32_nan;
    } else if (float32_lt(RsV, float32_zero, fp_status)) {
        /* Negative nonzero values are NaN */
        float_raise(float_flag_invalid, fp_status);
        RsV = float32_nan;
        RdV = float32_nan;
    } else if (float32_is_infinity(RsV)) {
        /* or put Inf in num fixup? */
        RsV = infinite_float32(1);
        RdV = infinite_float32(1);
    } else if (float32_is_zero(RsV)) {
        /* or put zero in num fixup? */
        RdV = float32_one;
    } else {
        PeV = 0x00;
        /* Basic checks passed */
        r_exp = float32_getexp(RsV);
        if (r_exp <= 24) {
            RsV = float32_scalbn(RsV, 64, fp_status);
            PeV = 0xe0;
        }
        RdV = 0;
        ret = 1;
    }
    *Rs = RsV;
    *Rd = RdV;
    *adjust = PeV;
    return ret;
}

const uint8_t recip_lookup_table[128] = {
    0x0fe, 0x0fa, 0x0f6, 0x0f2, 0x0ef, 0x0eb, 0x0e7, 0x0e4,
    0x0e0, 0x0dd, 0x0d9, 0x0d6, 0x0d2, 0x0cf, 0x0cc, 0x0c9,
    0x0c6, 0x0c2, 0x0bf, 0x0bc, 0x0b9, 0x0b6, 0x0b3, 0x0b1,
    0x0ae, 0x0ab, 0x0a8, 0x0a5, 0x0a3, 0x0a0, 0x09d, 0x09b,
    0x098, 0x096, 0x093, 0x091, 0x08e, 0x08c, 0x08a, 0x087,
    0x085, 0x083, 0x080, 0x07e, 0x07c, 0x07a, 0x078, 0x075,
    0x073, 0x071, 0x06f, 0x06d, 0x06b, 0x069, 0x067, 0x065,
    0x063, 0x061, 0x05f, 0x05e, 0x05c, 0x05a, 0x058, 0x056,
    0x054, 0x053, 0x051, 0x04f, 0x04e, 0x04c, 0x04a, 0x049,
    0x047, 0x045, 0x044, 0x042, 0x040, 0x03f, 0x03d, 0x03c,
    0x03a, 0x039, 0x037, 0x036, 0x034, 0x033, 0x032, 0x030,
    0x02f, 0x02d, 0x02c, 0x02b, 0x029, 0x028, 0x027, 0x025,
    0x024, 0x023, 0x021, 0x020, 0x01f, 0x01e, 0x01c, 0x01b,
    0x01a, 0x019, 0x017, 0x016, 0x015, 0x014, 0x013, 0x012,
    0x011, 0x00f, 0x00e, 0x00d, 0x00c, 0x00b, 0x00a, 0x009,
    0x008, 0x007, 0x006, 0x005, 0x004, 0x003, 0x002, 0x000,
};

const uint8_t invsqrt_lookup_table[128] = {
    0x069, 0x066, 0x063, 0x061, 0x05e, 0x05b, 0x059, 0x057,
    0x054, 0x052, 0x050, 0x04d, 0x04b, 0x049, 0x047, 0x045,
    0x043, 0x041, 0x03f, 0x03d, 0x03b, 0x039, 0x037, 0x036,
    0x034, 0x032, 0x030, 0x02f, 0x02d, 0x02c, 0x02a, 0x028,
    0x027, 0x025, 0x024, 0x022, 0x021, 0x01f, 0x01e, 0x01d,
    0x01b, 0x01a, 0x019, 0x017, 0x016, 0x015, 0x014, 0x012,
    0x011, 0x010, 0x00f, 0x00d, 0x00c, 0x00b, 0x00a, 0x009,
    0x008, 0x007, 0x006, 0x005, 0x004, 0x003, 0x002, 0x001,
    0x0fe, 0x0fa, 0x0f6, 0x0f3, 0x0ef, 0x0eb, 0x0e8, 0x0e4,
    0x0e1, 0x0de, 0x0db, 0x0d7, 0x0d4, 0x0d1, 0x0ce, 0x0cb,
    0x0c9, 0x0c6, 0x0c3, 0x0c0, 0x0be, 0x0bb, 0x0b8, 0x0b6,
    0x0b3, 0x0b1, 0x0af, 0x0ac, 0x0aa, 0x0a8, 0x0a5, 0x0a3,
    0x0a1, 0x09f, 0x09d, 0x09b, 0x099, 0x097, 0x095, 0x093,
    0x091, 0x08f, 0x08d, 0x08b, 0x089, 0x087, 0x086, 0x084,
    0x082, 0x080, 0x07f, 0x07d, 0x07b, 0x07a, 0x078, 0x077,
    0x075, 0x074, 0x072, 0x071, 0x06f, 0x06e, 0x06c, 0x06b,
};
