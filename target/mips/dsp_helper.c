/*
 * MIPS ASE DSP Instruction emulation helpers for QEMU.
 *
 * Copyright (c) 2012  Jia Liu <proljc@gmail.com>
 *                     Dongxue Zhang <elta.era@gmail.com>
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/bitops.h"

/*
 * As the byte ordering doesn't matter, i.e. all columns are treated
 * identically, these unions can be used directly.
 */
typedef union {
    uint8_t  ub[4];
    int8_t   sb[4];
    uint16_t uh[2];
    int16_t  sh[2];
    uint32_t uw[1];
    int32_t  sw[1];
} DSP32Value;

typedef union {
    uint8_t  ub[8];
    int8_t   sb[8];
    uint16_t uh[4];
    int16_t  sh[4];
    uint32_t uw[2];
    int32_t  sw[2];
    uint64_t ul[1];
    int64_t  sl[1];
} DSP64Value;

/*** MIPS DSP internal functions begin ***/
#define MIPSDSP_ABS(x) (((x) >= 0) ? (x) : -(x))
#define MIPSDSP_OVERFLOW_ADD(a, b, c, d) (~((a) ^ (b)) & ((a) ^ (c)) & (d))
#define MIPSDSP_OVERFLOW_SUB(a, b, c, d) (((a) ^ (b)) & ((a) ^ (c)) & (d))

static inline void set_DSPControl_overflow_flag(uint32_t flag, int position,
                                                CPUMIPSState *env)
{
    env->active_tc.DSPControl |= (target_ulong)flag << position;
}

