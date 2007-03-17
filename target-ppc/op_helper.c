/*
 *  PowerPC emulation helpers for qemu.
 * 
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "exec.h"

#define MEMSUFFIX _raw
#include "op_helper_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_helper_mem.h"
#define MEMSUFFIX _kernel
#include "op_helper_mem.h"
#endif

//#define DEBUG_OP
//#define DEBUG_EXCEPTIONS
//#define DEBUG_SOFTWARE_TLB
//#define FLUSH_ALL_TLBS

/*****************************************************************************/
/* Exceptions processing helpers */
void cpu_loop_exit (void)
{
    longjmp(env->jmp_env, 1);
}

void do_raise_exception_err (uint32_t exception, int error_code)
{
#if 0
    printf("Raise exception %3x code : %d\n", exception, error_code);
#endif
    switch (exception) {
    case EXCP_PROGRAM:
        if (error_code == EXCP_FP && msr_fe0 == 0 && msr_fe1 == 0)
            return;
        break;
    default:
        break;
    }
    env->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit();
}

void do_raise_exception (uint32_t exception)
{
    do_raise_exception_err(exception, 0);
}

/*****************************************************************************/
/* Registers load and stores */
void do_load_cr (void)
{
    T0 = (env->crf[0] << 28) |
        (env->crf[1] << 24) |
        (env->crf[2] << 20) |
        (env->crf[3] << 16) |
        (env->crf[4] << 12) |
        (env->crf[5] << 8) |
        (env->crf[6] << 4) |
        (env->crf[7] << 0);
}

void do_store_cr (uint32_t mask)
{
    int i, sh;

    for (i = 0, sh = 7; i < 8; i++, sh --) {
        if (mask & (1 << sh))
            env->crf[i] = (T0 >> (sh * 4)) & 0xFUL;
    }
}

void do_load_xer (void)
{
    T0 = (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC) |
        (xer_cmp << XER_CMP);
}

void do_store_xer (void)
{
    xer_so = (T0 >> XER_SO) & 0x01;
    xer_ov = (T0 >> XER_OV) & 0x01;
    xer_ca = (T0 >> XER_CA) & 0x01;
    xer_cmp = (T0 >> XER_CMP) & 0xFF;
    xer_bc = (T0 >> XER_BC) & 0x7F;
}

