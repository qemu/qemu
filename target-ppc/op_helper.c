/*
 *  PPC emulation helpers for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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
void do_queue_exception_err (uint32_t exception, int error_code)
{
    /* Queue real PPC exceptions */
    if (exception < EXCP_PPC_MAX) {
        env->exceptions |= 1 << exception;
        env->errors[exception] = error_code;
    } else {
        /* Preserve compatibility with qemu core */
        env->exceptions |= 1;
        env->exception_index = exception;
        env->error_code = error_code;
    }
}

void do_queue_exception (uint32_t exception)
{
    do_queue_exception_err(exception, 0);
}

void do_check_exception_state (void)
{
    if ((env->exceptions & 1) == 1 || check_exception_state(env)) {
        env->exceptions &= ~1;
        cpu_loop_exit();
    }
}

/*****************************************************************************/
/* Helpers for "fat" micro operations */
/* Special registers load and store */
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
            env->crf[i] = (T0 >> (sh * 4)) & 0xF;
    }
}

void do_load_xer (void)
{
    T0 = (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC);
}

void do_store_xer (void)
{
    xer_so = (T0 >> XER_SO) & 0x01;
    xer_ov = (T0 >> XER_OV) & 0x01;
    xer_ca = (T0 >> XER_CA) & 0x01;
    xer_bc = (T0 >> XER_BC) & 0x1f;
}

void do_load_msr (void)
{
    T0 = (msr_pow << MSR_POW) |
        (msr_ile << MSR_ILE) |
        (msr_ee << MSR_EE) |
        (msr_pr << MSR_PR) |
        (msr_fp << MSR_FP) |
        (msr_me << MSR_ME) |
        (msr_fe0 << MSR_FE0) |
        (msr_se << MSR_SE) |
        (msr_be << MSR_BE) |
        (msr_fe1 << MSR_FE1) |
        (msr_ip << MSR_IP) |
        (msr_ir << MSR_IR) |
        (msr_dr << MSR_DR) |
        (msr_ri << MSR_RI) |
        (msr_le << MSR_LE);
}

void do_store_msr (void)
{
    if (((T0 >> MSR_IR) & 0x01) != msr_ir ||
        ((T0 >> MSR_DR) & 0x01) != msr_dr) {
        /* Flush all tlb when changing translation mode or privilege level */
        do_tlbia();
    }
#if 0
    if ((T0 >> MSR_IP) & 0x01) {
        printf("Halting CPU. Stop emulation\n");
        do_queue_exception(EXCP_HLT);
        cpu_loop_exit();
    }
#endif
    msr_pow = (T0 >> MSR_POW) & 0x03;
    msr_ile = (T0 >> MSR_ILE) & 0x01;
    msr_ee = (T0 >> MSR_EE) & 0x01;
    msr_pr = (T0 >> MSR_PR) & 0x01;
    msr_fp = (T0 >> MSR_FP) & 0x01;
    msr_me = (T0 >> MSR_ME) & 0x01;
    msr_fe0 = (T0 >> MSR_FE0) & 0x01;
    msr_se = (T0 >> MSR_SE) & 0x01;
    msr_be = (T0 >> MSR_BE) & 0x01;
    msr_fe1 = (T0 >> MSR_FE1) & 0x01;
    msr_ip = (T0 >> MSR_IP) & 0x01;
    msr_ir = (T0 >> MSR_IR) & 0x01;
    msr_dr = (T0 >> MSR_DR) & 0x01;
    msr_ri = (T0 >> MSR_RI) & 0x01;
    msr_le = (T0 >> MSR_LE) & 0x01;
}

/* shift right arithmetic helper */
void do_sraw (void)
{
    int32_t ret;

    xer_ca = 0;
    if (T1 & 0x20) {
        ret = (-1) * (T0 >> 31);
        if (ret < 0)
            xer_ca = 1;
    } else {
        ret = (int32_t)T0 >> (T1 & 0x1f);
        if (ret < 0 && ((int32_t)T0 & ((1 << T1) - 1)) != 0)
            xer_ca = 1;
    }
    (int32_t)T0 = ret;
}