static inline void set_DSPControl_carryflag(bool flag, CPUMIPSState *env)
{
    env->active_tc.DSPControl &= ~(1 << 13);
    env->active_tc.DSPControl |= flag << 13;
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

static inline void set_DSPControl_pos(uint32_t pos, CPUMIPSState *env)
{
    target_ulong dspc;

    dspc = env->active_tc.DSPControl;
#ifndef TARGET_MIPS64
    dspc = dspc & 0xFFFFFFC0;
    dspc |= (pos & 0x3F);
#else
    dspc = dspc & 0xFFFFFF80;
    dspc |= (pos & 0x7F);
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

    if (MIPSDSP_OVERFLOW_ADD(a, b, tempI, 0x8000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempI;
}

static inline int16_t mipsdsp_sat_add_i16(int16_t a, int16_t b,
                                          CPUMIPSState *env)
{
    int16_t tempS;

    tempS = a + b;

    if (MIPSDSP_OVERFLOW_ADD(a, b, tempS, 0x8000)) {
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

    if (MIPSDSP_OVERFLOW_ADD(a, b, tempI, 0x80000000)) {
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

#ifdef TARGET_MIPS64
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
#endif

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

#ifdef TARGET_MIPS64
static inline int32_t mipsdsp_mul_i32_i32(int32_t a, int32_t b)
{
    return a * b;
}
#endif

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
        temp = ((int16_t)a * (int16_t)b) << 1;
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

#ifdef TARGET_MIPS64
static inline int32_t mipsdsp_rashift32(int32_t a, target_ulong mov)
{
    return a >> mov;
}
#endif

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

#ifdef TARGET_MIPS64
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
#endif

/*  128 bits long. p[0] is LO, p[1] is HI. */
static inline void mipsdsp_rndrashift_short_acc(int64_t *p,
                                                int32_t ac,
                                                int32_t shift,
                                                CPUMIPSState *env)
{
    int64_t acc;

    acc = ((int64_t)env->active_tc.HI[ac] << 32) |
          ((int64_t)env->active_tc.LO[ac] & 0xFFFFFFFF);
    p[0] = (shift == 0) ? (acc << 1) : (acc >> (shift - 1));
    p[1] = (acc >> 63) & 0x01;
}

#ifdef TARGET_MIPS64
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
#endif

static inline int32_t mipsdsp_mul_q15_q15(int32_t ac, uint16_t a, uint16_t b,
                                          CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    } else {
        temp = ((int16_t)a * (int16_t)b) << 1;
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
        temp = ((int64_t)(int32_t)a * (int32_t)b) << 1;
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

#ifdef TARGET_MIPS64
static inline uint64_t mipsdsp_mul_u32_u32(uint32_t a, uint32_t b)
{
    return (uint64_t)a * (uint64_t)b;
}
#endif

static inline int16_t mipsdsp_rndq15_mul_q15_q15(uint16_t a, uint16_t b,
                                                 CPUMIPSState *env)
{
    uint32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFF0000;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = ((int16_t)a * (int16_t)b) << 1;
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
        temp = (int16_t)a * (int16_t)b;
        temp = temp << 1;
    }

    return (temp >> 16) & 0x0000FFFF;
}

static inline uint16_t mipsdsp_trunc16_sat16_round(int32_t a,
                                                   CPUMIPSState *env)
{
    uint16_t temp;


    /*
     * The value 0x00008000 will be added to the input Q31 value, and the code
     * needs to check if the addition causes an overflow. Since a positive value
     * is added, overflow can happen in one direction only.
     */
    if (a > 0x7FFF7FFF) {
        temp = 0x7FFF;
        set_DSPControl_overflow_flag(1, 22, env);
    } else {
        temp = ((a + 0x8000) >> 16) & 0xFFFF;
    }

    return temp;
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
    uint8_t discard;

    if (s != 0) {
        discard = a >> (8 - s);

        if (discard != 0x00) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
    }
    return a << s;
}

static inline uint16_t mipsdsp_lshift16(uint16_t a, uint8_t s,
                                        CPUMIPSState *env)
{
    uint16_t discard;

    if (s != 0) {
        discard = (int16_t)a >> (15 - s);

        if ((discard != 0x0000) && (discard != 0xFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
    }
    return a << s;
}

#ifdef TARGET_MIPS64
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
#endif

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
    if (MIPSDSP_OVERFLOW_SUB(a, b, temp, 0x8000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline uint16_t mipsdsp_sat16_sub(int16_t a, int16_t b,
                                         CPUMIPSState *env)
{
    int16_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW_SUB(a, b, temp, 0x8000)) {
        if (a >= 0) {
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
    if (MIPSDSP_OVERFLOW_SUB(a, b, temp, 0x80000000)) {
        if (a >= 0) {
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

#ifdef TARGET_MIPS64
static inline uint32_t mipsdsp_sub32(int32_t a, int32_t b, CPUMIPSState *env)
{
    int32_t temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW_SUB(a, b, temp, 0x80000000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline int32_t mipsdsp_add_i32(int32_t a, int32_t b, CPUMIPSState *env)
{
    int32_t temp;

    temp = a + b;

    if (MIPSDSP_OVERFLOW_ADD(a, b, temp, 0x80000000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}
#endif

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

#define MIPSDSP_LHI 0xFFFFFFFF00000000ull
#define MIPSDSP_LLO 0x00000000FFFFFFFFull
#define MIPSDSP_HI  0xFFFF0000
#define MIPSDSP_LO  0x0000FFFF
#define MIPSDSP_Q3  0xFF000000
#define MIPSDSP_Q2  0x00FF0000
#define MIPSDSP_Q1  0x0000FF00
#define MIPSDSP_Q0  0x000000FF

#define MIPSDSP_SPLIT32_8(num, a, b, c, d)  \
    do {                                    \
        a = ((num) >> 24) & MIPSDSP_Q0;     \
        b = ((num) >> 16) & MIPSDSP_Q0;     \
        c = ((num) >> 8) & MIPSDSP_Q0;      \
        d = (num) & MIPSDSP_Q0;             \
    } while (0)

#define MIPSDSP_SPLIT32_16(num, a, b)       \
    do {                                    \
        a = ((num) >> 16) & MIPSDSP_LO;     \
        b = (num) & MIPSDSP_LO;             \
    } while (0)

#define MIPSDSP_RETURN32_8(a, b, c, d)  ((target_long)(int32_t)         \
                                         (((uint32_t)(a) << 24) |       \
                                          ((uint32_t)(b) << 16) |       \
                                          ((uint32_t)(c) << 8) |        \
                                          ((uint32_t)(d) & 0xFF)))
#define MIPSDSP_RETURN32_16(a, b)       ((target_long)(int32_t)         \
                                         (((uint32_t)(a) << 16) |       \
                                          ((uint32_t)(b) & 0xFFFF)))

#ifdef TARGET_MIPS64
#define MIPSDSP_SPLIT64_16(num, a, b, c, d)  \
    do {                                     \
        a = ((num) >> 48) & MIPSDSP_LO;      \
        b = ((num) >> 32) & MIPSDSP_LO;      \
        c = ((num) >> 16) & MIPSDSP_LO;      \
        d = (num) & MIPSDSP_LO;              \
    } while (0)

#define MIPSDSP_SPLIT64_32(num, a, b)       \
    do {                                    \
        a = ((num) >> 32) & MIPSDSP_LLO;    \
        b = (num) & MIPSDSP_LLO;            \
    } while (0)

#define MIPSDSP_RETURN64_16(a, b, c, d) (((uint64_t)(a) << 48) |        \
                                         ((uint64_t)(b) << 32) |        \
                                         ((uint64_t)(c) << 16) |        \
                                         (uint64_t)(d))
#define MIPSDSP_RETURN64_32(a, b)       (((uint64_t)(a) << 32) | (uint64_t)(b))
#endif

/** DSP Arithmetic Sub-class insns **/
#define MIPSDSP32_UNOP_ENV(name, func, element)                            \
target_ulong helper_##name(target_ulong rt, CPUMIPSState *env)             \
{                                                                          \
    DSP32Value dt;                                                         \
    unsigned int i;                                                     \
                                                                           \
    dt.sw[0] = rt;                                                         \
                                                                           \
    for (i = 0; i < ARRAY_SIZE(dt.element); i++) {                         \
        dt.element[i] = mipsdsp_##func(dt.element[i], env);                \
    }                                                                      \
                                                                           \
    return (target_long)dt.sw[0];                                          \
}
MIPSDSP32_UNOP_ENV(absq_s_ph, sat_abs16, sh)
MIPSDSP32_UNOP_ENV(absq_s_qb, sat_abs8, sb)
MIPSDSP32_UNOP_ENV(absq_s_w, sat_abs32, sw)
#undef MIPSDSP32_UNOP_ENV

#if defined(TARGET_MIPS64)
#define MIPSDSP64_UNOP_ENV(name, func, element)                            \
target_ulong helper_##name(target_ulong rt, CPUMIPSState *env)             \
{                                                                          \
    DSP64Value dt;                                                         \
    unsigned int i;                                                        \
                                                                           \
    dt.sl[0] = rt;                                                         \
                                                                           \
    for (i = 0; i < ARRAY_SIZE(dt.element); i++) {                         \
        dt.element[i] = mipsdsp_##func(dt.element[i], env);                \
    }                                                                      \
                                                                           \
    return dt.sl[0];                                                       \
}
MIPSDSP64_UNOP_ENV(absq_s_ob, sat_abs8, sb)
MIPSDSP64_UNOP_ENV(absq_s_qh, sat_abs16, sh)
MIPSDSP64_UNOP_ENV(absq_s_pw, sat_abs32, sw)
#undef MIPSDSP64_UNOP_ENV
#endif

#define MIPSDSP32_BINOP(name, func, element)                               \
target_ulong helper_##name(target_ulong rs, target_ulong rt)               \
{                                                                          \
    DSP32Value ds, dt;                                                     \
    unsigned int i;                                                        \
                                                                           \
    ds.sw[0] = rs;                                                         \
    dt.sw[0] = rt;                                                         \
                                                                           \
    for (i = 0; i < ARRAY_SIZE(ds.element); i++) {                         \
        ds.element[i] = mipsdsp_##func(ds.element[i], dt.element[i]);      \
    }                                                                      \
                                                                           \
    return (target_long)ds.sw[0];                                          \
}
MIPSDSP32_BINOP(addqh_ph, rshift1_add_q16, sh);
MIPSDSP32_BINOP(addqh_r_ph, rrshift1_add_q16, sh);
MIPSDSP32_BINOP(addqh_r_w, rrshift1_add_q32, sw);
MIPSDSP32_BINOP(addqh_w, rshift1_add_q32, sw);
MIPSDSP32_BINOP(adduh_qb, rshift1_add_u8, ub);
MIPSDSP32_BINOP(adduh_r_qb, rrshift1_add_u8, ub);
MIPSDSP32_BINOP(subqh_ph, rshift1_sub_q16, sh);
MIPSDSP32_BINOP(subqh_r_ph, rrshift1_sub_q16, sh);
MIPSDSP32_BINOP(subqh_r_w, rrshift1_sub_q32, sw);
MIPSDSP32_BINOP(subqh_w, rshift1_sub_q32, sw);
#undef MIPSDSP32_BINOP

#define MIPSDSP32_BINOP_ENV(name, func, element)                           \
target_ulong helper_##name(target_ulong rs, target_ulong rt,               \
                           CPUMIPSState *env)                              \
{                                                                          \
    DSP32Value ds, dt;                                                     \
    unsigned int i;                                                        \
                                                                           \
    ds.sw[0] = rs;                                                         \
    dt.sw[0] = rt;                                                         \
                                                                           \
    for (i = 0 ; i < ARRAY_SIZE(ds.element); i++) {                        \
        ds.element[i] = mipsdsp_##func(ds.element[i], dt.element[i], env); \
    }                                                                      \
                                                                           \
    return (target_long)ds.sw[0];                                          \
}
MIPSDSP32_BINOP_ENV(addq_ph, add_i16, sh)
MIPSDSP32_BINOP_ENV(addq_s_ph, sat_add_i16, sh)
MIPSDSP32_BINOP_ENV(addq_s_w, sat_add_i32, sw);
MIPSDSP32_BINOP_ENV(addu_ph, add_u16, sh)
MIPSDSP32_BINOP_ENV(addu_qb, add_u8, ub);
MIPSDSP32_BINOP_ENV(addu_s_ph, sat_add_u16, sh)
MIPSDSP32_BINOP_ENV(addu_s_qb, sat_add_u8, ub);
MIPSDSP32_BINOP_ENV(subq_ph, sub_i16, sh);
MIPSDSP32_BINOP_ENV(subq_s_ph, sat16_sub, sh);
MIPSDSP32_BINOP_ENV(subq_s_w, sat32_sub, sw);
MIPSDSP32_BINOP_ENV(subu_ph, sub_u16_u16, sh);
MIPSDSP32_BINOP_ENV(subu_qb, sub_u8, ub);
MIPSDSP32_BINOP_ENV(subu_s_ph, satu16_sub_u16_u16, sh);
MIPSDSP32_BINOP_ENV(subu_s_qb, satu8_sub, ub);
#undef MIPSDSP32_BINOP_ENV

#ifdef TARGET_MIPS64
#define MIPSDSP64_BINOP(name, func, element)                               \
target_ulong helper_##name(target_ulong rs, target_ulong rt)               \
{                                                                          \
    DSP64Value ds, dt;                                                     \
    unsigned int i;                                                        \
                                                                           \
    ds.sl[0] = rs;                                                         \
    dt.sl[0] = rt;                                                         \
                                                                           \
    for (i = 0 ; i < ARRAY_SIZE(ds.element); i++) {                        \
        ds.element[i] = mipsdsp_##func(ds.element[i], dt.element[i]);      \
    }                                                                      \
                                                                           \
    return ds.sl[0];                                                       \
}
MIPSDSP64_BINOP(adduh_ob, rshift1_add_u8, ub);
MIPSDSP64_BINOP(adduh_r_ob, rrshift1_add_u8, ub);
MIPSDSP64_BINOP(subuh_ob, rshift1_sub_u8, ub);
MIPSDSP64_BINOP(subuh_r_ob, rrshift1_sub_u8, ub);
#undef MIPSDSP64_BINOP

#define MIPSDSP64_BINOP_ENV(name, func, element)                           \
target_ulong helper_##name(target_ulong rs, target_ulong rt,               \
                           CPUMIPSState *env)                              \
{                                                                          \
    DSP64Value ds, dt;                                                     \
    unsigned int i;                                                        \
                                                                           \
    ds.sl[0] = rs;                                                         \
    dt.sl[0] = rt;                                                         \
                                                                           \
    for (i = 0 ; i < ARRAY_SIZE(ds.element); i++) {                        \
        ds.element[i] = mipsdsp_##func(ds.element[i], dt.element[i], env); \
    }                                                                      \
                                                                           \
    return ds.sl[0];                                                       \
}
MIPSDSP64_BINOP_ENV(addq_pw, add_i32, sw);
MIPSDSP64_BINOP_ENV(addq_qh, add_i16, sh);
MIPSDSP64_BINOP_ENV(addq_s_pw, sat_add_i32, sw);
MIPSDSP64_BINOP_ENV(addq_s_qh, sat_add_i16, sh);
MIPSDSP64_BINOP_ENV(addu_ob, add_u8, uh);
MIPSDSP64_BINOP_ENV(addu_qh, add_u16, uh);
MIPSDSP64_BINOP_ENV(addu_s_ob, sat_add_u8, uh);
MIPSDSP64_BINOP_ENV(addu_s_qh, sat_add_u16, uh);
MIPSDSP64_BINOP_ENV(subq_pw, sub32, sw);
MIPSDSP64_BINOP_ENV(subq_qh, sub_i16, sh);
MIPSDSP64_BINOP_ENV(subq_s_pw, sat32_sub, sw);
MIPSDSP64_BINOP_ENV(subq_s_qh, sat16_sub, sh);
MIPSDSP64_BINOP_ENV(subu_ob, sub_u8, uh);
MIPSDSP64_BINOP_ENV(subu_qh, sub_u16_u16, uh);
MIPSDSP64_BINOP_ENV(subu_s_ob, satu8_sub, uh);
MIPSDSP64_BINOP_ENV(subu_s_qh, satu16_sub_u16_u16, uh);
#undef MIPSDSP64_BINOP_ENV

#endif

#define SUBUH_QB(name, var) \
target_ulong helper_##name##_qb(target_ulong rs, target_ulong rt) \
{                                                                 \
    uint8_t rs3, rs2, rs1, rs0;                                   \
    uint8_t rt3, rt2, rt1, rt0;                                   \
    uint8_t tempD, tempC, tempB, tempA;                           \
                                                                  \
    MIPSDSP_SPLIT32_8(rs, rs3, rs2, rs1, rs0);                    \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                    \
                                                                  \
    tempD = ((uint16_t)rs3 - (uint16_t)rt3 + var) >> 1;           \
    tempC = ((uint16_t)rs2 - (uint16_t)rt2 + var) >> 1;           \
    tempB = ((uint16_t)rs1 - (uint16_t)rt1 + var) >> 1;           \
    tempA = ((uint16_t)rs0 - (uint16_t)rt0 + var) >> 1;           \
                                                                  \
    return ((uint32_t)tempD << 24) | ((uint32_t)tempC << 16) |    \
        ((uint32_t)tempB << 8) | ((uint32_t)tempA);               \
}

SUBUH_QB(subuh, 0);
SUBUH_QB(subuh_r, 1);

#undef SUBUH_QB

target_ulong helper_addsc(target_ulong rs, target_ulong rt, CPUMIPSState *env)
{
    uint64_t temp, tempRs, tempRt;
    bool flag;

    tempRs = (uint64_t)rs & MIPSDSP_LLO;
    tempRt = (uint64_t)rt & MIPSDSP_LLO;

    temp = tempRs + tempRt;
    flag = (temp & 0x0100000000ull) >> 32;
    set_DSPControl_carryflag(flag, env);

    return (target_long)(int32_t)(temp & MIPSDSP_LLO);
}

target_ulong helper_addwc(target_ulong rs, target_ulong rt, CPUMIPSState *env)
{
    uint32_t rd;
    int32_t temp32, temp31;
    int64_t tempL;

    tempL = (int64_t)(int32_t)rs + (int64_t)(int32_t)rt +
        get_DSPControl_carryflag(env);
    temp31 = (tempL >> 31) & 0x01;
    temp32 = (tempL >> 32) & 0x01;

    if (temp31 != temp32) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    rd = tempL & MIPSDSP_LLO;

    return (target_long)(int32_t)rd;
}

target_ulong helper_modsub(target_ulong rs, target_ulong rt)
{
    int32_t decr;
    uint16_t lastindex;
    target_ulong rd;

    decr = rt & MIPSDSP_Q0;
    lastindex = (rt >> 8) & MIPSDSP_LO;

    if ((rs & MIPSDSP_LLO) == 0x00000000) {
        rd = (target_ulong)lastindex;
    } else {
        rd = rs - decr;
    }

    return rd;
}

target_ulong helper_raddu_w_qb(target_ulong rs)
{
    target_ulong ret = 0;
    DSP32Value ds;
    unsigned int i;

    ds.uw[0] = rs;
    for (i = 0; i < 4; i++) {
        ret += ds.ub[i];
    }
    return ret;
}

#if defined(TARGET_MIPS64)
target_ulong helper_raddu_l_ob(target_ulong rs)
{
    target_ulong ret = 0;
    DSP64Value ds;
    unsigned int i;

    ds.ul[0] = rs;
    for (i = 0; i < 8; i++) {
        ret += ds.ub[i];
    }
    return ret;
}
#endif

#define PRECR_QB_PH(name, a, b)\
target_ulong helper_##name##_qb_ph(target_ulong rs, target_ulong rt) \
{                                                                    \
    uint8_t tempD, tempC, tempB, tempA;                              \
                                                                     \
    tempD = (rs >> a) & MIPSDSP_Q0;                                  \
    tempC = (rs >> b) & MIPSDSP_Q0;                                  \
    tempB = (rt >> a) & MIPSDSP_Q0;                                  \
    tempA = (rt >> b) & MIPSDSP_Q0;                                  \
                                                                     \
    return MIPSDSP_RETURN32_8(tempD, tempC, tempB, tempA);           \
}

PRECR_QB_PH(precr, 16, 0);
PRECR_QB_PH(precrq, 24, 8);

#undef PRECR_QB_OH

target_ulong helper_precr_sra_ph_w(uint32_t sa, target_ulong rs,
                                   target_ulong rt)
{
    uint16_t tempB, tempA;

    tempB = ((int32_t)rt >> sa) & MIPSDSP_LO;
    tempA = ((int32_t)rs >> sa) & MIPSDSP_LO;

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

target_ulong helper_precr_sra_r_ph_w(uint32_t sa,
                                     target_ulong rs, target_ulong rt)
{
    uint64_t tempB, tempA;

    /* If sa = 0, then (sa - 1) = -1 will case shift error, so we need else. */
    if (sa == 0) {
        tempB = (rt & MIPSDSP_LO) << 1;
        tempA = (rs & MIPSDSP_LO) << 1;
    } else {
        tempB = ((int32_t)rt >> (sa - 1)) + 1;
        tempA = ((int32_t)rs >> (sa - 1)) + 1;
    }
    rt = (((tempB >> 1) & MIPSDSP_LO) << 16) | ((tempA >> 1) & MIPSDSP_LO);

    return (target_long)(int32_t)rt;
}

target_ulong helper_precrq_ph_w(target_ulong rs, target_ulong rt)
{
    uint16_t tempB, tempA;

    tempB = (rs & MIPSDSP_HI) >> 16;
    tempA = (rt & MIPSDSP_HI) >> 16;

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

target_ulong helper_precrq_rs_ph_w(target_ulong rs, target_ulong rt,
                                   CPUMIPSState *env)
{
    uint16_t tempB, tempA;

    tempB = mipsdsp_trunc16_sat16_round(rs, env);
    tempA = mipsdsp_trunc16_sat16_round(rt, env);

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

#if defined(TARGET_MIPS64)
target_ulong helper_precr_ob_qh(target_ulong rs, target_ulong rt)
{
    uint8_t rs6, rs4, rs2, rs0;
    uint8_t rt6, rt4, rt2, rt0;
    uint64_t temp;

    rs6 = (rs >> 48) & MIPSDSP_Q0;
    rs4 = (rs >> 32) & MIPSDSP_Q0;
    rs2 = (rs >> 16) & MIPSDSP_Q0;
    rs0 = rs & MIPSDSP_Q0;
    rt6 = (rt >> 48) & MIPSDSP_Q0;
    rt4 = (rt >> 32) & MIPSDSP_Q0;
    rt2 = (rt >> 16) & MIPSDSP_Q0;
    rt0 = rt & MIPSDSP_Q0;

    temp = ((uint64_t)rs6 << 56) | ((uint64_t)rs4 << 48) |
           ((uint64_t)rs2 << 40) | ((uint64_t)rs0 << 32) |
           ((uint64_t)rt6 << 24) | ((uint64_t)rt4 << 16) |
           ((uint64_t)rt2 << 8) | (uint64_t)rt0;

    return temp;
}


/*
 * In case sa == 0, use rt2, rt0, rs2, rs0.
 * In case sa != 0, use rt3, rt1, rs3, rs1.
 */
#define PRECR_QH_PW(name, var)                                        \
target_ulong helper_precr_##name##_qh_pw(target_ulong rs,             \
                                         target_ulong rt,             \
                                         uint32_t sa)                 \
{                                                                     \
    uint16_t rs3, rs2, rs1, rs0;                                      \
    uint16_t rt3, rt2, rt1, rt0;                                      \
    uint16_t tempD, tempC, tempB, tempA;                              \
                                                                      \
    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);                       \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                       \
                                                                      \
    if (sa == 0) {                                                    \
        tempD = rt2 << var;                                           \
        tempC = rt0 << var;                                           \
        tempB = rs2 << var;                                           \
        tempA = rs0 << var;                                           \
    } else {                                                          \
        tempD = (((int16_t)rt3 >> sa) + var) >> var;                  \
        tempC = (((int16_t)rt1 >> sa) + var) >> var;                  \
        tempB = (((int16_t)rs3 >> sa) + var) >> var;                  \
        tempA = (((int16_t)rs1 >> sa) + var) >> var;                  \
    }                                                                 \
                                                                      \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);           \
}

PRECR_QH_PW(sra, 0);
PRECR_QH_PW(sra_r, 1);

#undef PRECR_QH_PW

target_ulong helper_precrq_ob_qh(target_ulong rs, target_ulong rt)
{
    uint8_t rs6, rs4, rs2, rs0;
    uint8_t rt6, rt4, rt2, rt0;
    uint64_t temp;

    rs6 = (rs >> 56) & MIPSDSP_Q0;
    rs4 = (rs >> 40) & MIPSDSP_Q0;
    rs2 = (rs >> 24) & MIPSDSP_Q0;
    rs0 = (rs >> 8) & MIPSDSP_Q0;
    rt6 = (rt >> 56) & MIPSDSP_Q0;
    rt4 = (rt >> 40) & MIPSDSP_Q0;
    rt2 = (rt >> 24) & MIPSDSP_Q0;
    rt0 = (rt >> 8) & MIPSDSP_Q0;

    temp = ((uint64_t)rs6 << 56) | ((uint64_t)rs4 << 48) |
           ((uint64_t)rs2 << 40) | ((uint64_t)rs0 << 32) |
           ((uint64_t)rt6 << 24) | ((uint64_t)rt4 << 16) |
           ((uint64_t)rt2 << 8) | (uint64_t)rt0;

    return temp;
}

target_ulong helper_precrq_qh_pw(target_ulong rs, target_ulong rt)
{
    uint16_t tempD, tempC, tempB, tempA;

    tempD = (rs >> 48) & MIPSDSP_LO;
    tempC = (rs >> 16) & MIPSDSP_LO;
    tempB = (rt >> 48) & MIPSDSP_LO;
    tempA = (rt >> 16) & MIPSDSP_LO;

    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);
}

target_ulong helper_precrq_rs_qh_pw(target_ulong rs, target_ulong rt,
                                    CPUMIPSState *env)
{
    uint32_t rs2, rs0;
    uint32_t rt2, rt0;
    uint16_t tempD, tempC, tempB, tempA;

    rs2 = (rs >> 32) & MIPSDSP_LLO;
    rs0 = rs & MIPSDSP_LLO;
    rt2 = (rt >> 32) & MIPSDSP_LLO;
    rt0 = rt & MIPSDSP_LLO;

    tempD = mipsdsp_trunc16_sat16_round(rs2, env);
    tempC = mipsdsp_trunc16_sat16_round(rs0, env);
    tempB = mipsdsp_trunc16_sat16_round(rt2, env);
    tempA = mipsdsp_trunc16_sat16_round(rt0, env);

    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);
}

target_ulong helper_precrq_pw_l(target_ulong rs, target_ulong rt)
{
    uint32_t tempB, tempA;

    tempB = (rs >> 32) & MIPSDSP_LLO;
    tempA = (rt >> 32) & MIPSDSP_LLO;

    return MIPSDSP_RETURN64_32(tempB, tempA);
}
#endif

target_ulong helper_precrqu_s_qb_ph(target_ulong rs, target_ulong rt,
                                    CPUMIPSState *env)
{
    uint8_t  tempD, tempC, tempB, tempA;
    uint16_t rsh, rsl, rth, rtl;

    rsh = (rs & MIPSDSP_HI) >> 16;
    rsl =  rs & MIPSDSP_LO;
    rth = (rt & MIPSDSP_HI) >> 16;
    rtl =  rt & MIPSDSP_LO;

    tempD = mipsdsp_sat8_reduce_precision(rsh, env);
    tempC = mipsdsp_sat8_reduce_precision(rsl, env);
    tempB = mipsdsp_sat8_reduce_precision(rth, env);
    tempA = mipsdsp_sat8_reduce_precision(rtl, env);

    return MIPSDSP_RETURN32_8(tempD, tempC, tempB, tempA);
}

#if defined(TARGET_MIPS64)
target_ulong helper_precrqu_s_ob_qh(target_ulong rs, target_ulong rt,
                                    CPUMIPSState *env)
{
    int i;
    uint16_t rs3, rs2, rs1, rs0;
    uint16_t rt3, rt2, rt1, rt0;
    uint8_t temp[8];
    uint64_t result;

    result = 0;

    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);

    temp[7] = mipsdsp_sat8_reduce_precision(rs3, env);
    temp[6] = mipsdsp_sat8_reduce_precision(rs2, env);
    temp[5] = mipsdsp_sat8_reduce_precision(rs1, env);
    temp[4] = mipsdsp_sat8_reduce_precision(rs0, env);
    temp[3] = mipsdsp_sat8_reduce_precision(rt3, env);
    temp[2] = mipsdsp_sat8_reduce_precision(rt2, env);
    temp[1] = mipsdsp_sat8_reduce_precision(rt1, env);
    temp[0] = mipsdsp_sat8_reduce_precision(rt0, env);

    for (i = 0; i < 8; i++) {
        result |= (uint64_t)temp[i] << (8 * i);
    }

    return result;
}

#define PRECEQ_PW(name, a, b) \
target_ulong helper_preceq_pw_##name(target_ulong rt) \
{                                                       \
    uint16_t tempB, tempA;                              \
    uint32_t tempBI, tempAI;                            \
                                                        \
    tempB = (rt >> a) & MIPSDSP_LO;                     \
    tempA = (rt >> b) & MIPSDSP_LO;                     \
                                                        \
    tempBI = (uint32_t)tempB << 16;                     \
    tempAI = (uint32_t)tempA << 16;                     \
                                                        \
    return MIPSDSP_RETURN64_32(tempBI, tempAI);         \
}

PRECEQ_PW(qhl, 48, 32);
PRECEQ_PW(qhr, 16, 0);
PRECEQ_PW(qhla, 48, 16);
PRECEQ_PW(qhra, 32, 0);

#undef PRECEQ_PW

#endif

#define PRECEQU_PH(name, a, b) \
target_ulong helper_precequ_ph_##name(target_ulong rt) \
{                                                        \
    uint16_t tempB, tempA;                               \
                                                         \
    tempB = (rt >> a) & MIPSDSP_Q0;                      \
    tempA = (rt >> b) & MIPSDSP_Q0;                      \
                                                         \
    tempB = tempB << 7;                                  \
    tempA = tempA << 7;                                  \
                                                         \
    return MIPSDSP_RETURN32_16(tempB, tempA);            \
}

PRECEQU_PH(qbl, 24, 16);
PRECEQU_PH(qbr, 8, 0);
PRECEQU_PH(qbla, 24, 8);
PRECEQU_PH(qbra, 16, 0);

#undef PRECEQU_PH

#if defined(TARGET_MIPS64)
#define PRECEQU_QH(name, a, b, c, d) \
target_ulong helper_precequ_qh_##name(target_ulong rt)       \
{                                                            \
    uint16_t tempD, tempC, tempB, tempA;                     \
                                                             \
    tempD = (rt >> a) & MIPSDSP_Q0;                          \
    tempC = (rt >> b) & MIPSDSP_Q0;                          \
    tempB = (rt >> c) & MIPSDSP_Q0;                          \
    tempA = (rt >> d) & MIPSDSP_Q0;                          \
                                                             \
    tempD = tempD << 7;                                      \
    tempC = tempC << 7;                                      \
    tempB = tempB << 7;                                      \
    tempA = tempA << 7;                                      \
                                                             \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);  \
}

PRECEQU_QH(obl, 56, 48, 40, 32);
PRECEQU_QH(obr, 24, 16, 8, 0);
PRECEQU_QH(obla, 56, 40, 24, 8);
PRECEQU_QH(obra, 48, 32, 16, 0);

#undef PRECEQU_QH

#endif

#define PRECEU_PH(name, a, b) \
target_ulong helper_preceu_ph_##name(target_ulong rt) \
{                                                     \
    uint16_t tempB, tempA;                            \
                                                      \
    tempB = (rt >> a) & MIPSDSP_Q0;                   \
    tempA = (rt >> b) & MIPSDSP_Q0;                   \
                                                      \
    return MIPSDSP_RETURN32_16(tempB, tempA);         \
}

PRECEU_PH(qbl, 24, 16);
PRECEU_PH(qbr, 8, 0);
PRECEU_PH(qbla, 24, 8);
PRECEU_PH(qbra, 16, 0);

#undef PRECEU_PH

#if defined(TARGET_MIPS64)
#define PRECEU_QH(name, a, b, c, d) \
target_ulong helper_preceu_qh_##name(target_ulong rt)        \
{                                                            \
    uint16_t tempD, tempC, tempB, tempA;                     \
                                                             \
    tempD = (rt >> a) & MIPSDSP_Q0;                          \
    tempC = (rt >> b) & MIPSDSP_Q0;                          \
    tempB = (rt >> c) & MIPSDSP_Q0;                          \
    tempA = (rt >> d) & MIPSDSP_Q0;                          \
                                                             \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);  \
}

PRECEU_QH(obl, 56, 48, 40, 32);
PRECEU_QH(obr, 24, 16, 8, 0);
PRECEU_QH(obla, 56, 40, 24, 8);
PRECEU_QH(obra, 48, 32, 16, 0);

#undef PRECEU_QH

#endif

/** DSP GPR-Based Shift Sub-class insns **/
#define SHIFT_QB(name, func) \
target_ulong helper_##name##_qb(target_ulong sa, target_ulong rt) \
{                                                                    \
    uint8_t rt3, rt2, rt1, rt0;                                      \
                                                                     \
    sa = sa & 0x07;                                                  \
                                                                     \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                       \
                                                                     \
    rt3 = mipsdsp_##func(rt3, sa);                                   \
    rt2 = mipsdsp_##func(rt2, sa);                                   \
    rt1 = mipsdsp_##func(rt1, sa);                                   \
    rt0 = mipsdsp_##func(rt0, sa);                                   \
                                                                     \
    return MIPSDSP_RETURN32_8(rt3, rt2, rt1, rt0);                   \
}

#define SHIFT_QB_ENV(name, func) \
target_ulong helper_##name##_qb(target_ulong sa, target_ulong rt,\
                                CPUMIPSState *env) \
{                                                                    \
    uint8_t rt3, rt2, rt1, rt0;                                      \
                                                                     \
    sa = sa & 0x07;                                                  \
                                                                     \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                       \
                                                                     \
    rt3 = mipsdsp_##func(rt3, sa, env);                              \
    rt2 = mipsdsp_##func(rt2, sa, env);                              \
    rt1 = mipsdsp_##func(rt1, sa, env);                              \
    rt0 = mipsdsp_##func(rt0, sa, env);                              \
                                                                     \
    return MIPSDSP_RETURN32_8(rt3, rt2, rt1, rt0);                   \
}

SHIFT_QB_ENV(shll, lshift8);
SHIFT_QB(shrl, rshift_u8);

SHIFT_QB(shra, rashift8);
SHIFT_QB(shra_r, rnd8_rashift);

#undef SHIFT_QB
#undef SHIFT_QB_ENV

#if defined(TARGET_MIPS64)
#define SHIFT_OB(name, func) \
target_ulong helper_##name##_ob(target_ulong rt, target_ulong sa) \
{                                                                        \
    int i;                                                               \
    uint8_t rt_t[8];                                                     \
    uint64_t temp;                                                       \
                                                                         \
    sa = sa & 0x07;                                                      \
    temp = 0;                                                            \
                                                                         \
    for (i = 0; i < 8; i++) {                                            \
        rt_t[i] = (rt >> (8 * i)) & MIPSDSP_Q0;                          \
        rt_t[i] = mipsdsp_##func(rt_t[i], sa);                           \
        temp |= (uint64_t)rt_t[i] << (8 * i);                            \
    }                                                                    \
                                                                         \
    return temp;                                                         \
}

#define SHIFT_OB_ENV(name, func) \
target_ulong helper_##name##_ob(target_ulong rt, target_ulong sa, \
                                CPUMIPSState *env)                       \
{                                                                        \
    int i;                                                               \
    uint8_t rt_t[8];                                                     \
    uint64_t temp;                                                       \
                                                                         \
    sa = sa & 0x07;                                                      \
    temp = 0;                                                            \
                                                                         \
    for (i = 0; i < 8; i++) {                                            \
        rt_t[i] = (rt >> (8 * i)) & MIPSDSP_Q0;                          \
        rt_t[i] = mipsdsp_##func(rt_t[i], sa, env);                      \
        temp |= (uint64_t)rt_t[i] << (8 * i);                            \
    }                                                                    \
                                                                         \
    return temp;                                                         \
}

SHIFT_OB_ENV(shll, lshift8);
SHIFT_OB(shrl, rshift_u8);

SHIFT_OB(shra, rashift8);
SHIFT_OB(shra_r, rnd8_rashift);

#undef SHIFT_OB
#undef SHIFT_OB_ENV

#endif

#define SHIFT_PH(name, func) \
target_ulong helper_##name##_ph(target_ulong sa, target_ulong rt, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint16_t rth, rtl;                                            \
                                                                  \
    sa = sa & 0x0F;                                               \
                                                                  \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                             \
                                                                  \
    rth = mipsdsp_##func(rth, sa, env);                           \
    rtl = mipsdsp_##func(rtl, sa, env);                           \
                                                                  \
    return MIPSDSP_RETURN32_16(rth, rtl);                         \
}

SHIFT_PH(shll, lshift16);
SHIFT_PH(shll_s, sat16_lshift);

#undef SHIFT_PH

#if defined(TARGET_MIPS64)
#define SHIFT_QH(name, func) \
target_ulong helper_##name##_qh(target_ulong rt, target_ulong sa) \
{                                                                 \
    uint16_t rt3, rt2, rt1, rt0;                                  \
                                                                  \
    sa = sa & 0x0F;                                               \
                                                                  \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                   \
                                                                  \
    rt3 = mipsdsp_##func(rt3, sa);                                \
    rt2 = mipsdsp_##func(rt2, sa);                                \
    rt1 = mipsdsp_##func(rt1, sa);                                \
    rt0 = mipsdsp_##func(rt0, sa);                                \
                                                                  \
    return MIPSDSP_RETURN64_16(rt3, rt2, rt1, rt0);               \
}

#define SHIFT_QH_ENV(name, func) \
target_ulong helper_##name##_qh(target_ulong rt, target_ulong sa, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint16_t rt3, rt2, rt1, rt0;                                  \
                                                                  \
    sa = sa & 0x0F;                                               \
                                                                  \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                   \
                                                                  \
    rt3 = mipsdsp_##func(rt3, sa, env);                           \
    rt2 = mipsdsp_##func(rt2, sa, env);                           \
    rt1 = mipsdsp_##func(rt1, sa, env);                           \
    rt0 = mipsdsp_##func(rt0, sa, env);                           \
                                                                  \
    return MIPSDSP_RETURN64_16(rt3, rt2, rt1, rt0);               \
}

SHIFT_QH_ENV(shll, lshift16);
SHIFT_QH_ENV(shll_s, sat16_lshift);

SHIFT_QH(shrl, rshift_u16);
SHIFT_QH(shra, rashift16);
SHIFT_QH(shra_r, rnd16_rashift);

#undef SHIFT_QH
#undef SHIFT_QH_ENV

#endif

#define SHIFT_W(name, func) \
target_ulong helper_##name##_w(target_ulong sa, target_ulong rt) \
{                                                                       \
    uint32_t temp;                                                      \
                                                                        \
    sa = sa & 0x1F;                                                     \
    temp = mipsdsp_##func(rt, sa);                                      \
                                                                        \
    return (target_long)(int32_t)temp;                                  \
}

#define SHIFT_W_ENV(name, func) \
target_ulong helper_##name##_w(target_ulong sa, target_ulong rt, \
                               CPUMIPSState *env) \
{                                                                       \
    uint32_t temp;                                                      \
                                                                        \
    sa = sa & 0x1F;                                                     \
    temp = mipsdsp_##func(rt, sa, env);                                 \
                                                                        \
    return (target_long)(int32_t)temp;                                  \
}

SHIFT_W_ENV(shll_s, sat32_lshift);
SHIFT_W(shra_r, rnd32_rashift);

#undef SHIFT_W
#undef SHIFT_W_ENV

#if defined(TARGET_MIPS64)
#define SHIFT_PW(name, func) \
target_ulong helper_##name##_pw(target_ulong rt, target_ulong sa) \
{                                                                 \
    uint32_t rt1, rt0;                                            \
                                                                  \
    sa = sa & 0x1F;                                               \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                             \
                                                                  \
    rt1 = mipsdsp_##func(rt1, sa);                                \
    rt0 = mipsdsp_##func(rt0, sa);                                \
                                                                  \
    return MIPSDSP_RETURN64_32(rt1, rt0);                         \
}

#define SHIFT_PW_ENV(name, func) \
target_ulong helper_##name##_pw(target_ulong rt, target_ulong sa, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint32_t rt1, rt0;                                            \
                                                                  \
    sa = sa & 0x1F;                                               \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                             \
                                                                  \
    rt1 = mipsdsp_##func(rt1, sa, env);                           \
    rt0 = mipsdsp_##func(rt0, sa, env);                           \
                                                                  \
    return MIPSDSP_RETURN64_32(rt1, rt0);                         \
}

SHIFT_PW_ENV(shll, lshift32);
SHIFT_PW_ENV(shll_s, sat32_lshift);

SHIFT_PW(shra, rashift32);
SHIFT_PW(shra_r, rnd32_rashift);

#undef SHIFT_PW
#undef SHIFT_PW_ENV

#endif

#define SHIFT_PH(name, func) \
target_ulong helper_##name##_ph(target_ulong sa, target_ulong rt) \
{                                                                    \
    uint16_t rth, rtl;                                               \
                                                                     \
    sa = sa & 0x0F;                                                  \
                                                                     \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                                \
                                                                     \
    rth = mipsdsp_##func(rth, sa);                                   \
    rtl = mipsdsp_##func(rtl, sa);                                   \
                                                                     \
    return MIPSDSP_RETURN32_16(rth, rtl);                            \
}

SHIFT_PH(shrl, rshift_u16);
SHIFT_PH(shra, rashift16);
SHIFT_PH(shra_r, rnd16_rashift);

#undef SHIFT_PH

/** DSP Multiply Sub-class insns **/
/*
 * Return value made up by two 16bits value.
 * FIXME give the macro a better name.
 */
#define MUL_RETURN32_16_PH(name, func, \
                           rsmov1, rsmov2, rsfilter, \
                           rtmov1, rtmov2, rtfilter) \
target_ulong helper_##name(target_ulong rs, target_ulong rt, \
                           CPUMIPSState *env)                \
{                                                            \
    uint16_t rsB, rsA, rtB, rtA;                             \
                                                             \
    rsB = (rs >> rsmov1) & rsfilter;                         \
    rsA = (rs >> rsmov2) & rsfilter;                         \
    rtB = (rt >> rtmov1) & rtfilter;                         \
    rtA = (rt >> rtmov2) & rtfilter;                         \
                                                             \
    rsB = mipsdsp_##func(rsB, rtB, env);                     \
    rsA = mipsdsp_##func(rsA, rtA, env);                     \
                                                             \
    return MIPSDSP_RETURN32_16(rsB, rsA);                    \
}

