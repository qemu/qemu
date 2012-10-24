/*
 * MIPS ASE DSP Instruction emulation helpers for QEMU.
 *
 * Copyright (c) 2012  Jia Liu <proljc@gmail.com>
 *                     Dongxue Zhang <elat.era@gmail.com>
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"

/*** MIPS DSP internal functions begin ***/
#define MIPSDSP_ABS(x) (((x) >= 0) ? x : -x)
#define MIPSDSP_OVERFLOW(a, b, c, d) (!(!((a ^ b ^ -1) & (a ^ c) & d)))

static inline void set_DSPControl_overflow_flag(uint32_t flag, int position,
                                                CPUMIPSState *env)
{
    env->active_tc.DSPControl |= (target_ulong)flag << position;
}

static inline void set_DSPControl_carryflag(uint32_t flag, CPUMIPSState *env)
{
    env->active_tc.DSPControl |= (target_ulong)flag << 13;
}

static inline uint32_t get_DSPControl_carryflag(CPUMIPSState *env)
{
    return (env->active_tc.DSPControl >> 13) & 0x01;
}

static inline void set_DSPControl_24(uint32_t flag, int len, CPUMIPSState *env)
{
  uint32_t filter;

  filter = ((0x01 << len) - 1) << 24;
  filter = ~filter;

  env->active_tc.DSPControl &= filter;
  env->active_tc.DSPControl |= (target_ulong)flag << 24;
}

static inline uint32_t get_DSPControl_24(int len, CPUMIPSState *env)
{
  uint32_t filter;

  filter = (0x01 << len) - 1;

  return (env->active_tc.DSPControl >> 24) & filter;
}

static inline void set_DSPControl_pos(uint32_t pos, CPUMIPSState *env)
{
    target_ulong dspc;

    dspc = env->active_tc.DSPControl;
#ifndef TARGET_MIPS64
    dspc = dspc & 0xFFFFFFC0;
    dspc |= pos;
#else
    dspc = dspc & 0xFFFFFF80;
    dspc |= pos;
#endif
    env->active_tc.DSPControl = dspc;
}

static inline uint32_t get_DSPControl_pos(CPUMIPSState *env)
{
    target_ulong dspc;
    uint32_t pos;

    dspc = env->active_tc.DSPControl;

#ifndef TARGET_MIPS64
    pos = dspc & 0x3F;
#else
    pos = dspc & 0x7F;
#endif

    return pos;
}

static inline void set_DSPControl_efi(uint32_t flag, CPUMIPSState *env)
{
    env->active_tc.DSPControl &= 0xFFFFBFFF;
    env->active_tc.DSPControl |= (target_ulong)flag << 14;
}

#define DO_MIPS_SAT_ABS(size)                                          \
static inline int##size##_t mipsdsp_sat_abs##size(int##size##_t a,         \
                                                  CPUMIPSState *env)   \
{                                                                      \
    if (a == INT##size##_MIN) {                                        \
        set_DSPControl_overflow_flag(1, 20, env);                      \
        return INT##size##_MAX;                                        \
    } else {                                                           \
        return MIPSDSP_ABS(a);                                         \
    }                                                                  \
}
DO_MIPS_SAT_ABS(8)
DO_MIPS_SAT_ABS(16)
DO_MIPS_SAT_ABS(32)
#undef DO_MIPS_SAT_ABS