void do_load_fpscr (void)
{
    /* The 32 MSB of the target fpr are undefined.
     * They'll be zero...
     */
    union {
        float64 d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i;

#if defined(WORDS_BIGENDIAN)
#define WORD0 0
#define WORD1 1
#else
#define WORD0 1
#define WORD1 0
#endif
    u.s.u[WORD0] = 0;
    u.s.u[WORD1] = 0;
    for (i = 0; i < 8; i++)
        u.s.u[WORD1] |= env->fpscr[i] << (4 * i);
    FT0 = u.d;
}

void do_store_fpscr (uint32_t mask)
{
    /*
     * We use only the 32 LSB of the incoming fpr
     */
    union {
        double d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i, rnd_type;

    u.d = FT0;
    if (mask & 0x80)
        env->fpscr[0] = (env->fpscr[0] & 0x9) | ((u.s.u[WORD1] >> 28) & ~0x9);
    for (i = 1; i < 7; i++) {
        if (mask & (1 << (7 - i)))
            env->fpscr[i] = (u.s.u[WORD1] >> (4 * (7 - i))) & 0xF;
    }
    /* TODO: update FEX & VX */
    /* Set rounding mode */
    switch (env->fpscr[0] & 0x3) {
    case 0:
        /* Best approximation (round to nearest) */
        rnd_type = float_round_nearest_even;
        break;
    case 1:
        /* Smaller magnitude (round toward zero) */
        rnd_type = float_round_to_zero;
        break;
    case 2:
        /* Round toward +infinite */
        rnd_type = float_round_up;
        break;
    default:
    case 3:
        /* Round toward -infinite */
        rnd_type = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
}

/*****************************************************************************/
/* Fixed point operations helpers */
#if defined(TARGET_PPC64)
static void add128 (uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    *plow += a;
    /* carry test */
    if (*plow < a)
        (*phigh)++;
    *phigh += b;
}

static void neg128 (uint64_t *plow, uint64_t *phigh)
{
    *plow = ~ *plow;
    *phigh = ~ *phigh;
    add128(plow, phigh, 1, 0);
}

static void mul64 (uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    uint32_t a0, a1, b0, b1;
    uint64_t v;

    a0 = a;
    a1 = a >> 32;

    b0 = b;
    b1 = b >> 32;
    
    v = (uint64_t)a0 * (uint64_t)b0;
    *plow = v;
    *phigh = 0;

    v = (uint64_t)a0 * (uint64_t)b1;
    add128(plow, phigh, v << 32, v >> 32);

    v = (uint64_t)a1 * (uint64_t)b0;
    add128(plow, phigh, v << 32, v >> 32);

    v = (uint64_t)a1 * (uint64_t)b1;
    *phigh += v;
#if defined(DEBUG_MULDIV)
    printf("mul: 0x%016llx * 0x%016llx = 0x%016llx%016llx\n",
           a, b, *phigh, *plow);
#endif
}

void do_mul64 (uint64_t *plow, uint64_t *phigh)
{
    mul64(plow, phigh, T0, T1);
}

static void imul64(uint64_t *plow, uint64_t *phigh, int64_t a, int64_t b)
{
    int sa, sb;
    sa = (a < 0);
    if (sa)
        a = -a;
    sb = (b < 0);
    if (sb)
        b = -b;
    mul64(plow, phigh, a, b);
    if (sa ^ sb) {
        neg128(plow, phigh);
    }
}

void do_imul64 (uint64_t *plow, uint64_t *phigh)
{
    imul64(plow, phigh, T0, T1);
}
#endif

void do_adde (void)
{
    T2 = T0;
    T0 += T1 + xer_ca;
    if (likely(!((uint32_t)T0 < (uint32_t)T2 ||
                 (xer_ca == 1 && (uint32_t)T0 == (uint32_t)T2)))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

#if defined(TARGET_PPC64)
void do_adde_64 (void)
{
    T2 = T0;
    T0 += T1 + xer_ca;
    if (likely(!((uint64_t)T0 < (uint64_t)T2 ||
                 (xer_ca == 1 && (uint64_t)T0 == (uint64_t)T2)))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}
#endif

void do_addmeo (void)
{
    T1 = T0;
    T0 += xer_ca + (-1);
    if (likely(!((uint32_t)T1 &
                 ((uint32_t)T1 ^ (uint32_t)T0) & (1UL << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely(T1 != 0))
        xer_ca = 1;
}

#if defined(TARGET_PPC64)
void do_addmeo_64 (void)
{
    T1 = T0;
    T0 += xer_ca + (-1);
    if (likely(!((uint64_t)T1 &
                 ((uint64_t)T1 ^ (uint64_t)T0) & (1ULL << 63)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely(T1 != 0))
        xer_ca = 1;
}
#endif

void do_divwo (void)
{
    if (likely(!(((int32_t)T0 == INT32_MIN && (int32_t)T1 == -1) ||
                 (int32_t)T1 == 0))) {
        xer_ov = 0;
        T0 = (int32_t)T0 / (int32_t)T1;
    } else {
        xer_so = 1;
        xer_ov = 1;
        T0 = (-1) * ((uint32_t)T0 >> 31);
    }
}

#if defined(TARGET_PPC64)
void do_divdo (void)
{
    if (likely(!(((int64_t)T0 == INT64_MIN && (int64_t)T1 == -1ULL) ||
                 (int64_t)T1 == 0))) {
        xer_ov = 0;
        T0 = (int64_t)T0 / (int64_t)T1;
    } else {
        xer_so = 1;
        xer_ov = 1;
        T0 = (-1ULL) * ((uint64_t)T0 >> 63);
    }
}
#endif

void do_divwuo (void)
{
    if (likely((uint32_t)T1 != 0)) {
        xer_ov = 0;
        T0 = (uint32_t)T0 / (uint32_t)T1;
    } else {
        xer_so = 1;
        xer_ov = 1;
        T0 = 0;
    }
}

#if defined(TARGET_PPC64)
void do_divduo (void)
{
    if (likely((uint64_t)T1 != 0)) {
        xer_ov = 0;
        T0 = (uint64_t)T0 / (uint64_t)T1;
    } else {
        xer_so = 1;
        xer_ov = 1;
        T0 = 0;
    }
}
#endif

void do_mullwo (void)
{
    int64_t res = (int64_t)T0 * (int64_t)T1;

    if (likely((int32_t)res == res)) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    T0 = (int32_t)res;
}

#if defined(TARGET_PPC64)
void do_mulldo (void)
{
    int64_t th;
    uint64_t tl;

    do_imul64(&tl, &th);
    if (likely(th == 0)) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    T0 = (int64_t)tl;
}
#endif

void do_nego (void)
{
    if (likely((int32_t)T0 != INT32_MIN)) {
        xer_ov = 0;
        T0 = -(int32_t)T0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
}

#if defined(TARGET_PPC64)
void do_nego_64 (void)
{
    if (likely((int64_t)T0 != INT64_MIN)) {
        xer_ov = 0;
        T0 = -(int64_t)T0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
}
#endif

void do_subfe (void)
{
    T0 = T1 + ~T0 + xer_ca;
    if (likely((uint32_t)T0 >= (uint32_t)T1 &&
               (xer_ca == 0 || (uint32_t)T0 != (uint32_t)T1))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

#if defined(TARGET_PPC64)
void do_subfe_64 (void)
{
    T0 = T1 + ~T0 + xer_ca;
    if (likely((uint64_t)T0 >= (uint64_t)T1 &&
               (xer_ca == 0 || (uint64_t)T0 != (uint64_t)T1))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}
#endif

void do_subfmeo (void)
{
    T1 = T0;
    T0 = ~T0 + xer_ca - 1;
    if (likely(!((uint32_t)~T1 & ((uint32_t)~T1 ^ (uint32_t)T0) &
                 (1UL << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely((uint32_t)T1 != UINT32_MAX))
        xer_ca = 1;
}

#if defined(TARGET_PPC64)
void do_subfmeo_64 (void)
{
    T1 = T0;
    T0 = ~T0 + xer_ca - 1;
    if (likely(!((uint64_t)~T1 & ((uint64_t)~T1 ^ (uint64_t)T0) &
                 (1ULL << 63)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely((uint64_t)T1 != UINT64_MAX))
        xer_ca = 1;
}
#endif

void do_subfzeo (void)
{
    T1 = T0;
    T0 = ~T0 + xer_ca;
    if (likely(!(((uint32_t)~T1 ^ UINT32_MAX) &
                 ((uint32_t)(~T1) ^ (uint32_t)T0) & (1UL << 31)))) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    if (likely((uint32_t)T0 >= (uint32_t)~T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

#if defined(TARGET_PPC64)
void do_subfzeo_64 (void)
{
    T1 = T0;
    T0 = ~T0 + xer_ca;
    if (likely(!(((uint64_t)~T1 ^ UINT64_MAX) &
                 ((uint64_t)(~T1) ^ (uint64_t)T0) & (1ULL << 63)))) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    if (likely((uint64_t)T0 >= (uint64_t)~T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}
#endif

/* shift right arithmetic helper */
void do_sraw (void)
{
    int32_t ret;

    if (likely(!(T1 & 0x20UL))) {
        if (likely((uint32_t)T1 != 0)) {
            ret = (int32_t)T0 >> (T1 & 0x1fUL);
            if (likely(ret >= 0 || ((int32_t)T0 & ((1 << T1) - 1)) == 0)) {
                xer_ca = 0;
            } else {
                xer_ca = 1;
            }
        } else {
            ret = T0;
            xer_ca = 0;
        }
    } else {
        ret = (-1) * ((uint32_t)T0 >> 31);
        if (likely(ret >= 0 || ((uint32_t)T0 & ~0x80000000UL) == 0)) {
            xer_ca = 0;
        } else {
            xer_ca = 1;
        }
    }
    T0 = ret;
}

#if defined(TARGET_PPC64)
void do_srad (void)
{
    int64_t ret;

    if (likely(!(T1 & 0x40UL))) {
        if (likely((uint64_t)T1 != 0)) {
            ret = (int64_t)T0 >> (T1 & 0x3FUL);
            if (likely(ret >= 0 || ((int64_t)T0 & ((1 << T1) - 1)) == 0)) {
                xer_ca = 0;
            } else {
                xer_ca = 1;
            }
        } else {
            ret = T0;
            xer_ca = 0;
        }
    } else {
        ret = (-1) * ((uint64_t)T0 >> 63);
        if (likely(ret >= 0 || ((uint64_t)T0 & ~0x8000000000000000ULL) == 0)) {
            xer_ca = 0;
        } else {
            xer_ca = 1;
        }
    }
    T0 = ret;
}
#endif

static inline int popcnt (uint32_t val)
{
    int i;

    for (i = 0; val != 0;)
        val = val ^ (val - 1);

    return i;
}

void do_popcntb (void)
{
    uint32_t ret;
    int i;

    ret = 0;
    for (i = 0; i < 32; i += 8)
        ret |= popcnt((T0 >> i) & 0xFF) << i;
    T0 = ret;
}

#if defined(TARGET_PPC64)
void do_popcntb_64 (void)
{
    uint64_t ret;
    int i;

    ret = 0;
    for (i = 0; i < 64; i += 8)
        ret |= popcnt((T0 >> i) & 0xFF) << i;
    T0 = ret;
}
#endif

/*****************************************************************************/
/* Floating point operations helpers */
void do_fctiw (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    /* XXX: higher bits are not supposed to be significant.
     *     to make tests easier, return the same as a real PowerPC 750 (aka G3)
     */
    p.i = float64_to_int32(FT0, &env->fp_status);
    p.i |= 0xFFF80000ULL << 32;
    FT0 = p.d;
}

void do_fctiwz (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    /* XXX: higher bits are not supposed to be significant.
     *     to make tests easier, return the same as a real PowerPC 750 (aka G3)
     */
    p.i = float64_to_int32_round_to_zero(FT0, &env->fp_status);
    p.i |= 0xFFF80000ULL << 32;
    FT0 = p.d;
}

void do_fnmadd (void)
{
    FT0 = float64_mul(FT0, FT1, &env->fp_status);
    FT0 = float64_add(FT0, FT2, &env->fp_status);
    if (likely(!isnan(FT0)))
        FT0 = float64_chs(FT0);
}

void do_fnmsub (void)
{
    FT0 = float64_mul(FT0, FT1, &env->fp_status);
    FT0 = float64_sub(FT0, FT2, &env->fp_status);
    if (likely(!isnan(FT0)))
        FT0 = float64_chs(FT0);
}

void do_fsqrt (void)
{
    FT0 = float64_sqrt(FT0, &env->fp_status);
}

void do_fres (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    if (likely(isnormal(FT0))) {
        FT0 = float32_div(1.0, FT0, &env->fp_status);
    } else {
        p.d = FT0;
        if (p.i == 0x8000000000000000ULL) {
            p.i = 0xFFF0000000000000ULL;
        } else if (p.i == 0x0000000000000000ULL) {
            p.i = 0x7FF0000000000000ULL;
        } else if (isnan(FT0)) {
            p.i = 0x7FF8000000000000ULL;
        } else if (FT0 < 0.0) {
            p.i = 0x8000000000000000ULL;
        } else {
            p.i = 0x0000000000000000ULL;
        }
        FT0 = p.d;
    }
}

void do_frsqrte (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    if (likely(isnormal(FT0) && FT0 > 0.0)) {
        FT0 = float64_sqrt(FT0, &env->fp_status);
        FT0 = float32_div(1.0, FT0, &env->fp_status);
    } else {
        p.d = FT0;
        if (p.i == 0x8000000000000000ULL) {
            p.i = 0xFFF0000000000000ULL;
        } else if (p.i == 0x0000000000000000ULL) {
            p.i = 0x7FF0000000000000ULL;
        } else if (isnan(FT0)) {
            if (!(p.i & 0x0008000000000000ULL))
                p.i |= 0x000FFFFFFFFFFFFFULL;
        } else if (FT0 < 0) {
            p.i = 0x7FF8000000000000ULL;
        } else {
            p.i = 0x0000000000000000ULL;
        }
        FT0 = p.d;
    }
}

void do_fsel (void)
{
    if (FT0 >= 0)
        FT0 = FT1;
    else
        FT0 = FT2;
}

void do_fcmpu (void)
{
    if (likely(!isnan(FT0) && !isnan(FT1))) {
        if (float64_lt(FT0, FT1, &env->fp_status)) {
            T0 = 0x08UL;
        } else if (!float64_le(FT0, FT1, &env->fp_status)) {
            T0 = 0x04UL;
        } else {
            T0 = 0x02UL;
        }
    } else {
        T0 = 0x01UL;
        env->fpscr[4] |= 0x1;
        env->fpscr[6] |= 0x1;
    }
    env->fpscr[3] = T0;
}

void do_fcmpo (void)
{
    env->fpscr[4] &= ~0x1;
    if (likely(!isnan(FT0) && !isnan(FT1))) {
        if (float64_lt(FT0, FT1, &env->fp_status)) {
            T0 = 0x08UL;
        } else if (!float64_le(FT0, FT1, &env->fp_status)) {
            T0 = 0x04UL;
        } else {
            T0 = 0x02UL;
        }
    } else {
        T0 = 0x01UL;
        env->fpscr[4] |= 0x1;
        if (!float64_is_signaling_nan(FT0) || !float64_is_signaling_nan(FT1)) {
            /* Quiet NaN case */
            env->fpscr[6] |= 0x1;
            if (!(env->fpscr[1] & 0x8))
                env->fpscr[4] |= 0x8;
        } else {
            env->fpscr[4] |= 0x8;
        }
    }
    env->fpscr[3] = T0;
}

#if !defined (CONFIG_USER_ONLY)
void do_rfi (void)
{
    env->nip = (target_ulong)(env->spr[SPR_SRR0] & ~0x00000003);
    T0 = (target_ulong)(env->spr[SPR_SRR1] & ~0xFFFF0000UL);
    do_store_msr(env, T0);
#if defined (DEBUG_OP)
    dump_rfi();
#endif
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

#if defined(TARGET_PPC64)
void do_rfi_32 (void)
{
    env->nip = (uint32_t)(env->spr[SPR_SRR0] & ~0x00000003);
    T0 = (uint32_t)(env->spr[SPR_SRR1] & ~0xFFFF0000UL);
    do_store_msr(env, T0);
#if defined (DEBUG_OP)
    dump_rfi();
#endif
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}
#endif
#endif

void do_tw (int flags)
{
    if (!likely(!(((int32_t)T0 < (int32_t)T1 && (flags & 0x10)) ||
                  ((int32_t)T0 > (int32_t)T1 && (flags & 0x08)) ||
                  ((int32_t)T0 == (int32_t)T1 && (flags & 0x04)) ||
                  ((uint32_t)T0 < (uint32_t)T1 && (flags & 0x02)) ||
                  ((uint32_t)T0 > (uint32_t)T1 && (flags & 0x01)))))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_TRAP);
}

#if defined(TARGET_PPC64)
void do_td (int flags)
{
    if (!likely(!(((int64_t)T0 < (int64_t)T1 && (flags & 0x10)) ||
                  ((int64_t)T0 > (int64_t)T1 && (flags & 0x08)) ||
                  ((int64_t)T0 == (int64_t)T1 && (flags & 0x04)) ||
                  ((uint64_t)T0 < (uint64_t)T1 && (flags & 0x02)) ||
                  ((uint64_t)T0 > (uint64_t)T1 && (flags & 0x01)))))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_TRAP);
}
#endif

/* Instruction cache invalidation helper */
void do_icbi (void)
{
    uint32_t tmp;
    /* Invalidate one cache line :
     * PowerPC specification says this is to be treated like a load
     * (not a fetch) by the MMU. To be sure it will be so,
     * do the load "by hand".
     */
    tmp = ldl_kernel((uint32_t)T0);
    T0 &= ~(ICACHE_LINE_SIZE - 1);
    tb_invalidate_page_range((uint32_t)T0, (uint32_t)(T0 + ICACHE_LINE_SIZE));
}

#if defined(TARGET_PPC64)
void do_icbi_64 (void)
{
    uint64_t tmp;
    /* Invalidate one cache line :
     * PowerPC specification says this is to be treated like a load
     * (not a fetch) by the MMU. To be sure it will be so,
     * do the load "by hand".
     */
    tmp = ldq_kernel((uint64_t)T0);
    T0 &= ~(ICACHE_LINE_SIZE - 1);
    tb_invalidate_page_range((uint64_t)T0, (uint64_t)(T0 + ICACHE_LINE_SIZE));
}
#endif

/*****************************************************************************/
/* PowerPC 601 specific instructions (POWER bridge) */
void do_POWER_abso (void)
{
    if ((uint32_t)T0 == INT32_MIN) {
        T0 = INT32_MAX;
        xer_ov = 1;
        xer_so = 1;
    } else {
        T0 = -T0;
        xer_ov = 0;
    }
}

void do_POWER_clcs (void)
{
    switch (T0) {
    case 0x0CUL:
        /* Instruction cache line size */
        T0 = ICACHE_LINE_SIZE;
        break;
    case 0x0DUL:
        /* Data cache line size */
        T0 = DCACHE_LINE_SIZE;
        break;
    case 0x0EUL:
        /* Minimum cache line size */
        T0 = ICACHE_LINE_SIZE < DCACHE_LINE_SIZE ?
            ICACHE_LINE_SIZE : DCACHE_LINE_SIZE;
        break;
    case 0x0FUL:
        /* Maximum cache line size */
        T0 = ICACHE_LINE_SIZE > DCACHE_LINE_SIZE ?
            ICACHE_LINE_SIZE : DCACHE_LINE_SIZE;
        break;
    default:
        /* Undefined */
        break;
    }
}

void do_POWER_div (void)
{
    uint64_t tmp;

    if (((int32_t)T0 == INT32_MIN && (int32_t)T1 == -1) || (int32_t)T1 == 0) {
        T0 = (long)((-1) * (T0 >> 31));
        env->spr[SPR_MQ] = 0;
    } else {
        tmp = ((uint64_t)T0 << 32) | env->spr[SPR_MQ];
        env->spr[SPR_MQ] = tmp % T1;
        T0 = tmp / (int32_t)T1;
    }
}

void do_POWER_divo (void)
{
    int64_t tmp;

    if (((int32_t)T0 == INT32_MIN && (int32_t)T1 == -1) || (int32_t)T1 == 0) {
        T0 = (long)((-1) * (T0 >> 31));
        env->spr[SPR_MQ] = 0;
        xer_ov = 1;
        xer_so = 1;
    } else {
        tmp = ((uint64_t)T0 << 32) | env->spr[SPR_MQ];
        env->spr[SPR_MQ] = tmp % T1;
        tmp /= (int32_t)T1;
        if (tmp > (int64_t)INT32_MAX || tmp < (int64_t)INT32_MIN) {
            xer_ov = 1;
            xer_so = 1;
        } else {
            xer_ov = 0;
        }
        T0 = tmp;
    }
}

void do_POWER_divs (void)
{
    if (((int32_t)T0 == INT32_MIN && (int32_t)T1 == -1) || (int32_t)T1 == 0) {
        T0 = (long)((-1) * (T0 >> 31));
        env->spr[SPR_MQ] = 0;
    } else {
        env->spr[SPR_MQ] = T0 % T1;
        T0 = (int32_t)T0 / (int32_t)T1;
    }
}

void do_POWER_divso (void)
{
    if (((int32_t)T0 == INT32_MIN && (int32_t)T1 == -1) || (int32_t)T1 == 0) {
        T0 = (long)((-1) * (T0 >> 31));
        env->spr[SPR_MQ] = 0;
        xer_ov = 1;
        xer_so = 1;
    } else {
        T0 = (int32_t)T0 / (int32_t)T1;
        env->spr[SPR_MQ] = (int32_t)T0 % (int32_t)T1;
        xer_ov = 0;
    }
}

void do_POWER_dozo (void)
{
    if ((int32_t)T1 > (int32_t)T0) {
        T2 = T0;
        T0 = T1 - T0;
        if (((uint32_t)(~T2) ^ (uint32_t)T1 ^ UINT32_MAX) &
            ((uint32_t)(~T2) ^ (uint32_t)T0) & (1UL << 31)) {
            xer_so = 1;
            xer_ov = 1;
        } else {
            xer_ov = 0;
        }
    } else {
        T0 = 0;
        xer_ov = 0;
    }
}

void do_POWER_maskg (void)
{
    uint32_t ret;

    if ((uint32_t)T0 == (uint32_t)(T1 + 1)) {
        ret = -1;
    } else {
        ret = (((uint32_t)(-1)) >> ((uint32_t)T0)) ^
            (((uint32_t)(-1) >> ((uint32_t)T1)) >> 1);
        if ((uint32_t)T0 > (uint32_t)T1)
            ret = ~ret;
    }
    T0 = ret;
}

void do_POWER_mulo (void)
{
    uint64_t tmp;

    tmp = (uint64_t)T0 * (uint64_t)T1;
    env->spr[SPR_MQ] = tmp >> 32;
    T0 = tmp;
    if (tmp >> 32 != ((uint64_t)T0 >> 16) * ((uint64_t)T1 >> 16)) {
        xer_ov = 1;
        xer_so = 1;
    } else {
        xer_ov = 0;
    }
}

#if !defined (CONFIG_USER_ONLY)
void do_POWER_rac (void)
{
#if 0
    mmu_ctx_t ctx;

    /* We don't have to generate many instances of this instruction,
     * as rac is supervisor only.
     */
    if (get_physical_address(env, &ctx, T0, 0, ACCESS_INT, 1) == 0)
        T0 = ctx.raddr;
#endif
}

void do_POWER_rfsvc (void)
{
    env->nip = env->lr & ~0x00000003UL;
    T0 = env->ctr & 0x0000FFFFUL;
    do_store_msr(env, T0);
#if defined (DEBUG_OP)
    dump_rfi();
#endif
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

/* PowerPC 601 BAT management helper */
void do_store_601_batu (int nr)
{
    do_store_ibatu(env, nr, (uint32_t)T0);
    env->DBAT[0][nr] = env->IBAT[0][nr];
    env->DBAT[1][nr] = env->IBAT[1][nr];
}
#endif

/*****************************************************************************/
/* 602 specific instructions */
/* mfrom is the most crazy instruction ever seen, imho ! */
/* Real implementation uses a ROM table. Do the same */
#define USE_MFROM_ROM_TABLE
void do_op_602_mfrom (void)
{
    if (likely(T0 < 602)) {
#if defined(USE_MFROM_ROM_TABLE)
#include "mfrom_table.c"
        T0 = mfrom_ROM_table[T0];
#else
        double d;
        /* Extremly decomposed:
         *                    -T0 / 256
         * T0 = 256 * log10(10          + 1.0) + 0.5
         */
        d = T0;
        d = float64_div(d, 256, &env->fp_status);
        d = float64_chs(d);
        d = exp10(d); // XXX: use float emulation function
        d = float64_add(d, 1.0, &env->fp_status);
        d = log10(d); // XXX: use float emulation function
        d = float64_mul(d, 256, &env->fp_status);
        d = float64_add(d, 0.5, &env->fp_status);
        T0 = float64_round_to_int(d, &env->fp_status);
#endif
    } else {
        T0 = 0;
    }
}

/*****************************************************************************/
/* Embedded PowerPC specific helpers */
void do_405_check_ov (void)
{
    if (likely((((uint32_t)T1 ^ (uint32_t)T2) >> 31) ||
               !(((uint32_t)T0 ^ (uint32_t)T2) >> 31))) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
}

void do_405_check_sat (void)
{
    if (!likely((((uint32_t)T1 ^ (uint32_t)T2) >> 31) ||
                !(((uint32_t)T0 ^ (uint32_t)T2) >> 31))) {
        /* Saturate result */
        if (T2 >> 31) {
            T0 = INT32_MIN;
        } else {
            T0 = INT32_MAX;
        }
    }
}

#if !defined(CONFIG_USER_ONLY)
void do_4xx_rfci (void)
{
    env->nip = env->spr[SPR_40x_SRR2];
    T0 = env->spr[SPR_40x_SRR3] & ~0xFFFF0000;
    do_store_msr(env, T0);
#if defined (DEBUG_OP)
    dump_rfi();
#endif
    env->interrupt_request = CPU_INTERRUPT_EXITTB;
}

void do_4xx_load_dcr (int dcrn)
{
    target_ulong val;
    
    if (unlikely(env->dcr_read == NULL))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_INVAL);
    else if (unlikely((*env->dcr_read)(env->dcr_env, dcrn, &val) != 0))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_PRIV_REG);
    else
        T0 = val;
}

void do_4xx_store_dcr (int dcrn)
{
    if (unlikely(env->dcr_write == NULL))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_INVAL);
    else if (unlikely((*env->dcr_write)(env->dcr_env, dcrn, T0) != 0))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_PRIV_REG);
}

void do_load_403_pb (int num)
{
    T0 = env->pb[num];
}

void do_store_403_pb (int num)
{
    if (likely(env->pb[num] != T0)) {
        env->pb[num] = T0;
        /* Should be optimized */
        tlb_flush(env, 1);
    }
}
#endif

/* 440 specific */
void do_440_dlmzb (void)
{
    target_ulong mask;
    int i;

    i = 1;
    for (mask = 0xFF000000; mask != 0; mask = mask >> 8) {
        if ((T0 & mask) == 0)
            goto done;
        i++;
    }
    for (mask = 0xFF000000; mask != 0; mask = mask >> 8) {
        if ((T1 & mask) == 0)
            break;
        i++;
    }
 done:
    T0 = i;
}

/*****************************************************************************/
/* Softmmu support */
#if !defined (CONFIG_USER_ONLY)

#define MMUSUFFIX _mmu
#define GETPC() (__builtin_return_address(0))

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill (target_ulong addr, int is_write, int is_user, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    target_phys_addr_t pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_ppc_handle_mmu_fault(env, addr, is_write, is_user, 1);
    if (unlikely(ret != 0)) {
        if (likely(retaddr)) {
            /* now we have a real cpu fault */
            pc = (target_phys_addr_t)retaddr;
            tb = tb_find_pc(pc);
            if (likely(tb)) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        do_raise_exception_err(env->exception_index, env->error_code);
    }
    env = saved_env;
}

/* TLB invalidation helpers */
void do_tlbia (void)
{
    if (unlikely(PPC_MMU(env) == PPC_FLAGS_MMU_SOFT_6xx)) {
        ppc6xx_tlb_invalidate_all(env);
    } else if (unlikely(PPC_MMU(env) == PPC_FLAGS_MMU_SOFT_4xx)) {
        /* XXX: TODO */
#if 0
        ppcbooke_tlb_invalidate_all(env);
#endif
    } else {
        tlb_flush(env, 1);
    }
}

void do_tlbie (void)
{
    T0 = (uint32_t)T0;
#if !defined(FLUSH_ALL_TLBS)
    if (unlikely(PPC_MMU(env) == PPC_FLAGS_MMU_SOFT_6xx)) {
        ppc6xx_tlb_invalidate_virt(env, T0 & TARGET_PAGE_MASK, 0);
        if (env->id_tlbs == 1)
            ppc6xx_tlb_invalidate_virt(env, T0 & TARGET_PAGE_MASK, 1);
    } else if (unlikely(PPC_MMU(env) == PPC_FLAGS_MMU_SOFT_4xx)) {
        /* XXX: TODO */
#if 0
        ppcbooke_tlb_invalidate_virt(env, T0 & TARGET_PAGE_MASK,
                                     env->spr[SPR_BOOKE_PID]);
#endif
    } else {
        /* tlbie invalidate TLBs for all segments */
        T0 &= TARGET_PAGE_MASK;
        T0 &= ~((target_ulong)-1 << 28);
        /* XXX: this case should be optimized,
         * giving a mask to tlb_flush_page
         */
        tlb_flush_page(env, T0 | (0x0 << 28));
        tlb_flush_page(env, T0 | (0x1 << 28));
        tlb_flush_page(env, T0 | (0x2 << 28));
        tlb_flush_page(env, T0 | (0x3 << 28));
        tlb_flush_page(env, T0 | (0x4 << 28));
        tlb_flush_page(env, T0 | (0x5 << 28));
        tlb_flush_page(env, T0 | (0x6 << 28));
        tlb_flush_page(env, T0 | (0x7 << 28));
        tlb_flush_page(env, T0 | (0x8 << 28));
        tlb_flush_page(env, T0 | (0x9 << 28));
        tlb_flush_page(env, T0 | (0xA << 28));
        tlb_flush_page(env, T0 | (0xB << 28));
        tlb_flush_page(env, T0 | (0xC << 28));
        tlb_flush_page(env, T0 | (0xD << 28));
        tlb_flush_page(env, T0 | (0xE << 28));
        tlb_flush_page(env, T0 | (0xF << 28));
    }
#else
    do_tlbia();
#endif
}

#if defined(TARGET_PPC64)
void do_tlbie_64 (void)
{
    T0 = (uint64_t)T0;
#if !defined(FLUSH_ALL_TLBS)
    if (unlikely(PPC_MMU(env) == PPC_FLAGS_MMU_SOFT_6xx)) {
        ppc6xx_tlb_invalidate_virt(env, T0 & TARGET_PAGE_MASK, 0);
        if (env->id_tlbs == 1)
            ppc6xx_tlb_invalidate_virt(env, T0 & TARGET_PAGE_MASK, 1);
    } else if (unlikely(PPC_MMU(env) == PPC_FLAGS_MMU_SOFT_4xx)) {
        /* XXX: TODO */
#if 0
        ppcbooke_tlb_invalidate_virt(env, T0 & TARGET_PAGE_MASK,
                                     env->spr[SPR_BOOKE_PID]);
#endif
    } else {
        /* tlbie invalidate TLBs for all segments
         * As we have 2^36 segments, invalidate all qemu TLBs
         */
#if 0
        T0 &= TARGET_PAGE_MASK;
        T0 &= ~((target_ulong)-1 << 28);
        /* XXX: this case should be optimized,
         * giving a mask to tlb_flush_page
         */
        tlb_flush_page(env, T0 | (0x0 << 28));
        tlb_flush_page(env, T0 | (0x1 << 28));
        tlb_flush_page(env, T0 | (0x2 << 28));
        tlb_flush_page(env, T0 | (0x3 << 28));
        tlb_flush_page(env, T0 | (0x4 << 28));
        tlb_flush_page(env, T0 | (0x5 << 28));
        tlb_flush_page(env, T0 | (0x6 << 28));
        tlb_flush_page(env, T0 | (0x7 << 28));
        tlb_flush_page(env, T0 | (0x8 << 28));
        tlb_flush_page(env, T0 | (0x9 << 28));
        tlb_flush_page(env, T0 | (0xA << 28));
        tlb_flush_page(env, T0 | (0xB << 28));
        tlb_flush_page(env, T0 | (0xC << 28));
        tlb_flush_page(env, T0 | (0xD << 28));
        tlb_flush_page(env, T0 | (0xE << 28));
        tlb_flush_page(env, T0 | (0xF << 28));
#else
        tlb_flush(env, 1);
#endif
    }
#else
    do_tlbia();
#endif
}
#endif

#if defined(TARGET_PPC64)
void do_slbia (void)
{
    /* XXX: TODO */
    tlb_flush(env, 1);
}

void do_slbie (void)
{
    /* XXX: TODO */
    tlb_flush(env, 1);
}
#endif

/* Software driven TLBs management */
/* PowerPC 602/603 software TLB load instructions helpers */
void do_load_6xx_tlb (int is_code)
{
    target_ulong RPN, CMP, EPN;
    int way;

    RPN = env->spr[SPR_RPA];
    if (is_code) {
        CMP = env->spr[SPR_ICMP];
        EPN = env->spr[SPR_IMISS];
    } else {
        CMP = env->spr[SPR_DCMP];
        EPN = env->spr[SPR_DMISS];
    }
    way = (env->spr[SPR_SRR1] >> 17) & 1;
#if defined (DEBUG_SOFTWARE_TLB)
    if (loglevel != 0) {
        fprintf(logfile, "%s: EPN %08lx %08lx PTE0 %08lx PTE1 %08lx way %d\n",
                __func__, (unsigned long)T0, (unsigned long)EPN,
                (unsigned long)CMP, (unsigned long)RPN, way);
    }
#endif
    /* Store this TLB */
    ppc6xx_tlb_store(env, (uint32_t)(T0 & TARGET_PAGE_MASK),
                     way, is_code, CMP, RPN);
}

/* Helpers for 4xx TLB management */
void do_4xx_tlbia (void)
{
#if 0
    ppc_tlb_t *tlb;
    target_ulong page, end;
    int i;

    for (i = 0; i < 64; i++) {
        tlb = &env->tlb[i];
        if (tlb->prot & PAGE_VALID) {
            end = tlb->EPN + tlb->size;
            for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE)
                tlb_flush_page(env, page);
            tlb->prot &= ~PAGE_VALID;
        }
    }
#endif
}

void do_4xx_tlbre_lo (void)
{
#if 0
    ppc_tlb_t *tlb;

    T0 &= 0x3F;
    tlb = &env->tlb[T0];
    T0 = tlb->stor[0];
    env->spr[SPR_40x_PID] = tlb->pid;
#endif
}

void do_4xx_tlbre_hi (void)
{
#if 0
    ppc_tlb_t *tlb;

    T0 &= 0x3F;
    tlb = &env->tlb[T0];
    T0 = tlb->stor[1];
#endif
}

static int tlb_4xx_search (target_ulong virtual)
{
#if 0
    ppc_tlb_t *tlb;
    target_ulong base, mask;
    int i, ret;

    /* Default return value is no match */
    ret = -1;
    for (i = 0; i < 64; i++) {
        tlb = &env->tlb[i];
        /* Check TLB validity */
        if (!(tlb->prot & PAGE_VALID))
            continue;
        /* Check TLB PID vs current PID */
        if (tlb->pid != 0 && tlb->pid != env->spr[SPR_40x_PID])
            continue;
        /* Check TLB address vs virtual address */
        base = tlb->EPN;
        mask = ~(tlb->size - 1);
        if ((base & mask) != (virtual & mask))
            continue;
        ret = i;
        break;
    }

    return ret;
#else
    return -1;
#endif
}

void do_4xx_tlbsx (void)
{
    T0 = tlb_4xx_search(T0);
}

void do_4xx_tlbsx_ (void)
{
    int tmp = xer_ov;

    T0 = tlb_4xx_search(T0);
    if (T0 != -1)
        tmp |= 0x02;
    env->crf[0] = tmp;
}

void do_4xx_tlbwe_lo (void)
{
#if 0
    ppc_tlb_t *tlb;
    target_ulong page, end;

    T0 &= 0x3F;
    tlb = &env->tlb[T0];
    /* Invalidate previous TLB (if it's valid) */
    if (tlb->prot & PAGE_VALID) {
        end = tlb->EPN + tlb->size;
        for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE)
            tlb_flush_page(env, page);
    }
    tlb->size = 1024 << (2 * ((T1 >> 7) & 0x7));
    tlb->EPN = (T1 & 0xFFFFFC00) & ~(tlb->size - 1);
    if (T1 & 0x400)
        tlb->prot |= PAGE_VALID;
    else
        tlb->prot &= ~PAGE_VALID;
    tlb->pid = env->spr[SPR_BOOKE_PID]; /* PID */
    /* Invalidate new TLB (if valid) */
    if (tlb->prot & PAGE_VALID) {
        end = tlb->EPN + tlb->size;
        for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE)
            tlb_flush_page(env, page);
    }
#endif
}

void do_4xx_tlbwe_hi (void)
{
#if 0
    ppc_tlb_t *tlb;

    T0 &= 0x3F;
    tlb = &env->tlb[T0];
    tlb->RPN = T1 & 0xFFFFFC00;
    tlb->prot = PAGE_READ;
    if (T1 & 0x200)
        tlb->prot |= PAGE_EXEC;
    if (T1 & 0x100)
        tlb->prot |= PAGE_WRITE;
#endif
}
#endif /* !CONFIG_USER_ONLY */