MUL_RETURN32_16_PH(muleu_s_ph_qbl, mul_u8_u16, \
                      24, 16, MIPSDSP_Q0, \
                      16, 0, MIPSDSP_LO);
MUL_RETURN32_16_PH(muleu_s_ph_qbr, mul_u8_u16, \
                      8, 0, MIPSDSP_Q0, \
                      16, 0, MIPSDSP_LO);
MUL_RETURN32_16_PH(mulq_rs_ph, rndq15_mul_q15_q15, \
                      16, 0, MIPSDSP_LO, \
                      16, 0, MIPSDSP_LO);
MUL_RETURN32_16_PH(mul_ph, mul_i16_i16, \
                      16, 0, MIPSDSP_LO, \
                      16, 0, MIPSDSP_LO);
MUL_RETURN32_16_PH(mul_s_ph, sat16_mul_i16_i16, \
                      16, 0, MIPSDSP_LO, \
                      16, 0, MIPSDSP_LO);
MUL_RETURN32_16_PH(mulq_s_ph, sat16_mul_q15_q15, \
                      16, 0, MIPSDSP_LO, \
                      16, 0, MIPSDSP_LO);

#undef MUL_RETURN32_16_PH

#define MUL_RETURN32_32_ph(name, func, movbits) \
target_ulong helper_##name(target_ulong rs, target_ulong rt, \
                                  CPUMIPSState *env)         \
{                                                            \
    int16_t rsh, rth;                                        \
    int32_t temp;                                            \
                                                             \
    rsh = (rs >> movbits) & MIPSDSP_LO;                      \
    rth = (rt >> movbits) & MIPSDSP_LO;                      \
    temp = mipsdsp_##func(rsh, rth, env);                    \
                                                             \
    return (target_long)(int32_t)temp;                       \
}