/* get sum value */
static inline int16_t mipsdsp_add_i16(int16_t a, int16_t b, CPUMIPSState *env)
{
    int16_t tempI;

    tempI = a + b;

    if (MIPSDSP_OVERFLOW(a, b, tempI, 0x8000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempI;
}

static inline int16_t mipsdsp_sat_add_i16(int16_t a, int16_t b,
                                          CPUMIPSState *env)
{
    int16_t tempS;

    tempS = a + b;

    if (MIPSDSP_OVERFLOW(a, b, tempS, 0x8000)) {
        if (a > 0) {
            tempS = 0x7FFF;
        } else {
            tempS = 0x8000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempS;
}

static inline int32_t mipsdsp_sat_add_i32(int32_t a, int32_t b,
                                          CPUMIPSState *env)
{
    int32_t tempI;

    tempI = a + b;

    if (MIPSDSP_OVERFLOW(a, b, tempI, 0x80000000)) {
        if (a > 0) {
            tempI = 0x7FFFFFFF;
        } else {
            tempI = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempI;
}

static inline uint8_t mipsdsp_add_u8(uint8_t a, uint8_t b, CPUMIPSState *env)
{
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b;

    if (temp & 0x0100) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0xFF;
}

static inline uint16_t mipsdsp_add_u16(uint16_t a, uint16_t b,
                                       CPUMIPSState *env)
{
    uint32_t temp;

    temp = (uint32_t)a + (uint32_t)b;

    if (temp & 0x00010000) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0xFFFF;
}

static inline uint8_t mipsdsp_sat_add_u8(uint8_t a, uint8_t b,
                                         CPUMIPSState *env)
{
    uint8_t  result;
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b;
    result = temp & 0xFF;

    if (0x0100 & temp) {
        result = 0xFF;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return result;
}

static inline uint16_t mipsdsp_sat_add_u16(uint16_t a, uint16_t b,
                                           CPUMIPSState *env)
{
    uint16_t result;
    uint32_t temp;

    temp = (uint32_t)a + (uint32_t)b;
    result = temp & 0xFFFF;

    if (0x00010000 & temp) {
        result = 0xFFFF;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return result;
}

static inline int32_t mipsdsp_sat32_acc_q31(int32_t acc, int32_t a,
                                            CPUMIPSState *env)
{
    int64_t temp;
    int32_t temp32, temp31, result;
    int64_t temp_sum;

#ifndef TARGET_MIPS64
    temp = ((uint64_t)env->active_tc.HI[acc] << 32) |
           (uint64_t)env->active_tc.LO[acc];
#else
    temp = (uint64_t)env->active_tc.LO[acc];
#endif

    temp_sum = (int64_t)a + temp;

    temp32 = (temp_sum >> 32) & 0x01;
    temp31 = (temp_sum >> 31) & 0x01;
    result = temp_sum & 0xFFFFFFFF;

    /* FIXME
       This sat function may wrong, because user manual wrote:
       temp127..0 ← temp + ( (signA) || a31..0
       if ( temp32 ≠ temp31 ) then
           if ( temp32 = 0 ) then
               temp31..0 ← 0x80000000
           else
                temp31..0 ← 0x7FFFFFFF
           endif
           DSPControlouflag:16+acc ← 1
       endif
     */
    if (temp32 != temp31) {
        if (temp32 == 0) {
            result = 0x7FFFFFFF;
        } else {
            result = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 16 + acc, env);
    }

    return result;
}

/* a[0] is LO, a[1] is HI. */
static inline void mipsdsp_sat64_acc_add_q63(int64_t *ret,
                                             int32_t ac,
                                             int64_t *a,
                                             CPUMIPSState *env)
{
    bool temp64;

    ret[0] = env->active_tc.LO[ac] + a[0];
    ret[1] = env->active_tc.HI[ac] + a[1];

    if (((uint64_t)ret[0] < (uint64_t)env->active_tc.LO[ac]) &&
        ((uint64_t)ret[0] < (uint64_t)a[0])) {
        ret[1] += 1;
    }
    temp64 = ret[1] & 1;
    if (temp64 != ((ret[0] >> 63) & 0x01)) {
        if (temp64) {
            ret[0] = (0x01ull << 63);
            ret[1] = ~0ull;
        } else {
            ret[0] = (0x01ull << 63) - 1;
            ret[1] = 0x00;
        }
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    }
}

static inline void mipsdsp_sat64_acc_sub_q63(int64_t *ret,
                                             int32_t ac,
                                             int64_t *a,
                                             CPUMIPSState *env)
{
    bool temp64;

    ret[0] = env->active_tc.LO[ac] - a[0];
    ret[1] = env->active_tc.HI[ac] - a[1];

    if ((uint64_t)ret[0] > (uint64_t)env->active_tc.LO[ac]) {
        ret[1] -= 1;
    }
    temp64 = ret[1] & 1;
    if (temp64 != ((ret[0] >> 63) & 0x01)) {
        if (temp64) {
            ret[0] = (0x01ull << 63);
            ret[1] = ~0ull;
        } else {
            ret[0] = (0x01ull << 63) - 1;
            ret[1] = 0x00;
        }
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    }
}

static inline int32_t mipsdsp_mul_i16_i16(int16_t a, int16_t b,
                                          CPUMIPSState *env)
{
    int32_t temp;

    temp = (int32_t)a * (int32_t)b;

    if ((temp > (int)0x7FFF) || (temp < (int)0xFFFF8000)) {
        set_DSPControl_overflow_flag(1, 21, env);
    }
    temp &= 0x0000FFFF;

    return temp;
}

static inline int32_t mipsdsp_mul_u16_u16(int32_t a, int32_t b)
{
    return a * b;
}

static inline int32_t mipsdsp_mul_i32_i32(int32_t a, int32_t b)
{
    return a * b;
}

static inline int32_t mipsdsp_sat16_mul_i16_i16(int16_t a, int16_t b,
                                                CPUMIPSState *env)
{
    int32_t temp;

    temp = (int32_t)a * (int32_t)b;

    if (temp > (int)0x7FFF) {
        temp = 0x00007FFF;
        set_DSPControl_overflow_flag(1, 21, env);
    } else if (temp < (int)0xffff8000) {
        temp = 0xFFFF8000;
        set_DSPControl_overflow_flag(1, 21, env);
    }
    temp &= 0x0000FFFF;

    return temp;
}

static inline int32_t mipsdsp_mul_q15_q15_overflowflag21(uint16_t a, uint16_t b,
                                                         CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = ((int32_t)(int16_t)a * (int32_t)(int16_t)b) << 1;
    }

    return temp;
}

/* right shift */
static inline uint8_t mipsdsp_rshift_u8(uint8_t a, target_ulong mov)
{
    return a >> mov;
}

static inline uint16_t mipsdsp_rshift_u16(uint16_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int8_t mipsdsp_rashift8(int8_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int16_t mipsdsp_rashift16(int16_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int32_t mipsdsp_rashift32(int32_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int16_t mipsdsp_rshift1_add_q16(int16_t a, int16_t b)
{
    int32_t temp;

    temp = (int32_t)a + (int32_t)b;

    return (temp >> 1) & 0xFFFF;
}

/* round right shift */
static inline int16_t mipsdsp_rrshift1_add_q16(int16_t a, int16_t b)
{
    int32_t temp;

    temp = (int32_t)a + (int32_t)b;
    temp += 1;

    return (temp >> 1) & 0xFFFF;
}

static inline int32_t mipsdsp_rshift1_add_q32(int32_t a, int32_t b)
{
    int64_t temp;

    temp = (int64_t)a + (int64_t)b;

    return (temp >> 1) & 0xFFFFFFFF;
}

static inline int32_t mipsdsp_rrshift1_add_q32(int32_t a, int32_t b)
{
    int64_t temp;

    temp = (int64_t)a + (int64_t)b;
    temp += 1;

    return (temp >> 1) & 0xFFFFFFFF;
}

static inline uint8_t mipsdsp_rshift1_add_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b;

    return (temp >> 1) & 0x00FF;
}

static inline uint8_t mipsdsp_rrshift1_add_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b + 1;

    return (temp >> 1) & 0x00FF;
}

static inline uint8_t mipsdsp_rshift1_sub_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b;

    return (temp >> 1) & 0x00FF;
}

static inline uint8_t mipsdsp_rrshift1_sub_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b + 1;

    return (temp >> 1) & 0x00FF;
}

static inline int64_t mipsdsp_rashift_short_acc(int32_t ac,
                                                int32_t shift,
                                                CPUMIPSState *env)
{
    int32_t sign, temp31;
    int64_t temp, acc;

    sign = (env->active_tc.HI[ac] >> 31) & 0x01;
    acc = ((int64_t)env->active_tc.HI[ac] << 32) |
          ((int64_t)env->active_tc.LO[ac] & 0xFFFFFFFF);
    if (shift == 0) {
        temp = acc;
    } else {
        if (sign == 0) {
            temp = (((int64_t)0x01 << (32 - shift + 1)) - 1) & (acc >> shift);
        } else {
            temp = ((((int64_t)0x01 << (shift + 1)) - 1) << (32 - shift)) |
                   (acc >> shift);
        }
    }

    temp31 = (temp >> 31) & 0x01;
    if (sign != temp31) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return temp;
}

/*  128 bits long. p[0] is LO, p[1] is HI. */
static inline void mipsdsp_rndrashift_short_acc(int64_t *p,
                                                int32_t ac,
                                                int32_t shift,
                                                CPUMIPSState *env)
{
    int64_t acc;

    acc = ((int64_t)env->active_tc.HI[ac] << 32) |
          ((int64_t)env->active_tc.LO[ac] & 0xFFFFFFFF);
    if (shift == 0) {
        p[0] = acc << 1;
        p[1] = (acc >> 63) & 0x01;
    } else {
        p[0] = acc >> (shift - 1);
        p[1] = 0;
    }
}

/* 128 bits long. p[0] is LO, p[1] is HI */
static inline void mipsdsp_rashift_acc(uint64_t *p,
                                       uint32_t ac,
                                       uint32_t shift,
                                       CPUMIPSState *env)
{
    uint64_t tempB, tempA;

    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];
    shift = shift & 0x1F;

    if (shift == 0) {
        p[1] = tempB;
        p[0] = tempA;
    } else {
        p[0] = (tempB << (64 - shift)) | (tempA >> shift);
        p[1] = (int64_t)tempB >> shift;
    }
}

/* 128 bits long. p[0] is LO, p[1] is HI , p[2] is sign of HI.*/
static inline void mipsdsp_rndrashift_acc(uint64_t *p,
                                          uint32_t ac,
                                          uint32_t shift,
                                          CPUMIPSState *env)
{
    int64_t tempB, tempA;

    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];
    shift = shift & 0x3F;

    if (shift == 0) {
        p[2] = tempB >> 63;
        p[1] = (tempB << 1) | (tempA >> 63);
        p[0] = tempA << 1;
    } else {
        p[0] = (tempB << (65 - shift)) | (tempA >> (shift - 1));
        p[1] = (int64_t)tempB >> (shift - 1);
        if (tempB >= 0) {
            p[2] = 0x0;
        } else {
            p[2] = ~0ull;
        }
    }
}

static inline int32_t mipsdsp_mul_q15_q15(int32_t ac, uint16_t a, uint16_t b,
                                          CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    } else {
        temp = ((uint32_t)a * (uint32_t)b) << 1;
    }

    return temp;
}

static inline int64_t mipsdsp_mul_q31_q31(int32_t ac, uint32_t a, uint32_t b,
                                          CPUMIPSState *env)
{
    uint64_t temp;

    if ((a == 0x80000000) && (b == 0x80000000)) {
        temp = (0x01ull << 63) - 1;
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    } else {
        temp = ((uint64_t)a * (uint64_t)b) << 1;
    }

    return temp;
}

static inline uint16_t mipsdsp_mul_u8_u8(uint8_t a, uint8_t b)
{
    return (uint16_t)a * (uint16_t)b;
}

static inline uint16_t mipsdsp_mul_u8_u16(uint8_t a, uint16_t b,
                                          CPUMIPSState *env)
{
    uint32_t tempI;

    tempI = (uint32_t)a * (uint32_t)b;
    if (tempI > 0x0000FFFF) {
        tempI = 0x0000FFFF;
        set_DSPControl_overflow_flag(1, 21, env);
    }

    return tempI & 0x0000FFFF;
}

static inline uint64_t mipsdsp_mul_u32_u32(uint32_t a, uint32_t b)
{
    return (uint64_t)a * (uint64_t)b;
}

static inline int16_t mipsdsp_rndq15_mul_q15_q15(uint16_t a, uint16_t b,
                                                 CPUMIPSState *env)
{
    uint32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFF0000;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = (a * b) << 1;
        temp = temp + 0x00008000;
    }

    return (temp & 0xFFFF0000) >> 16;
}

static inline int32_t mipsdsp_sat16_mul_q15_q15(uint16_t a, uint16_t b,
                                                CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFF0000;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = ((uint32_t)a * (uint32_t)b);
        temp = temp << 1;
    }

    return (temp >> 16) & 0x0000FFFF;
}

static inline uint16_t mipsdsp_trunc16_sat16_round(int32_t a,
                                                   CPUMIPSState *env)
{
    int64_t temp;

    temp = (int32_t)a + 0x00008000;

    if (a > (int)0x7fff8000) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 22, env);
    }

    return (temp >> 16) & 0xFFFF;
}