/* Floating point operations helpers */
void do_load_fpscr (void)
{
    /* The 32 MSB of the target fpr are undefined.
     * They'll be zero...
     */
    union {
        double d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i;

    u.s.u[0] = 0;
    u.s.u[1] = 0;
    for (i = 0; i < 8; i++)
        u.s.u[1] |= env->fpscr[i] << (4 * i);
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
    int i;

    u.d = FT0;
    if (mask & 0x80)
        env->fpscr[0] = (env->fpscr[0] & 0x9) | ((u.s.u[1] >> 28) & ~0x9);
    for (i = 1; i < 7; i++) {
        if (mask & (1 << (7 - i)))
            env->fpscr[i] = (u.s.u[1] >> (4 * (7 - i))) & 0xF;
    }
    /* TODO: update FEX & VX */
    /* Set rounding mode */
    switch (env->fpscr[0] & 0x3) {
    case 0:
        /* Best approximation (round to nearest) */
        fesetround(FE_TONEAREST);
        break;
    case 1:
        /* Smaller magnitude (round toward zero) */
        fesetround(FE_TOWARDZERO);
        break;
    case 2:
        /* Round toward +infinite */
        fesetround(FE_UPWARD);
        break;
    case 3:
        /* Round toward -infinite */
        fesetround(FE_DOWNWARD);
        break;
    }
}

void do_fctiw (void)
{
    union {
        double d;
        uint64_t i;
    } *p = (void *)&FT1;

    if (FT0 > (double)0x7FFFFFFF)
        p->i = 0x7FFFFFFFULL << 32;
    else if (FT0 < -(double)0x80000000)
        p->i = 0x80000000ULL << 32;
    else
        p->i = 0;
    p->i |= (uint32_t)FT0;
    FT0 = p->d;
}

void do_fctiwz (void)
{
    union {
        double d;
        uint64_t i;
    } *p = (void *)&FT1;
    int cround = fegetround();

    fesetround(FE_TOWARDZERO);
    if (FT0 > (double)0x7FFFFFFF)
        p->i = 0x7FFFFFFFULL << 32;
    else if (FT0 < -(double)0x80000000)
        p->i = 0x80000000ULL << 32;
    else
        p->i = 0;
    p->i |= (uint32_t)FT0;
    FT0 = p->d;
    fesetround(cround);
}

void do_fsqrt (void)
{
    FT0 = sqrt(FT0);
}

void do_fsqrts (void)
{
    FT0 = (float)sqrt((float)FT0);
}

void do_fres (void)
{
    FT0 = 1.0 / FT0;
}

void do_fsqrte (void)
{
    FT0 = 1.0 / sqrt(FT0);
}

void do_fsel (void)
{
    if (FT0 >= 0)
        FT0 = FT2;
    else
        FT0 = FT1;
}

void do_fcmpu (void)
{
    env->fpscr[4] &= ~0x1;
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
    env->fpscr[3] |= T0;
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
    env->fpscr[3] |= T0;
}

void do_fabs (void)
{
    FT0 = fabsl(FT0);
}

void do_fnabs (void)
{
    FT0 = -fabsl(FT0);
}

/* Instruction cache invalidation helper */
#define ICACHE_LINE_SIZE 32

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

/*****************************************************************************/
/* Special helpers for debug */
extern FILE *stdout;

void dump_state (void)
{
    cpu_ppc_dump_state(env, stdout, 0);
}

void dump_rfi (void)
{
#if 0
    printf("Return from interrupt %d => 0x%08x\n", pos, env->nip);
    //    cpu_ppc_dump_state(env, stdout, 0);
#endif
}

void dump_store_sr (int srnum)
{
#if 0
    printf("%s: reg=%d 0x%08x\n", __func__, srnum, T0);
#endif
}

static void _dump_store_bat (char ID, int ul, int nr)
{
    printf("Set %cBAT%d%c to 0x%08x (0x%08x)\n",
           ID, nr, ul == 0 ? 'u' : 'l', T0, env->nip);
}

void dump_store_ibat (int ul, int nr)
{
    _dump_store_bat('I', ul, nr);
}

void dump_store_dbat (int ul, int nr)
{
    _dump_store_bat('D', ul, nr);
}

void dump_store_tb (int ul)
{
    printf("Set TB%c to 0x%08x\n", ul == 0 ? 'L' : 'U', T0);
}

void dump_update_tb(uint32_t param)
{
#if 0
    printf("Update TB: 0x%08x + %d => 0x%08x\n", T1, param, T0);
#endif
}