MUL_RETURN32_32_ph(muleq_s_w_phl, mul_q15_q15_overflowflag21, 16);
MUL_RETURN32_32_ph(muleq_s_w_phr, mul_q15_q15_overflowflag21, 0);

#undef MUL_RETURN32_32_ph

#define MUL_VOID_PH(name, use_ac_env) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt,        \
                          CPUMIPSState *env)                             \
{                                                                        \
    int16_t rsh, rsl, rth, rtl;                                          \
    int32_t tempB, tempA;                                                \
    int64_t acc, dotp;                                                   \
                                                                         \
    MIPSDSP_SPLIT32_16(rs, rsh, rsl);                                    \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                                    \
                                                                         \
    if (use_ac_env == 1) {                                               \
        tempB = mipsdsp_mul_q15_q15(ac, rsh, rth, env);                  \
        tempA = mipsdsp_mul_q15_q15(ac, rsl, rtl, env);                  \
    } else {                                                             \
        tempB = mipsdsp_mul_u16_u16(rsh, rth);                           \
        tempA = mipsdsp_mul_u16_u16(rsl, rtl);                           \
    }                                                                    \
                                                                         \
    dotp = (int64_t)tempB - (int64_t)tempA;                              \
    acc = ((uint64_t)env->active_tc.HI[ac] << 32) |                      \
          ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);               \
    dotp = dotp + acc;                                                   \
    env->active_tc.HI[ac] = (target_long)(int32_t)                       \
                            ((dotp & MIPSDSP_LHI) >> 32);                \
    env->active_tc.LO[ac] = (target_long)(int32_t)(dotp & MIPSDSP_LLO);  \
}

MUL_VOID_PH(mulsaq_s_w_ph, 1);
MUL_VOID_PH(mulsa_w_ph, 0);

#undef MUL_VOID_PH

#if defined(TARGET_MIPS64)
#define MUL_RETURN64_16_QH(name, func, \
                           rsmov1, rsmov2, rsmov3, rsmov4, rsfilter, \
                           rtmov1, rtmov2, rtmov3, rtmov4, rtfilter) \
target_ulong helper_##name(target_ulong rs, target_ulong rt,         \
                           CPUMIPSState *env)                        \
{                                                                    \
    uint16_t rs3, rs2, rs1, rs0;                                     \
    uint16_t rt3, rt2, rt1, rt0;                                     \
    uint16_t tempD, tempC, tempB, tempA;                             \
                                                                     \
    rs3 = (rs >> rsmov1) & rsfilter;                                 \
    rs2 = (rs >> rsmov2) & rsfilter;                                 \
    rs1 = (rs >> rsmov3) & rsfilter;                                 \
    rs0 = (rs >> rsmov4) & rsfilter;                                 \
    rt3 = (rt >> rtmov1) & rtfilter;                                 \
    rt2 = (rt >> rtmov2) & rtfilter;                                 \
    rt1 = (rt >> rtmov3) & rtfilter;                                 \
    rt0 = (rt >> rtmov4) & rtfilter;                                 \
                                                                     \
    tempD = mipsdsp_##func(rs3, rt3, env);                           \
    tempC = mipsdsp_##func(rs2, rt2, env);                           \
    tempB = mipsdsp_##func(rs1, rt1, env);                           \
    tempA = mipsdsp_##func(rs0, rt0, env);                           \
                                                                     \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);          \
}

MUL_RETURN64_16_QH(muleu_s_qh_obl, mul_u8_u16, \
                   56, 48, 40, 32, MIPSDSP_Q0, \
                   48, 32, 16, 0, MIPSDSP_LO);
MUL_RETURN64_16_QH(muleu_s_qh_obr, mul_u8_u16, \
                   24, 16, 8, 0, MIPSDSP_Q0, \
                   48, 32, 16, 0, MIPSDSP_LO);
MUL_RETURN64_16_QH(mulq_rs_qh, rndq15_mul_q15_q15, \
                   48, 32, 16, 0, MIPSDSP_LO, \
                   48, 32, 16, 0, MIPSDSP_LO);

#undef MUL_RETURN64_16_QH

#define MUL_RETURN64_32_QH(name, \
                           rsmov1, rsmov2, \
                           rtmov1, rtmov2) \
target_ulong helper_##name(target_ulong rs, target_ulong rt, \
                           CPUMIPSState *env)                \
{                                                            \
    uint16_t rsB, rsA;                                       \
    uint16_t rtB, rtA;                                       \
    uint32_t tempB, tempA;                                   \
                                                             \
    rsB = (rs >> rsmov1) & MIPSDSP_LO;                       \
    rsA = (rs >> rsmov2) & MIPSDSP_LO;                       \
    rtB = (rt >> rtmov1) & MIPSDSP_LO;                       \
    rtA = (rt >> rtmov2) & MIPSDSP_LO;                       \
                                                             \
    tempB = mipsdsp_mul_q15_q15(5, rsB, rtB, env);           \
    tempA = mipsdsp_mul_q15_q15(5, rsA, rtA, env);           \
                                                             \
    return ((uint64_t)tempB << 32) | (uint64_t)tempA;        \
}

MUL_RETURN64_32_QH(muleq_s_pw_qhl, 48, 32, 48, 32);
MUL_RETURN64_32_QH(muleq_s_pw_qhr, 16, 0, 16, 0);

#undef MUL_RETURN64_32_QH

void helper_mulsaq_s_w_qh(target_ulong rs, target_ulong rt, uint32_t ac,
                          CPUMIPSState *env)
{
    int16_t rs3, rs2, rs1, rs0;
    int16_t rt3, rt2, rt1, rt0;
    int32_t tempD, tempC, tempB, tempA;
    int64_t acc[2];
    int64_t temp[2];
    int64_t temp_sum;

    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);

    tempD = mipsdsp_mul_q15_q15(ac, rs3, rt3, env);
    tempC = mipsdsp_mul_q15_q15(ac, rs2, rt2, env);
    tempB = mipsdsp_mul_q15_q15(ac, rs1, rt1, env);
    tempA = mipsdsp_mul_q15_q15(ac, rs0, rt0, env);

    temp[0] = ((int32_t)tempD - (int32_t)tempC) +
              ((int32_t)tempB - (int32_t)tempA);
    temp[0] = (int64_t)(temp[0] << 30) >> 30;
    if (((temp[0] >> 33) & 0x01) == 0) {
        temp[1] = 0x00;
    } else {
        temp[1] = ~0ull;
    }

    acc[0] = env->active_tc.LO[ac];
    acc[1] = env->active_tc.HI[ac];

    temp_sum = acc[0] + temp[0];
    if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&
       ((uint64_t)temp_sum < (uint64_t)temp[0])) {
        acc[1] += 1;
    }
    acc[0] = temp_sum;
    acc[1] += temp[1];

    env->active_tc.HI[ac] = acc[1];
    env->active_tc.LO[ac] = acc[0];
}
#endif

#define DP_QB(name, func, is_add, rsmov1, rsmov2, rtmov1, rtmov2) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt,        \
                   CPUMIPSState *env)                                    \
{                                                                        \
    uint8_t rs3, rs2;                                                    \
    uint8_t rt3, rt2;                                                    \
    uint16_t tempB, tempA;                                               \
    uint64_t tempC, dotp;                                                \
                                                                         \
    rs3 = (rs >> rsmov1) & MIPSDSP_Q0;                                   \
    rs2 = (rs >> rsmov2) & MIPSDSP_Q0;                                   \
    rt3 = (rt >> rtmov1) & MIPSDSP_Q0;                                   \
    rt2 = (rt >> rtmov2) & MIPSDSP_Q0;                                   \
    tempB = mipsdsp_##func(rs3, rt3);                                    \
    tempA = mipsdsp_##func(rs2, rt2);                                    \
    dotp = (int64_t)tempB + (int64_t)tempA;                              \
    if (is_add) {                                                        \
        tempC = (((uint64_t)env->active_tc.HI[ac] << 32) |               \
                 ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO))        \
            + dotp;                                                      \
    } else {                                                             \
        tempC = (((uint64_t)env->active_tc.HI[ac] << 32) |               \
                 ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO))        \
            - dotp;                                                      \
    }                                                                    \
                                                                         \
    env->active_tc.HI[ac] = (target_long)(int32_t)                       \
                            ((tempC & MIPSDSP_LHI) >> 32);               \
    env->active_tc.LO[ac] = (target_long)(int32_t)(tempC & MIPSDSP_LLO); \
}

DP_QB(dpau_h_qbl, mul_u8_u8, 1, 24, 16, 24, 16);
DP_QB(dpau_h_qbr, mul_u8_u8, 1, 8, 0, 8, 0);
DP_QB(dpsu_h_qbl, mul_u8_u8, 0, 24, 16, 24, 16);
DP_QB(dpsu_h_qbr, mul_u8_u8, 0, 8, 0, 8, 0);

#undef DP_QB