static inline uint8_t mipsdsp_sat8_reduce_precision(uint16_t a,
                                                    CPUMIPSState *env)
{
    uint16_t mag;
    uint32_t sign;

    sign = (a >> 15) & 0x01;
    mag = a & 0x7FFF;

    if (sign == 0) {
        if (mag > 0x7F80) {
            set_DSPControl_overflow_flag(1, 22, env);
            return 0xFF;
        } else {
            return (mag >> 7) & 0xFFFF;
        }
    } else {
        set_DSPControl_overflow_flag(1, 22, env);
        return 0x00;
    }
}

static inline uint8_t mipsdsp_lshift8(uint8_t a, uint8_t s, CPUMIPSState *env)
{
    uint8_t sign;
    uint8_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 7) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (8 - s)) - 1) << s) |
                      ((a >> (6 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (6 - (s - 1));
        }

        if (discard != 0x00) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
        return a << s;
    }
}

static inline uint16_t mipsdsp_lshift16(uint16_t a, uint8_t s,
                                        CPUMIPSState *env)
{
    uint8_t  sign;
    uint16_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 15) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (16 - s)) - 1) << s) |
                      ((a >> (14 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (14 - (s - 1));
        }

        if ((discard != 0x0000) && (discard != 0xFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
        return a << s;
    }
}


static inline uint32_t mipsdsp_lshift32(uint32_t a, uint8_t s,
                                        CPUMIPSState *env)
{
    uint32_t discard;

    if (s == 0) {
        return a;
    } else {
        discard = (int32_t)a >> (31 - (s - 1));

        if ((discard != 0x00000000) && (discard != 0xFFFFFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
        return a << s;
    }
}

static inline uint16_t mipsdsp_sat16_lshift(uint16_t a, uint8_t s,
                                            CPUMIPSState *env)
{
    uint8_t  sign;
    uint16_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 15) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (16 - s)) - 1) << s) |
                      ((a >> (14 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (14 - (s - 1));
        }

        if ((discard != 0x0000) && (discard != 0xFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
            return (sign == 0) ? 0x7FFF : 0x8000;
        } else {
            return a << s;
        }
    }
}

static inline uint32_t mipsdsp_sat32_lshift(uint32_t a, uint8_t s,
                                            CPUMIPSState *env)
{
    uint8_t  sign;
    uint32_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 31) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (32 - s)) - 1) << s) |
                      ((a >> (30 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (30 - (s - 1));
        }

        if ((discard != 0x00000000) && (discard != 0xFFFFFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
            return (sign == 0) ? 0x7FFFFFFF : 0x80000000;
        } else {
            return a << s;
        }
    }
}

static inline uint8_t mipsdsp_rnd8_rashift(uint8_t a, uint8_t s)
{
    uint32_t temp;

    if (s == 0) {
        temp = (uint32_t)a << 1;
    } else {
        temp = (int32_t)(int8_t)a >> (s - 1);
    }

    return (temp + 1) >> 1;
}

static inline uint16_t mipsdsp_rnd16_rashift(uint16_t a, uint8_t s)
{
    uint32_t temp;

    if (s == 0) {
        temp = (uint32_t)a << 1;
    } else {
        temp = (int32_t)(int16_t)a >> (s - 1);
    }

    return (temp + 1) >> 1;
}

static inline uint32_t mipsdsp_rnd32_rashift(uint32_t a, uint8_t s)
{
    int64_t temp;

    if (s == 0) {
        temp = (uint64_t)a << 1;
    } else {
        temp = (int64_t)(int32_t)a >> (s - 1);
    }
    temp += 1;

    return (temp >> 1) & 0xFFFFFFFFull;
}

static inline uint16_t mipsdsp_sub_i16(int16_t a, int16_t b, CPUMIPSState *env)
{
    int16_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x8000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline uint16_t mipsdsp_sat16_sub(int16_t a, int16_t b,
                                         CPUMIPSState *env)
{
    int16_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x8000)) {
        if (a > 0) {
            temp = 0x7FFF;
        } else {
            temp = 0x8000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline uint32_t mipsdsp_sat32_sub(int32_t a, int32_t b,
                                         CPUMIPSState *env)
{
    int32_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x80000000)) {
        if (a > 0) {
            temp = 0x7FFFFFFF;
        } else {
            temp = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0xFFFFFFFFull;
}

static inline uint16_t mipsdsp_rshift1_sub_q16(int16_t a, int16_t b)
{
    int32_t  temp;

    temp = (int32_t)a - (int32_t)b;

    return (temp >> 1) & 0x0000FFFF;
}

static inline uint16_t mipsdsp_rrshift1_sub_q16(int16_t a, int16_t b)
{
    int32_t  temp;

    temp = (int32_t)a - (int32_t)b;
    temp += 1;

    return (temp >> 1) & 0x0000FFFF;
}

static inline uint32_t mipsdsp_rshift1_sub_q32(int32_t a, int32_t b)
{
    int64_t  temp;

    temp = (int64_t)a - (int64_t)b;

    return (temp >> 1) & 0xFFFFFFFFull;
}

static inline uint32_t mipsdsp_rrshift1_sub_q32(int32_t a, int32_t b)
{
    int64_t  temp;

    temp = (int64_t)a - (int64_t)b;
    temp += 1;

    return (temp >> 1) & 0xFFFFFFFFull;
}

static inline uint16_t mipsdsp_sub_u16_u16(uint16_t a, uint16_t b,
                                           CPUMIPSState *env)
{
    uint8_t  temp16;
    uint32_t temp;

    temp = (uint32_t)a - (uint32_t)b;
    temp16 = (temp >> 16) & 0x01;
    if (temp16 == 1) {
        set_DSPControl_overflow_flag(1, 20, env);
    }
    return temp & 0x0000FFFF;
}

static inline uint16_t mipsdsp_satu16_sub_u16_u16(uint16_t a, uint16_t b,
                                                  CPUMIPSState *env)
{
    uint8_t  temp16;
    uint32_t temp;

    temp   = (uint32_t)a - (uint32_t)b;
    temp16 = (temp >> 16) & 0x01;

    if (temp16 == 1) {
        temp = 0x0000;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0x0000FFFF;
}

static inline uint8_t mipsdsp_sub_u8(uint8_t a, uint8_t b, CPUMIPSState *env)
{
    uint8_t  temp8;
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b;
    temp8 = (temp >> 8) & 0x01;
    if (temp8 == 1) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0x00FF;
}

static inline uint8_t mipsdsp_satu8_sub(uint8_t a, uint8_t b, CPUMIPSState *env)
{
    uint8_t  temp8;
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b;
    temp8 = (temp >> 8) & 0x01;
    if (temp8 == 1) {
        temp = 0x00;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0x00FF;
}

static inline uint32_t mipsdsp_sub32(int32_t a, int32_t b, CPUMIPSState *env)
{
    int32_t temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x80000000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline int32_t mipsdsp_add_i32(int32_t a, int32_t b, CPUMIPSState *env)
{
    int32_t temp;

    temp = a + b;

    if (MIPSDSP_OVERFLOW(a, b, temp, 0x80000000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline int32_t mipsdsp_cmp_eq(int32_t a, int32_t b)
{
    return a == b;
}

static inline int32_t mipsdsp_cmp_le(int32_t a, int32_t b)
{
    return a <= b;
}

static inline int32_t mipsdsp_cmp_lt(int32_t a, int32_t b)
{
    return a < b;
}

static inline int32_t mipsdsp_cmpu_eq(uint32_t a, uint32_t b)
{
    return a == b;
}

static inline int32_t mipsdsp_cmpu_le(uint32_t a, uint32_t b)
{
    return a <= b;
}

static inline int32_t mipsdsp_cmpu_lt(uint32_t a, uint32_t b)
{
    return a < b;
}
/*** MIPS DSP internal functions end ***/
