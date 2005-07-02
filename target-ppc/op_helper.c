/*
 *  PowerPC emulation helpers for qemu.
 * 
 *  Copyright (c) 2003-2005 Jocelyn Mayer
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
#include <math.h>
#include "exec.h"

#define MEMSUFFIX _raw
#include "op_helper_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_helper_mem.h"
#define MEMSUFFIX _kernel
#include "op_helper_mem.h"
#endif

/*****************************************************************************/
/* Exceptions processing helpers */
void cpu_loop_exit(void)
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
/* Helpers for "fat" micro operations */
/* shift right arithmetic helper */
void do_sraw (void)
{
    int32_t ret;

    xer_ca = 0;
    if (T1 & 0x20) {
        ret = (-1) * (T0 >> 31);
        if (ret < 0 && (T0 & ~0x80000000) != 0)
            xer_ca = 1;
#if 1 // TRY
    } else if (T1 == 0) {
        ret = T0;
#endif
    } else {
        ret = (int32_t)T0 >> (T1 & 0x1f);
        if (ret < 0 && ((int32_t)T0 & ((1 << T1) - 1)) != 0)
            xer_ca = 1;
    }
    T0 = ret;
}

/* Floating point operations helpers */
void do_fctiw (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    /* XXX: higher bits are not supposed to be significant.
     *      to make tests easier, return the same as a real PowerPC 750 (aka G3)
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
     *      to make tests easier, return the same as a real PowerPC 750 (aka G3)
     */
    p.i = float64_to_int32_round_to_zero(FT0, &env->fp_status);
    p.i |= 0xFFF80000ULL << 32;
    FT0 = p.d;
}

void do_fnmadd (void)
{
    FT0 = (FT0 * FT1) + FT2;
    if (!isnan(FT0))
        FT0 = -FT0;
}

void do_fnmsub (void)
{
    FT0 = (FT0 * FT1) - FT2;
    if (!isnan(FT0))
        FT0 = -FT0;
}

void do_fdiv (void)
{
    if (FT0 == -0.0 && FT1 == -0.0)
        FT0 = 0.0 / 0.0;
    else
        FT0 /= FT1;
}

void do_fsqrt (void)
{
    FT0 = sqrt(FT0);
}

void do_fres (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    if (isnormal(FT0)) {
        FT0 = (float)(1.0 / FT0);
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

    if (isnormal(FT0) && FT0 > 0.0) {
        FT0 = (float)(1.0 / sqrt(FT0));
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
    if (isnan(FT0) || isnan(FT1)) {
        T0 = 0x01;
        env->fpscr[4] |= 0x1;
        env->fpscr[6] |= 0x1;
    } else if (FT0 < FT1) {
        T0 = 0x08;
    } else if (FT0 > FT1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    env->fpscr[3] = T0;
}

void do_fcmpo (void)
{
    env->fpscr[4] &= ~0x1;
    if (isnan(FT0) || isnan(FT1)) {
        T0 = 0x01;
        env->fpscr[4] |= 0x1;
        /* I don't know how to test "quiet" nan... */
        if (0 /* || ! quiet_nan(...) */) {
            env->fpscr[6] |= 0x1;
            if (!(env->fpscr[1] & 0x8))
                env->fpscr[4] |= 0x8;
        } else {
            env->fpscr[4] |= 0x8;
        }
    } else if (FT0 < FT1) {
        T0 = 0x08;
    } else if (FT0 > FT1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    env->fpscr[3] = T0;
}

void do_fabs (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.d = FT0;
    p.i &= ~0x8000000000000000ULL;
    FT0 = p.d;
}

void do_fnabs (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.d = FT0;
    p.i |= 0x8000000000000000ULL;
    FT0 = p.d;
}

/* Instruction cache invalidation helper */
#define ICACHE_LINE_SIZE 32

void do_check_reservation (void)
{
    if ((env->reserve & ~0x03) == T0)
        env->reserve = -1;
}

void do_icbi (void)
{
    /* Invalidate one cache line */
    T0 &= ~(ICACHE_LINE_SIZE - 1);
    tb_invalidate_page_range(T0, T0 + ICACHE_LINE_SIZE);
}

/* TLB invalidation helpers */
void do_tlbia (void)
{
    tlb_flush(env, 1);
}

void do_tlbie (void)
{
    tlb_flush_page(env, T0);
}