#if defined(TARGET_MIPS64)
#define DP_OB(name, add_sub, \
              rsmov1, rsmov2, rsmov3, rsmov4, \
              rtmov1, rtmov2, rtmov3, rtmov4) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac,       \
                       CPUMIPSState *env)                               \
{                                                                       \
    uint8_t rsD, rsC, rsB, rsA;                                         \
    uint8_t rtD, rtC, rtB, rtA;                                         \
    uint16_t tempD, tempC, tempB, tempA;                                \
    uint64_t temp[2];                                                   \
    uint64_t acc[2];                                                    \
    uint64_t temp_sum;                                                  \
                                                                        \
    temp[0] = 0;                                                        \
    temp[1] = 0;                                                        \
                                                                        \
    rsD = (rs >> rsmov1) & MIPSDSP_Q0;                                  \
    rsC = (rs >> rsmov2) & MIPSDSP_Q0;                                  \
    rsB = (rs >> rsmov3) & MIPSDSP_Q0;                                  \
    rsA = (rs >> rsmov4) & MIPSDSP_Q0;                                  \
    rtD = (rt >> rtmov1) & MIPSDSP_Q0;                                  \
    rtC = (rt >> rtmov2) & MIPSDSP_Q0;                                  \
    rtB = (rt >> rtmov3) & MIPSDSP_Q0;                                  \
    rtA = (rt >> rtmov4) & MIPSDSP_Q0;                                  \
                                                                        \
    tempD = mipsdsp_mul_u8_u8(rsD, rtD);                                \
    tempC = mipsdsp_mul_u8_u8(rsC, rtC);                                \
    tempB = mipsdsp_mul_u8_u8(rsB, rtB);                                \
    tempA = mipsdsp_mul_u8_u8(rsA, rtA);                                \
                                                                        \
    temp[0] = (uint64_t)tempD + (uint64_t)tempC +                       \
      (uint64_t)tempB + (uint64_t)tempA;                                \
                                                                        \
    acc[0] = env->active_tc.LO[ac];                                     \
    acc[1] = env->active_tc.HI[ac];                                     \
                                                                        \
    if (add_sub) {                                                      \
        temp_sum = acc[0] + temp[0];                                    \
        if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&                  \
            ((uint64_t)temp_sum < (uint64_t)temp[0])) {                 \
            acc[1] += 1;                                                \
        }                                                               \
        temp[0] = temp_sum;                                             \
        temp[1] = acc[1] + temp[1];                                     \
    } else {                                                            \
        temp_sum = acc[0] - temp[0];                                    \
        if ((uint64_t)temp_sum > (uint64_t)acc[0]) {                    \
            acc[1] -= 1;                                                \
        }                                                               \
        temp[0] = temp_sum;                                             \
        temp[1] = acc[1] - temp[1];                                     \
    }                                                                   \
                                                                        \
    env->active_tc.HI[ac] = temp[1];                                    \
    env->active_tc.LO[ac] = temp[0];                                    \
}

DP_OB(dpau_h_obl, 1, 56, 48, 40, 32, 56, 48, 40, 32);
DP_OB(dpau_h_obr, 1, 24, 16, 8, 0, 24, 16, 8, 0);
DP_OB(dpsu_h_obl, 0, 56, 48, 40, 32, 56, 48, 40, 32);
DP_OB(dpsu_h_obr, 0, 24, 16, 8, 0, 24, 16, 8, 0);

#undef DP_OB
#endif

#define DP_NOFUNC_PH(name, is_add, rsmov1, rsmov2, rtmov1, rtmov2)             \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt,              \
                   CPUMIPSState *env)                                          \
{                                                                              \
    int16_t rsB, rsA, rtB, rtA;                                                \
    int32_t  tempA, tempB;                                                     \
    int64_t  acc;                                                              \
                                                                               \
    rsB = (rs >> rsmov1) & MIPSDSP_LO;                                         \
    rsA = (rs >> rsmov2) & MIPSDSP_LO;                                         \
    rtB = (rt >> rtmov1) & MIPSDSP_LO;                                         \
    rtA = (rt >> rtmov2) & MIPSDSP_LO;                                         \
                                                                               \
    tempB = (int32_t)rsB * (int32_t)rtB;                                       \
    tempA = (int32_t)rsA * (int32_t)rtA;                                       \
                                                                               \
    acc = ((uint64_t)env->active_tc.HI[ac] << 32) |                            \
          ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);                     \
                                                                               \
    if (is_add) {                                                              \
        acc = acc + ((int64_t)tempB + (int64_t)tempA);                         \
    } else {                                                                   \
        acc = acc - ((int64_t)tempB + (int64_t)tempA);                         \
    }                                                                          \
                                                                               \
    env->active_tc.HI[ac] = (target_long)(int32_t)((acc & MIPSDSP_LHI) >> 32); \
    env->active_tc.LO[ac] = (target_long)(int32_t)(acc & MIPSDSP_LLO);         \
}

DP_NOFUNC_PH(dpa_w_ph, 1, 16, 0, 16, 0);
DP_NOFUNC_PH(dpax_w_ph, 1, 16, 0, 0, 16);
DP_NOFUNC_PH(dps_w_ph, 0, 16, 0, 16, 0);
DP_NOFUNC_PH(dpsx_w_ph, 0, 16, 0, 0, 16);
#undef DP_NOFUNC_PH

#define DP_HASFUNC_PH(name, is_add, rsmov1, rsmov2, rtmov1, rtmov2) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt,   \
                   CPUMIPSState *env)                      \
{                                                          \
    int16_t rsB, rsA, rtB, rtA;                            \
    int32_t tempB, tempA;                                  \
    int64_t acc, dotp;                                     \
                                                           \
    rsB = (rs >> rsmov1) & MIPSDSP_LO;                     \
    rsA = (rs >> rsmov2) & MIPSDSP_LO;                     \
    rtB = (rt >> rtmov1) & MIPSDSP_LO;                     \
    rtA = (rt >> rtmov2) & MIPSDSP_LO;                     \
                                                           \
    tempB = mipsdsp_mul_q15_q15(ac, rsB, rtB, env);        \
    tempA = mipsdsp_mul_q15_q15(ac, rsA, rtA, env);        \
                                                           \
    dotp = (int64_t)tempB + (int64_t)tempA;                \
    acc = ((uint64_t)env->active_tc.HI[ac] << 32) |        \
          ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO); \
                                                           \
    if (is_add) {                                          \
        acc = acc + dotp;                                  \
    } else {                                               \
        acc = acc - dotp;                                  \
    }                                                      \
                                                           \
    env->active_tc.HI[ac] = (target_long)(int32_t)         \
        ((acc & MIPSDSP_LHI) >> 32);                       \
    env->active_tc.LO[ac] = (target_long)(int32_t)         \
        (acc & MIPSDSP_LLO);                               \
}

DP_HASFUNC_PH(dpaq_s_w_ph, 1, 16, 0, 16, 0);
DP_HASFUNC_PH(dpaqx_s_w_ph, 1, 16, 0, 0, 16);
DP_HASFUNC_PH(dpsq_s_w_ph, 0, 16, 0, 16, 0);
DP_HASFUNC_PH(dpsqx_s_w_ph, 0, 16, 0, 0, 16);

#undef DP_HASFUNC_PH

#define DP_128OPERATION_PH(name, is_add) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt, \
                          CPUMIPSState *env)                             \
{                                                                        \
    int16_t rsh, rsl, rth, rtl;                                          \
    int32_t tempB, tempA, tempC62_31, tempC63;                           \
    int64_t acc, dotp, tempC;                                            \
                                                                         \
    MIPSDSP_SPLIT32_16(rs, rsh, rsl);                                    \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                                    \
                                                                         \
    tempB = mipsdsp_mul_q15_q15(ac, rsh, rtl, env);                      \
    tempA = mipsdsp_mul_q15_q15(ac, rsl, rth, env);                      \
                                                                         \
    dotp = (int64_t)tempB + (int64_t)tempA;                              \
    acc = ((uint64_t)env->active_tc.HI[ac] << 32) |                      \
          ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);               \
    if (is_add) {                                                        \
        tempC = acc + dotp;                                              \
    } else {                                                             \
        tempC = acc - dotp;                                              \
    }                                                                    \
    tempC63 = (tempC >> 63) & 0x01;                                      \
    tempC62_31 = (tempC >> 31) & 0xFFFFFFFF;                             \
                                                                         \
    if ((tempC63 == 0) && (tempC62_31 != 0x00000000)) {                  \
        tempC = 0x7FFFFFFF;                                              \
        set_DSPControl_overflow_flag(1, 16 + ac, env);                   \
    }                                                                    \
                                                                         \
    if ((tempC63 == 1) && (tempC62_31 != 0xFFFFFFFF)) {                  \
        tempC = (int64_t)(int32_t)0x80000000;                            \
        set_DSPControl_overflow_flag(1, 16 + ac, env);                   \
    }                                                                    \
                                                                         \
    env->active_tc.HI[ac] = (target_long)(int32_t)                       \
        ((tempC & MIPSDSP_LHI) >> 32);                                   \
    env->active_tc.LO[ac] = (target_long)(int32_t)                       \
        (tempC & MIPSDSP_LLO);                                           \
}

DP_128OPERATION_PH(dpaqx_sa_w_ph, 1);
DP_128OPERATION_PH(dpsqx_sa_w_ph, 0);

#undef DP_128OPERATION_HP

#if defined(TARGET_MIPS64)
#define DP_QH(name, is_add, use_ac_env) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac,    \
                   CPUMIPSState *env)                                \
{                                                                    \
    int32_t rs3, rs2, rs1, rs0;                                      \
    int32_t rt3, rt2, rt1, rt0;                                      \
    int32_t tempD, tempC, tempB, tempA;                              \
    int64_t acc[2];                                                  \
    int64_t temp[2];                                                 \
    int64_t temp_sum;                                                \
                                                                     \
    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);                      \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                      \
                                                                     \
    if (use_ac_env) {                                                \
        tempD = mipsdsp_mul_q15_q15(ac, rs3, rt3, env);              \
        tempC = mipsdsp_mul_q15_q15(ac, rs2, rt2, env);              \
        tempB = mipsdsp_mul_q15_q15(ac, rs1, rt1, env);              \
        tempA = mipsdsp_mul_q15_q15(ac, rs0, rt0, env);              \
    } else {                                                         \
        tempD = mipsdsp_mul_u16_u16(rs3, rt3);                       \
        tempC = mipsdsp_mul_u16_u16(rs2, rt2);                       \
        tempB = mipsdsp_mul_u16_u16(rs1, rt1);                       \
        tempA = mipsdsp_mul_u16_u16(rs0, rt0);                       \
    }                                                                \
                                                                     \
    temp[0] = (int64_t)tempD + (int64_t)tempC +                      \
              (int64_t)tempB + (int64_t)tempA;                       \
                                                                     \
    if (temp[0] >= 0) {                                              \
        temp[1] = 0;                                                 \
    } else {                                                         \
        temp[1] = ~0ull;                                             \
    }                                                                \
                                                                     \
    acc[1] = env->active_tc.HI[ac];                                  \
    acc[0] = env->active_tc.LO[ac];                                  \
                                                                     \
    if (is_add) {                                                    \
        temp_sum = acc[0] + temp[0];                                 \
        if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&               \
            ((uint64_t)temp_sum < (uint64_t)temp[0])) {              \
            acc[1] = acc[1] + 1;                                     \
        }                                                            \
        temp[0] = temp_sum;                                          \
        temp[1] = acc[1] + temp[1];                                  \
    } else {                                                         \
        temp_sum = acc[0] - temp[0];                                 \
        if ((uint64_t)temp_sum > (uint64_t)acc[0]) {                 \
            acc[1] = acc[1] - 1;                                     \
        }                                                            \
        temp[0] = temp_sum;                                          \
        temp[1] = acc[1] - temp[1];                                  \
    }                                                                \
                                                                     \
    env->active_tc.HI[ac] = temp[1];                                 \
    env->active_tc.LO[ac] = temp[0];                                 \
}

DP_QH(dpa_w_qh, 1, 0);
DP_QH(dpaq_s_w_qh, 1, 1);
DP_QH(dps_w_qh, 0, 0);
DP_QH(dpsq_s_w_qh, 0, 1);

#undef DP_QH

#endif

#define DP_L_W(name, is_add) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt,      \
                   CPUMIPSState *env)                                  \
{                                                                      \
    int32_t temp63;                                                    \
    int64_t dotp, acc;                                                 \
    uint64_t temp;                                                     \
    bool overflow;                                                     \
                                                                       \
    dotp = mipsdsp_mul_q31_q31(ac, rs, rt, env);                       \
    acc = ((uint64_t)env->active_tc.HI[ac] << 32) |                    \
          ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);             \
    if (is_add) {                                                      \
        temp = acc + dotp;                                             \
        overflow = MIPSDSP_OVERFLOW_ADD((uint64_t)acc, (uint64_t)dotp, \
                                        temp, (0x01ull << 63));        \
    } else {                                                           \
        temp = acc - dotp;                                             \
        overflow = MIPSDSP_OVERFLOW_SUB((uint64_t)acc, (uint64_t)dotp, \
                                        temp, (0x01ull << 63));        \
    }                                                                  \
                                                                       \
    if (overflow) {                                                    \
        temp63 = (temp >> 63) & 0x01;                                  \
        if (temp63 == 1) {                                             \
            temp = (0x01ull << 63) - 1;                                \
        } else {                                                       \
            temp = 0x01ull << 63;                                      \
        }                                                              \
                                                                       \
        set_DSPControl_overflow_flag(1, 16 + ac, env);                 \
    }                                                                  \
                                                                       \
    env->active_tc.HI[ac] = (target_long)(int32_t)                     \
        ((temp & MIPSDSP_LHI) >> 32);                                  \
    env->active_tc.LO[ac] = (target_long)(int32_t)                     \
        (temp & MIPSDSP_LLO);                                          \
}

DP_L_W(dpaq_sa_l_w, 1);
DP_L_W(dpsq_sa_l_w, 0);

#undef DP_L_W

#if defined(TARGET_MIPS64)
#define DP_L_PW(name, func) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac, \
                   CPUMIPSState *env)                             \
{                                                                 \
    int32_t rs1, rs0;                                             \
    int32_t rt1, rt0;                                             \
    int64_t tempB[2], tempA[2];                                   \
    int64_t temp[2];                                              \
    int64_t acc[2];                                               \
    int64_t temp_sum;                                             \
                                                                  \
    temp[0] = 0;                                                  \
    temp[1] = 0;                                                  \
                                                                  \
    MIPSDSP_SPLIT64_32(rs, rs1, rs0);                             \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                             \
                                                                  \
    tempB[0] = mipsdsp_mul_q31_q31(ac, rs1, rt1, env);            \
    tempA[0] = mipsdsp_mul_q31_q31(ac, rs0, rt0, env);            \
                                                                  \
    if (tempB[0] >= 0) {                                          \
        tempB[1] = 0x00;                                          \
    } else {                                                      \
        tempB[1] = ~0ull;                                         \
    }                                                             \
                                                                  \
    if (tempA[0] >= 0) {                                          \
        tempA[1] = 0x00;                                          \
    } else {                                                      \
        tempA[1] = ~0ull;                                         \
    }                                                             \
                                                                  \
    temp_sum = tempB[0] + tempA[0];                               \
    if (((uint64_t)temp_sum < (uint64_t)tempB[0]) &&              \
        ((uint64_t)temp_sum < (uint64_t)tempA[0])) {              \
        temp[1] += 1;                                             \
    }                                                             \
    temp[0] = temp_sum;                                           \
    temp[1] += tempB[1] + tempA[1];                               \
                                                                  \
    mipsdsp_##func(acc, ac, temp, env);                           \
                                                                  \
    env->active_tc.HI[ac] = acc[1];                               \
    env->active_tc.LO[ac] = acc[0];                               \
}

DP_L_PW(dpaq_sa_l_pw, sat64_acc_add_q63);
DP_L_PW(dpsq_sa_l_pw, sat64_acc_sub_q63);

#undef DP_L_PW

void helper_mulsaq_s_l_pw(target_ulong rs, target_ulong rt, uint32_t ac,
                          CPUMIPSState *env)
{
    int32_t rs1, rs0;
    int32_t rt1, rt0;
    int64_t tempB[2], tempA[2];
    int64_t temp[2];
    int64_t acc[2];
    int64_t temp_sum;

    rs1 = (rs >> 32) & MIPSDSP_LLO;
    rs0 = rs & MIPSDSP_LLO;
    rt1 = (rt >> 32) & MIPSDSP_LLO;
    rt0 = rt & MIPSDSP_LLO;

    tempB[0] = mipsdsp_mul_q31_q31(ac, rs1, rt1, env);
    tempA[0] = mipsdsp_mul_q31_q31(ac, rs0, rt0, env);

    if (tempB[0] >= 0) {
        tempB[1] = 0x00;
    } else {
        tempB[1] = ~0ull;
    }

    if (tempA[0] >= 0) {
        tempA[1] = 0x00;
    } else {
        tempA[1] = ~0ull;
    }

    acc[0] = env->active_tc.LO[ac];
    acc[1] = env->active_tc.HI[ac];

    temp_sum = tempB[0] - tempA[0];
    if ((uint64_t)temp_sum > (uint64_t)tempB[0]) {
        tempB[1] -= 1;
    }
    temp[0] = temp_sum;
    temp[1] = tempB[1] - tempA[1];

    if ((temp[1] & 0x01) == 0) {
        temp[1] = 0x00;
    } else {
        temp[1] = ~0ull;
    }

    temp_sum = acc[0] + temp[0];
    if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&
       ((uint64_t)temp_sum < (uint64_t)temp[0])) {
        acc[1] += 1;
    }
    acc[0] = temp_sum;
    acc[1] += temp[1];

    env->active_tc.HI[ac] = acc[1];
    env->active_tc.LO[ac] = acc[0];
}
#endif

#define MAQ_S_W(name, mov) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt, \
                   CPUMIPSState *env)                             \
{                                                                 \
    int16_t rsh, rth;                                             \
    int32_t tempA;                                                \
    int64_t tempL, acc;                                           \
                                                                  \
    rsh = (rs >> mov) & MIPSDSP_LO;                               \
    rth = (rt >> mov) & MIPSDSP_LO;                               \
    tempA  = mipsdsp_mul_q15_q15(ac, rsh, rth, env);              \
    acc = ((uint64_t)env->active_tc.HI[ac] << 32) |               \
          ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);        \
    tempL  = (int64_t)tempA + acc;                                \
    env->active_tc.HI[ac] = (target_long)(int32_t)                \
        ((tempL & MIPSDSP_LHI) >> 32);                            \
    env->active_tc.LO[ac] = (target_long)(int32_t)                \
        (tempL & MIPSDSP_LLO);                                    \
}

MAQ_S_W(maq_s_w_phl, 16);
MAQ_S_W(maq_s_w_phr, 0);

#undef MAQ_S_W

#define MAQ_SA_W(name, mov) \
void helper_##name(uint32_t ac, target_ulong rs, target_ulong rt,        \
                   CPUMIPSState *env)                                    \
{                                                                        \
    int16_t rsh, rth;                                                    \
    int32_t tempA;                                                       \
                                                                         \
    rsh = (rs >> mov) & MIPSDSP_LO;                                      \
    rth = (rt >> mov) & MIPSDSP_LO;                                      \
    tempA = mipsdsp_mul_q15_q15(ac, rsh, rth, env);                      \
    tempA = mipsdsp_sat32_acc_q31(ac, tempA, env);                       \
                                                                         \
    env->active_tc.HI[ac] = (target_long)(int32_t)(((int64_t)tempA &     \
                                                    MIPSDSP_LHI) >> 32); \
    env->active_tc.LO[ac] = (target_long)(int32_t)((int64_t)tempA &      \
                                                   MIPSDSP_LLO);         \
}

MAQ_SA_W(maq_sa_w_phl, 16);
MAQ_SA_W(maq_sa_w_phr, 0);

#undef MAQ_SA_W

#define MULQ_W(name, addvar) \
target_ulong helper_##name(target_ulong rs, target_ulong rt,   \
                           CPUMIPSState *env)                  \
{                                                              \
    int32_t rs_t, rt_t;                                        \
    int32_t tempI;                                             \
    int64_t tempL;                                             \
                                                               \
    rs_t = rs & MIPSDSP_LLO;                                   \
    rt_t = rt & MIPSDSP_LLO;                                   \
                                                               \
    if ((rs_t == 0x80000000) && (rt_t == 0x80000000)) {        \
        tempL = 0x7FFFFFFF00000000ull;                         \
        set_DSPControl_overflow_flag(1, 21, env);              \
    } else {                                                   \
        tempL  = ((int64_t)rs_t * (int64_t)rt_t) << 1;         \
        tempL += addvar;                                       \
    }                                                          \
    tempI = (tempL & MIPSDSP_LHI) >> 32;                       \
                                                               \
    return (target_long)(int32_t)tempI;                        \
}

MULQ_W(mulq_s_w, 0);
MULQ_W(mulq_rs_w, 0x80000000ull);

#undef MULQ_W

#if defined(TARGET_MIPS64)

#define MAQ_S_W_QH(name, mov) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac, \
                   CPUMIPSState *env)                             \
{                                                                 \
    int16_t rs_t, rt_t;                                           \
    int32_t temp_mul;                                             \
    int64_t temp[2];                                              \
    int64_t acc[2];                                               \
    int64_t temp_sum;                                             \
                                                                  \
    temp[0] = 0;                                                  \
    temp[1] = 0;                                                  \
                                                                  \
    rs_t = (rs >> mov) & MIPSDSP_LO;                              \
    rt_t = (rt >> mov) & MIPSDSP_LO;                              \
    temp_mul = mipsdsp_mul_q15_q15(ac, rs_t, rt_t, env);          \
                                                                  \
    temp[0] = (int64_t)temp_mul;                                  \
    if (temp[0] >= 0) {                                           \
        temp[1] = 0x00;                                           \
    } else {                                                      \
        temp[1] = ~0ull;                                          \
    }                                                             \
                                                                  \
    acc[0] = env->active_tc.LO[ac];                               \
    acc[1] = env->active_tc.HI[ac];                               \
                                                                  \
    temp_sum = acc[0] + temp[0];                                  \
    if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&                \
        ((uint64_t)temp_sum < (uint64_t)temp[0])) {               \
        acc[1] += 1;                                              \
    }                                                             \
    acc[0] = temp_sum;                                            \
    acc[1] += temp[1];                                            \
                                                                  \
    env->active_tc.HI[ac] = acc[1];                               \
    env->active_tc.LO[ac] = acc[0];                               \
}

MAQ_S_W_QH(maq_s_w_qhll, 48);
MAQ_S_W_QH(maq_s_w_qhlr, 32);
MAQ_S_W_QH(maq_s_w_qhrl, 16);
MAQ_S_W_QH(maq_s_w_qhrr, 0);

#undef MAQ_S_W_QH

#define MAQ_SA_W(name, mov) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac, \
                   CPUMIPSState *env)                             \
{                                                                 \
    int16_t rs_t, rt_t;                                           \
    int32_t temp;                                                 \
    int64_t acc[2];                                               \
                                                                  \
    rs_t = (rs >> mov) & MIPSDSP_LO;                              \
    rt_t = (rt >> mov) & MIPSDSP_LO;                              \
    temp = mipsdsp_mul_q15_q15(ac, rs_t, rt_t, env);              \
    temp = mipsdsp_sat32_acc_q31(ac, temp, env);                  \
                                                                  \
    acc[0] = (int64_t)(int32_t)temp;                              \
    if (acc[0] >= 0) {                                            \
        acc[1] = 0x00;                                            \
    } else {                                                      \
        acc[1] = ~0ull;                                           \
    }                                                             \
                                                                  \
    env->active_tc.HI[ac] = acc[1];                               \
    env->active_tc.LO[ac] = acc[0];                               \
}

MAQ_SA_W(maq_sa_w_qhll, 48);
MAQ_SA_W(maq_sa_w_qhlr, 32);
MAQ_SA_W(maq_sa_w_qhrl, 16);
MAQ_SA_W(maq_sa_w_qhrr, 0);

#undef MAQ_SA_W

#define MAQ_S_L_PW(name, mov) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac, \
                   CPUMIPSState *env)                             \
{                                                                 \
    int32_t rs_t, rt_t;                                           \
    int64_t temp[2];                                              \
    int64_t acc[2];                                               \
    int64_t temp_sum;                                             \
                                                                  \
    temp[0] = 0;                                                  \
    temp[1] = 0;                                                  \
                                                                  \
    rs_t = (rs >> mov) & MIPSDSP_LLO;                             \
    rt_t = (rt >> mov) & MIPSDSP_LLO;                             \
                                                                  \
    temp[0] = mipsdsp_mul_q31_q31(ac, rs_t, rt_t, env);           \
    if (temp[0] >= 0) {                                           \
        temp[1] = 0x00;                                           \
    } else {                                                      \
        temp[1] = ~0ull;                                          \
    }                                                             \
                                                                  \
    acc[0] = env->active_tc.LO[ac];                               \
    acc[1] = env->active_tc.HI[ac];                               \
                                                                  \
    temp_sum = acc[0] + temp[0];                                  \
    if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&                \
        ((uint64_t)temp_sum < (uint64_t)temp[0])) {               \
        acc[1] += 1;                                              \
    }                                                             \
    acc[0] = temp_sum;                                            \
    acc[1] += temp[1];                                            \
                                                                  \
    env->active_tc.HI[ac] = acc[1];                               \
    env->active_tc.LO[ac] = acc[0];                               \
}

MAQ_S_L_PW(maq_s_l_pwl, 32);
MAQ_S_L_PW(maq_s_l_pwr, 0);

#undef MAQ_S_L_PW

#define DM_OPERATE(name, func, is_add, sigext) \
void helper_##name(target_ulong rs, target_ulong rt, uint32_t ac,    \
                  CPUMIPSState *env)                                 \
{                                                                    \
    int32_t rs1, rs0;                                                \
    int32_t rt1, rt0;                                                \
    int64_t tempBL[2], tempAL[2];                                    \
    int64_t acc[2];                                                  \
    int64_t temp[2];                                                 \
    int64_t temp_sum;                                                \
                                                                     \
    temp[0] = 0x00;                                                  \
    temp[1] = 0x00;                                                  \
                                                                     \
    MIPSDSP_SPLIT64_32(rs, rs1, rs0);                                \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                                \
                                                                     \
    if (sigext) {                                                    \
        tempBL[0] = (int64_t)mipsdsp_##func(rs1, rt1);               \
        tempAL[0] = (int64_t)mipsdsp_##func(rs0, rt0);               \
                                                                     \
        if (tempBL[0] >= 0) {                                        \
            tempBL[1] = 0x0;                                         \
        } else {                                                     \
            tempBL[1] = ~0ull;                                       \
        }                                                            \
                                                                     \
        if (tempAL[0] >= 0) {                                        \
            tempAL[1] = 0x0;                                         \
        } else {                                                     \
            tempAL[1] = ~0ull;                                       \
        }                                                            \
    } else {                                                         \
        tempBL[0] = mipsdsp_##func(rs1, rt1);                        \
        tempAL[0] = mipsdsp_##func(rs0, rt0);                        \
        tempBL[1] = 0;                                               \
        tempAL[1] = 0;                                               \
    }                                                                \
                                                                     \
    acc[1] = env->active_tc.HI[ac];                                  \
    acc[0] = env->active_tc.LO[ac];                                  \
                                                                     \
    temp_sum = tempBL[0] + tempAL[0];                                \
    if (((uint64_t)temp_sum < (uint64_t)tempBL[0]) &&                \
        ((uint64_t)temp_sum < (uint64_t)tempAL[0])) {                \
        temp[1] += 1;                                                \
    }                                                                \
    temp[0] = temp_sum;                                              \
    temp[1] += tempBL[1] + tempAL[1];                                \
                                                                     \
    if (is_add) {                                                    \
        temp_sum = acc[0] + temp[0];                                 \
        if (((uint64_t)temp_sum < (uint64_t)acc[0]) &&               \
            ((uint64_t)temp_sum < (uint64_t)temp[0])) {              \
            acc[1] += 1;                                             \
        }                                                            \
        temp[0] = temp_sum;                                          \
        temp[1] = acc[1] + temp[1];                                  \
    } else {                                                         \
        temp_sum = acc[0] - temp[0];                                 \
        if ((uint64_t)temp_sum > (uint64_t)acc[0]) {                 \
            acc[1] -= 1;                                             \
        }                                                            \
        temp[0] = temp_sum;                                          \
        temp[1] = acc[1] - temp[1];                                  \
    }                                                                \
                                                                     \
    env->active_tc.HI[ac] = temp[1];                                 \
    env->active_tc.LO[ac] = temp[0];                                 \
}

DM_OPERATE(dmadd, mul_i32_i32, 1, 1);
DM_OPERATE(dmaddu, mul_u32_u32, 1, 0);
DM_OPERATE(dmsub, mul_i32_i32, 0, 1);
DM_OPERATE(dmsubu, mul_u32_u32, 0, 0);
#undef DM_OPERATE
#endif

/** DSP Bit/Manipulation Sub-class insns **/
target_ulong helper_bitrev(target_ulong rt)
{
    int32_t temp;
    uint32_t rd;
    int i;

    temp = rt & MIPSDSP_LO;
    rd = 0;
    for (i = 0; i < 16; i++) {
        rd = (rd << 1) | (temp & 1);
        temp = temp >> 1;
    }

    return (target_ulong)rd;
}

#define BIT_INSV(name, posfilter, ret_type)                     \
target_ulong helper_##name(CPUMIPSState *env, target_ulong rs,  \
                           target_ulong rt)                     \
{                                                               \
    uint32_t pos, size, msb, lsb;                               \
    uint32_t const sizefilter = 0x3F;                           \
    target_ulong temp;                                          \
    target_ulong dspc;                                          \
                                                                \
    dspc = env->active_tc.DSPControl;                           \
                                                                \
    pos  = dspc & posfilter;                                    \
    size = (dspc >> 7) & sizefilter;                            \
                                                                \
    msb  = pos + size - 1;                                      \
    lsb  = pos;                                                 \
                                                                \
    if (lsb > msb || (msb > TARGET_LONG_BITS)) {                \
        return rt;                                              \
    }                                                           \
                                                                \
    temp = deposit64(rt, pos, size, rs);                        \
                                                                \
    return (target_long)(ret_type)temp;                         \
}

BIT_INSV(insv, 0x1F, int32_t);
#ifdef TARGET_MIPS64
BIT_INSV(dinsv, 0x7F, target_long);
#endif

#undef BIT_INSV


/** DSP Compare-Pick Sub-class insns **/
#define CMP_HAS_RET(name, func, split_num, filter, bit_size) \
target_ulong helper_##name(target_ulong rs, target_ulong rt) \
{                                                       \
    uint32_t rs_t, rt_t;                                \
    uint8_t cc;                                         \
    uint32_t temp = 0;                                  \
    int i;                                              \
                                                        \
    for (i = 0; i < split_num; i++) {                   \
        rs_t = (rs >> (bit_size * i)) & filter;         \
        rt_t = (rt >> (bit_size * i)) & filter;         \
        cc = mipsdsp_##func(rs_t, rt_t);                \
        temp |= cc << i;                                \
    }                                                   \
                                                        \
    return (target_ulong)temp;                          \
}

CMP_HAS_RET(cmpgu_eq_qb, cmpu_eq, 4, MIPSDSP_Q0, 8);
CMP_HAS_RET(cmpgu_lt_qb, cmpu_lt, 4, MIPSDSP_Q0, 8);
CMP_HAS_RET(cmpgu_le_qb, cmpu_le, 4, MIPSDSP_Q0, 8);

#ifdef TARGET_MIPS64
CMP_HAS_RET(cmpgu_eq_ob, cmpu_eq, 8, MIPSDSP_Q0, 8);
CMP_HAS_RET(cmpgu_lt_ob, cmpu_lt, 8, MIPSDSP_Q0, 8);
CMP_HAS_RET(cmpgu_le_ob, cmpu_le, 8, MIPSDSP_Q0, 8);
#endif

#undef CMP_HAS_RET


#define CMP_NO_RET(name, func, split_num, filter, bit_size) \
void helper_##name(target_ulong rs, target_ulong rt,        \
                            CPUMIPSState *env)              \
{                                                           \
    int##bit_size##_t rs_t, rt_t;                           \
    int##bit_size##_t flag = 0;                             \
    int##bit_size##_t cc;                                   \
    int i;                                                  \
                                                            \
    for (i = 0; i < split_num; i++) {                       \
        rs_t = (rs >> (bit_size * i)) & filter;             \
        rt_t = (rt >> (bit_size * i)) & filter;             \
                                                            \
        cc = mipsdsp_##func((int32_t)rs_t, (int32_t)rt_t);  \
        flag |= cc << i;                                    \
    }                                                       \
                                                            \
    set_DSPControl_24(flag, split_num, env);                \
}

CMP_NO_RET(cmpu_eq_qb, cmpu_eq, 4, MIPSDSP_Q0, 8);
CMP_NO_RET(cmpu_lt_qb, cmpu_lt, 4, MIPSDSP_Q0, 8);
CMP_NO_RET(cmpu_le_qb, cmpu_le, 4, MIPSDSP_Q0, 8);

CMP_NO_RET(cmp_eq_ph, cmp_eq, 2, MIPSDSP_LO, 16);
CMP_NO_RET(cmp_lt_ph, cmp_lt, 2, MIPSDSP_LO, 16);
CMP_NO_RET(cmp_le_ph, cmp_le, 2, MIPSDSP_LO, 16);

#ifdef TARGET_MIPS64
CMP_NO_RET(cmpu_eq_ob, cmpu_eq, 8, MIPSDSP_Q0, 8);
CMP_NO_RET(cmpu_lt_ob, cmpu_lt, 8, MIPSDSP_Q0, 8);
CMP_NO_RET(cmpu_le_ob, cmpu_le, 8, MIPSDSP_Q0, 8);

CMP_NO_RET(cmp_eq_qh, cmp_eq, 4, MIPSDSP_LO, 16);
CMP_NO_RET(cmp_lt_qh, cmp_lt, 4, MIPSDSP_LO, 16);
CMP_NO_RET(cmp_le_qh, cmp_le, 4, MIPSDSP_LO, 16);

CMP_NO_RET(cmp_eq_pw, cmp_eq, 2, MIPSDSP_LLO, 32);
CMP_NO_RET(cmp_lt_pw, cmp_lt, 2, MIPSDSP_LLO, 32);
CMP_NO_RET(cmp_le_pw, cmp_le, 2, MIPSDSP_LLO, 32);
#endif
#undef CMP_NO_RET

#if defined(TARGET_MIPS64)

#define CMPGDU_OB(name) \
target_ulong helper_cmpgdu_##name##_ob(target_ulong rs, target_ulong rt, \
                                       CPUMIPSState *env)  \
{                                                     \
    int i;                                            \
    uint8_t rs_t, rt_t;                               \
    uint32_t cond;                                    \
                                                      \
    cond = 0;                                         \
                                                      \
    for (i = 0; i < 8; i++) {                         \
        rs_t = (rs >> (8 * i)) & MIPSDSP_Q0;          \
        rt_t = (rt >> (8 * i)) & MIPSDSP_Q0;          \
                                                      \
        if (mipsdsp_cmpu_##name(rs_t, rt_t)) {        \
            cond |= 0x01 << i;                        \
        }                                             \
    }                                                 \
                                                      \
    set_DSPControl_24(cond, 8, env);                  \
                                                      \
    return (uint64_t)cond;                            \
}

CMPGDU_OB(eq)
CMPGDU_OB(lt)
CMPGDU_OB(le)
#undef CMPGDU_OB
#endif

#define PICK_INSN(name, split_num, filter, bit_size, ret32bit) \
target_ulong helper_##name(target_ulong rs, target_ulong rt,   \
                            CPUMIPSState *env)                 \
{                                                              \
    uint32_t rs_t, rt_t;                                       \
    uint32_t cc;                                               \
    target_ulong dsp;                                          \
    int i;                                                     \
    target_ulong result = 0;                                   \
                                                               \
    dsp = env->active_tc.DSPControl;                           \
    for (i = 0; i < split_num; i++) {                          \
        rs_t = (rs >> (bit_size * i)) & filter;                \
        rt_t = (rt >> (bit_size * i)) & filter;                \
        cc = (dsp >> (24 + i)) & 0x01;                         \
        cc = cc == 1 ? rs_t : rt_t;                            \
                                                               \
        result |= (target_ulong)cc << (bit_size * i);          \
    }                                                          \
                                                               \
    if (ret32bit) {                                            \
        result = (target_long)(int32_t)(result & MIPSDSP_LLO); \
    }                                                          \
                                                               \
    return result;                                             \
}

PICK_INSN(pick_qb, 4, MIPSDSP_Q0, 8, 1);
PICK_INSN(pick_ph, 2, MIPSDSP_LO, 16, 1);

#ifdef TARGET_MIPS64
PICK_INSN(pick_ob, 8, MIPSDSP_Q0, 8, 0);
PICK_INSN(pick_qh, 4, MIPSDSP_LO, 16, 0);
PICK_INSN(pick_pw, 2, MIPSDSP_LLO, 32, 0);
#endif
#undef PICK_INSN

target_ulong helper_packrl_ph(target_ulong rs, target_ulong rt)
{
    uint32_t rsl, rth;

    rsl =  rs & MIPSDSP_LO;
    rth = (rt & MIPSDSP_HI) >> 16;

    return (target_long)(int32_t)((rsl << 16) | rth);
}

#if defined(TARGET_MIPS64)
target_ulong helper_packrl_pw(target_ulong rs, target_ulong rt)
{
    uint32_t rs0, rt1;

    rs0 = rs & MIPSDSP_LLO;
    rt1 = (rt >> 32) & MIPSDSP_LLO;

    return ((uint64_t)rs0 << 32) | (uint64_t)rt1;
}
#endif

/** DSP Accumulator and DSPControl Access Sub-class insns **/
target_ulong helper_extr_w(target_ulong ac, target_ulong shift,
                           CPUMIPSState *env)
{
    int32_t tempI;
    int64_t tempDL[2];

    shift = shift & 0x1F;

    mipsdsp_rndrashift_short_acc(tempDL, ac, shift, env);
    if ((tempDL[1] != 0 || (tempDL[0] & MIPSDSP_LHI) != 0) &&
        (tempDL[1] != 1 || (tempDL[0] & MIPSDSP_LHI) != MIPSDSP_LHI)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    tempI = (tempDL[0] >> 1) & MIPSDSP_LLO;

    tempDL[0] += 1;
    if (tempDL[0] == 0) {
        tempDL[1] += 1;
    }

    if (((tempDL[1] & 0x01) != 0 || (tempDL[0] & MIPSDSP_LHI) != 0) &&
        ((tempDL[1] & 0x01) != 1 || (tempDL[0] & MIPSDSP_LHI) != MIPSDSP_LHI)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (target_long)tempI;
}

target_ulong helper_extr_r_w(target_ulong ac, target_ulong shift,
                             CPUMIPSState *env)
{
    int64_t tempDL[2];

    shift = shift & 0x1F;

    mipsdsp_rndrashift_short_acc(tempDL, ac, shift, env);
    if ((tempDL[1] != 0 || (tempDL[0] & MIPSDSP_LHI) != 0) &&
        (tempDL[1] != 1 || (tempDL[0] & MIPSDSP_LHI) != MIPSDSP_LHI)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    tempDL[0] += 1;
    if (tempDL[0] == 0) {
        tempDL[1] += 1;
    }

    if (((tempDL[1] & 0x01) != 0 || (tempDL[0] & MIPSDSP_LHI) != 0) &&
        ((tempDL[1] & 0x01) != 1 || (tempDL[0] & MIPSDSP_LHI) != MIPSDSP_LHI)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (target_long)(int32_t)(tempDL[0] >> 1);
}

target_ulong helper_extr_rs_w(target_ulong ac, target_ulong shift,
                              CPUMIPSState *env)
{
    int32_t tempI, temp64;
    int64_t tempDL[2];

    shift = shift & 0x1F;

    mipsdsp_rndrashift_short_acc(tempDL, ac, shift, env);
    if ((tempDL[1] != 0 || (tempDL[0] & MIPSDSP_LHI) != 0) &&
        (tempDL[1] != 1 || (tempDL[0] & MIPSDSP_LHI) != MIPSDSP_LHI)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }
    tempDL[0] += 1;
    if (tempDL[0] == 0) {
        tempDL[1] += 1;
    }
    tempI = tempDL[0] >> 1;

    if (((tempDL[1] & 0x01) != 0 || (tempDL[0] & MIPSDSP_LHI) != 0) &&
        ((tempDL[1] & 0x01) != 1 || (tempDL[0] & MIPSDSP_LHI) != MIPSDSP_LHI)) {
        temp64 = tempDL[1] & 0x01;
        if (temp64 == 0) {
            tempI = 0x7FFFFFFF;
        } else {
            tempI = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (target_long)tempI;
}

#if defined(TARGET_MIPS64)
target_ulong helper_dextr_w(target_ulong ac, target_ulong shift,
                            CPUMIPSState *env)
{
    uint64_t temp[3];

    shift = shift & 0x3F;

    mipsdsp_rndrashift_acc(temp, ac, shift, env);

    return (int64_t)(int32_t)(temp[0] >> 1);
}

target_ulong helper_dextr_r_w(target_ulong ac, target_ulong shift,
                              CPUMIPSState *env)
{
    uint64_t temp[3];
    uint32_t temp128;

    shift = shift & 0x3F;
    mipsdsp_rndrashift_acc(temp, ac, shift, env);

    temp[0] += 1;
    if (temp[0] == 0) {
        temp[1] += 1;
        if (temp[1] == 0) {
            temp[2] += 1;
        }
    }

    temp128 = temp[2] & 0x01;

    if ((temp128 != 0 || temp[1] != 0) &&
       (temp128 != 1 || temp[1] != ~0ull)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (int64_t)(int32_t)(temp[0] >> 1);
}

target_ulong helper_dextr_rs_w(target_ulong ac, target_ulong shift,
                               CPUMIPSState *env)
{
    uint64_t temp[3];
    uint32_t temp128;

    shift = shift & 0x3F;
    mipsdsp_rndrashift_acc(temp, ac, shift, env);

    temp[0] += 1;
    if (temp[0] == 0) {
        temp[1] += 1;
        if (temp[1] == 0) {
            temp[2] += 1;
        }
    }

    temp128 = temp[2] & 0x01;

    if ((temp128 != 0 || temp[1] != 0) &&
       (temp128 != 1 || temp[1] != ~0ull)) {
        if (temp128 == 0) {
            temp[0] = 0x0FFFFFFFF;
        } else {
            temp[0] = 0x0100000000ULL;
        }
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (int64_t)(int32_t)(temp[0] >> 1);
}

target_ulong helper_dextr_l(target_ulong ac, target_ulong shift,
                            CPUMIPSState *env)
{
    uint64_t temp[3];
    target_ulong ret;

    shift = shift & 0x3F;

    mipsdsp_rndrashift_acc(temp, ac, shift, env);

    ret = (temp[1] << 63) | (temp[0] >> 1);

    return ret;
}

target_ulong helper_dextr_r_l(target_ulong ac, target_ulong shift,
                              CPUMIPSState *env)
{
    uint64_t temp[3];
    uint32_t temp128;
    target_ulong ret;

    shift = shift & 0x3F;
    mipsdsp_rndrashift_acc(temp, ac, shift, env);

    temp[0] += 1;
    if (temp[0] == 0) {
        temp[1] += 1;
        if (temp[1] == 0) {
            temp[2] += 1;
        }
    }

    temp128 = temp[2] & 0x01;

    if ((temp128 != 0 || temp[1] != 0) &&
       (temp128 != 1 || temp[1] != ~0ull)) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    ret = (temp[1] << 63) | (temp[0] >> 1);

    return ret;
}

target_ulong helper_dextr_rs_l(target_ulong ac, target_ulong shift,
                               CPUMIPSState *env)
{
    uint64_t temp[3];
    uint32_t temp128;
    target_ulong ret;

    shift = shift & 0x3F;
    mipsdsp_rndrashift_acc(temp, ac, shift, env);

    temp[0] += 1;
    if (temp[0] == 0) {
        temp[1] += 1;
        if (temp[1] == 0) {
            temp[2] += 1;
        }
    }

    temp128 = temp[2] & 0x01;

    if ((temp128 != 0 || temp[1] != 0) &&
       (temp128 != 1 || temp[1] != ~0ull)) {
        if (temp128 == 0) {
            temp[1] &= ~0x00ull - 1;
            temp[0] |= ~0x00ull - 1;
        } else {
            temp[1] |= 0x01;
            temp[0] &= 0x01;
        }
        set_DSPControl_overflow_flag(1, 23, env);
    }

    ret = (temp[1] << 63) | (temp[0] >> 1);

    return ret;
}
#endif

target_ulong helper_extr_s_h(target_ulong ac, target_ulong shift,
                             CPUMIPSState *env)
{
    int64_t temp, acc;

    shift = shift & 0x1F;

    acc = ((int64_t)env->active_tc.HI[ac] << 32) |
          ((int64_t)env->active_tc.LO[ac] & 0xFFFFFFFF);

    temp = acc >> shift;

    if (temp > (int64_t)0x7FFF) {
        temp = 0x00007FFF;
        set_DSPControl_overflow_flag(1, 23, env);
    } else if (temp < (int64_t)0xFFFFFFFFFFFF8000ULL) {
        temp = 0xFFFF8000;
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (target_long)(int32_t)(temp & 0xFFFFFFFF);
}


#if defined(TARGET_MIPS64)
target_ulong helper_dextr_s_h(target_ulong ac, target_ulong shift,
                              CPUMIPSState *env)
{
    int64_t temp[2];
    uint32_t temp127;

    shift = shift & 0x1F;

    mipsdsp_rashift_acc((uint64_t *)temp, ac, shift, env);

    temp127 = (temp[1] >> 63) & 0x01;

    if ((temp127 == 0) && (temp[1] > 0 || temp[0] > 32767)) {
        temp[0] &= 0xFFFF0000;
        temp[0] |= 0x00007FFF;
        set_DSPControl_overflow_flag(1, 23, env);
    } else if ((temp127 == 1) &&
            (temp[1] < 0xFFFFFFFFFFFFFFFFll
             || temp[0] < 0xFFFFFFFFFFFF1000ll)) {
        temp[0] &= 0xFFFF0000;
        temp[0] |= 0x00008000;
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return (int64_t)(int16_t)(temp[0] & MIPSDSP_LO);
}

#endif

target_ulong helper_extp(target_ulong ac, target_ulong size, CPUMIPSState *env)
{
    int32_t start_pos;
    int sub;
    uint32_t temp;
    uint64_t acc;

    size = size & 0x1F;

    temp = 0;
    start_pos = get_DSPControl_pos(env);
    sub = start_pos - (size + 1);
    if (sub >= -1) {
        acc = ((uint64_t)env->active_tc.HI[ac] << 32) |
              ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);
        temp = (acc >> (start_pos - size)) & (~0U >> (31 - size));
        set_DSPControl_efi(0, env);
    } else {
        set_DSPControl_efi(1, env);
    }

    return (target_ulong)temp;
}

target_ulong helper_extpdp(target_ulong ac, target_ulong size,
                           CPUMIPSState *env)
{
    int32_t start_pos;
    int sub;
    uint32_t temp;
    uint64_t acc;

    size = size & 0x1F;
    temp = 0;
    start_pos = get_DSPControl_pos(env);
    sub = start_pos - (size + 1);
    if (sub >= -1) {
        acc  = ((uint64_t)env->active_tc.HI[ac] << 32) |
               ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);
        temp = extract64(acc, start_pos - size, size + 1);

        set_DSPControl_pos(sub, env);
        set_DSPControl_efi(0, env);
    } else {
        set_DSPControl_efi(1, env);
    }

    return (target_ulong)temp;
}


#if defined(TARGET_MIPS64)
target_ulong helper_dextp(target_ulong ac, target_ulong size, CPUMIPSState *env)
{
    int start_pos;
    int len;
    int sub;
    uint64_t tempB, tempA;
    uint64_t temp;

    temp = 0;

    size = size & 0x3F;
    start_pos = get_DSPControl_pos(env);
    len = start_pos - size;
    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];

    sub = start_pos - (size + 1);

    if (sub >= -1) {
        temp = (tempB << (64 - len)) | (tempA >> len);
        temp = temp & ((1ULL << (size + 1)) - 1);
        set_DSPControl_efi(0, env);
    } else {
        set_DSPControl_efi(1, env);
    }

    return temp;
}

target_ulong helper_dextpdp(target_ulong ac, target_ulong size,
                            CPUMIPSState *env)
{
    int start_pos;
    int len;
    int sub;
    uint64_t tempB, tempA;
    uint64_t temp;

    temp = 0;
    size = size & 0x3F;
    start_pos = get_DSPControl_pos(env);
    len = start_pos - size;
    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];

    sub = start_pos - (size + 1);

    if (sub >= -1) {
        temp = (tempB << (64 - len)) | (tempA >> len);
        temp = temp & ((1ULL << (size + 1)) - 1);
        set_DSPControl_pos(sub, env);
        set_DSPControl_efi(0, env);
    } else {
        set_DSPControl_efi(1, env);
    }

    return temp;
}

#endif

void helper_shilo(target_ulong ac, target_ulong rs, CPUMIPSState *env)
{
    int8_t  rs5_0;
    uint64_t temp, acc;

    rs5_0 = rs & 0x3F;
    rs5_0 = (int8_t)(rs5_0 << 2) >> 2;

    if (unlikely(rs5_0 == 0)) {
        return;
    }

    acc   = (((uint64_t)env->active_tc.HI[ac] << 32) & MIPSDSP_LHI) |
            ((uint64_t)env->active_tc.LO[ac] & MIPSDSP_LLO);

    if (rs5_0 > 0) {
        temp = acc >> rs5_0;
    } else {
        temp = acc << -rs5_0;
    }

    env->active_tc.HI[ac] = (target_ulong)(int32_t)((temp & MIPSDSP_LHI) >> 32);
    env->active_tc.LO[ac] = (target_ulong)(int32_t)(temp & MIPSDSP_LLO);
}

#if defined(TARGET_MIPS64)
void helper_dshilo(target_ulong shift, target_ulong ac, CPUMIPSState *env)
{
    int8_t shift_t;
    uint64_t tempB, tempA;

    shift_t = (int8_t)(shift << 1) >> 1;

    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];

    if (shift_t != 0) {
        if (shift_t >= 0) {
            tempA = (tempB << (64 - shift_t)) | (tempA >> shift_t);
            tempB = tempB >> shift_t;
        } else {
            shift_t = -shift_t;
            tempB = (tempB << shift_t) | (tempA >> (64 - shift_t));
            tempA = tempA << shift_t;
        }
    }

    env->active_tc.HI[ac] = tempB;
    env->active_tc.LO[ac] = tempA;
}

#endif
void helper_mthlip(target_ulong ac, target_ulong rs, CPUMIPSState *env)
{
    int32_t tempA, tempB, pos;

    tempA = rs;
    tempB = env->active_tc.LO[ac];
    env->active_tc.HI[ac] = (target_long)tempB;
    env->active_tc.LO[ac] = (target_long)tempA;
    pos = get_DSPControl_pos(env);

    if (pos > 32) {
        return;
    } else {
        set_DSPControl_pos(pos + 32, env);
    }
}

#if defined(TARGET_MIPS64)
void helper_dmthlip(target_ulong rs, target_ulong ac, CPUMIPSState *env)
{
    uint8_t ac_t;
    uint8_t pos;
    uint64_t tempB, tempA;

    ac_t = ac & 0x3;

    tempA = rs;
    tempB = env->active_tc.LO[ac_t];

    env->active_tc.HI[ac_t] = tempB;
    env->active_tc.LO[ac_t] = tempA;

    pos = get_DSPControl_pos(env);

    if (pos <= 64) {
        pos = pos + 64;
        set_DSPControl_pos(pos, env);
    }
}
#endif

void cpu_wrdsp(uint32_t rs, uint32_t mask_num, CPUMIPSState *env)
{
    uint8_t  mask[6];
    uint8_t  i;
    uint32_t newbits, overwrite;
    target_ulong dsp;

    newbits   = 0x00;
    overwrite = 0xFFFFFFFF;
    dsp = env->active_tc.DSPControl;

    for (i = 0; i < 6; i++) {
        mask[i] = (mask_num >> i) & 0x01;
    }

    if (mask[0] == 1) {
#if defined(TARGET_MIPS64)
        overwrite &= 0xFFFFFF80;
        newbits   &= 0xFFFFFF80;
        newbits   |= 0x0000007F & rs;
#else
        overwrite &= 0xFFFFFFC0;
        newbits   &= 0xFFFFFFC0;
        newbits   |= 0x0000003F & rs;
#endif
    }

    if (mask[1] == 1) {
        overwrite &= 0xFFFFE07F;
        newbits   &= 0xFFFFE07F;
        newbits   |= 0x00001F80 & rs;
    }

    if (mask[2] == 1) {
        overwrite &= 0xFFFFDFFF;
        newbits   &= 0xFFFFDFFF;
        newbits   |= 0x00002000 & rs;
    }

    if (mask[3] == 1) {
        overwrite &= 0xFF00FFFF;
        newbits   &= 0xFF00FFFF;
        newbits   |= 0x00FF0000 & rs;
    }

    if (mask[4] == 1) {
        overwrite &= 0x00FFFFFF;
        newbits   &= 0x00FFFFFF;
#if defined(TARGET_MIPS64)
        newbits   |= 0xFF000000 & rs;
#else
        newbits   |= 0x0F000000 & rs;
#endif
    }

    if (mask[5] == 1) {
        overwrite &= 0xFFFFBFFF;
        newbits   &= 0xFFFFBFFF;
        newbits   |= 0x00004000 & rs;
    }

    dsp = dsp & overwrite;
    dsp = dsp | newbits;
    env->active_tc.DSPControl = dsp;
}

void helper_wrdsp(target_ulong rs, target_ulong mask_num, CPUMIPSState *env)
{
    cpu_wrdsp(rs, mask_num, env);
}

uint32_t cpu_rddsp(uint32_t mask_num, CPUMIPSState *env)
{
    uint8_t  mask[6];
    uint32_t ruler, i;
    target_ulong temp;
    target_ulong dsp;

    ruler = 0x01;
    for (i = 0; i < 6; i++) {
        mask[i] = (mask_num & ruler) >> i ;
        ruler = ruler << 1;
    }

    temp  = 0x00;
    dsp = env->active_tc.DSPControl;

    if (mask[0] == 1) {
#if defined(TARGET_MIPS64)
        temp |= dsp & 0x7F;
#else
        temp |= dsp & 0x3F;
#endif
    }

    if (mask[1] == 1) {
        temp |= dsp & 0x1F80;
    }

    if (mask[2] == 1) {
        temp |= dsp & 0x2000;
    }

    if (mask[3] == 1) {
        temp |= dsp & 0x00FF0000;
    }

    if (mask[4] == 1) {
#if defined(TARGET_MIPS64)
        temp |= dsp & 0xFF000000;
#else
        temp |= dsp & 0x0F000000;
#endif
    }

    if (mask[5] == 1) {
        temp |= dsp & 0x4000;
    }

    return temp;
}

target_ulong helper_rddsp(target_ulong mask_num, CPUMIPSState *env)
{
    return cpu_rddsp(mask_num, env);
}


#undef MIPSDSP_LHI
#undef MIPSDSP_LLO
#undef MIPSDSP_HI
#undef MIPSDSP_LO
#undef MIPSDSP_Q3
#undef MIPSDSP_Q2
#undef MIPSDSP_Q1
#undef MIPSDSP_Q0

#undef MIPSDSP_SPLIT32_8
#undef MIPSDSP_SPLIT32_16

#undef MIPSDSP_RETURN32_8
#undef MIPSDSP_RETURN32_16

#ifdef TARGET_MIPS64
#undef MIPSDSP_SPLIT64_16
#undef MIPSDSP_SPLIT64_32
#undef MIPSDSP_RETURN64_16
#undef MIPSDSP_RETURN64_32
#endif
