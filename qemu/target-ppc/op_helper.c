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
//#define FLUSH_ALL_TLBS

#define Ts0 (long)((target_long)T0)
#define Ts1 (long)((target_long)T1)
#define Ts2 (long)((target_long)T2)

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
/* Fixed point operations helpers */
void do_addo (void)
{
    T2 = T0;
    T0 += T1;
    if (likely(!((T2 ^ T1 ^ (-1)) & (T2 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
}

void do_addco (void)
{
    T2 = T0;
    T0 += T1;
    if (likely(T0 >= T2)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    if (likely(!((T2 ^ T1 ^ (-1)) & (T2 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
}

void do_adde (void)
{
    T2 = T0;
    T0 += T1 + xer_ca;
    if (likely(!(T0 < T2 || (xer_ca == 1 && T0 == T2)))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

void do_addeo (void)
{
    T2 = T0;
    T0 += T1 + xer_ca;
    if (likely(!(T0 < T2 || (xer_ca == 1 && T0 == T2)))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    if (likely(!((T2 ^ T1 ^ (-1)) & (T2 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
}

void do_addmeo (void)
{
    T1 = T0;
    T0 += xer_ca + (-1);
    if (likely(!(T1 & (T1 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely(T1 != 0))
        xer_ca = 1;
}

void do_addzeo (void)
{
    T1 = T0;
    T0 += xer_ca;
    if (likely(!((T1 ^ (-1)) & (T1 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely(T0 >= T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

void do_divwo (void)
{
    if (likely(!((Ts0 == INT32_MIN && Ts1 == -1) || Ts1 == 0))) {
        xer_ov = 0;
        T0 = (Ts0 / Ts1);
    } else {
        xer_so = 1;
        xer_ov = 1;
        T0 = (-1) * ((uint32_t)T0 >> 31);
    }
}

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

void do_mullwo (void)
{
    int64_t res = (int64_t)Ts0 * (int64_t)Ts1;

    if (likely((int32_t)res == res)) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    T0 = (int32_t)res;
}

void do_nego (void)
{
    if (likely(T0 != INT32_MIN)) {
        xer_ov = 0;
        T0 = -Ts0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
}

void do_subfo (void)
{
    T2 = T0;
    T0 = T1 - T0;
    if (likely(!(((~T2) ^ T1 ^ (-1)) & ((~T2) ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    RETURN();
}

void do_subfco (void)
{
    T2 = T0;
    T0 = T1 - T0;
    if (likely(T0 > T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    if (likely(!(((~T2) ^ T1 ^ (-1)) & ((~T2) ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
}

void do_subfe (void)
{
    T0 = T1 + ~T0 + xer_ca;
    if (likely(T0 >= T1 && (xer_ca == 0 || T0 != T1))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

void do_subfeo (void)
{
    T2 = T0;
    T0 = T1 + ~T0 + xer_ca;
    if (likely(!((~T2 ^ T1 ^ (-1)) & (~T2 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely(T0 >= T1 && (xer_ca == 0 || T0 != T1))) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

void do_subfmeo (void)
{
    T1 = T0;
    T0 = ~T0 + xer_ca - 1;
    if (likely(!(~T1 & (~T1 ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_so = 1;
        xer_ov = 1;
    }
    if (likely(T1 != -1))
        xer_ca = 1;
}

void do_subfzeo (void)
{
    T1 = T0;
    T0 = ~T0 + xer_ca;
    if (likely(!((~T1 ^ (-1)) & ((~T1) ^ T0) & (1 << 31)))) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    if (likely(T0 >= ~T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
}

/* shift right arithmetic helper */
void do_sraw (void)
{
    int32_t ret;

    if (likely(!(T1 & 0x20UL))) {
        if (likely(T1 != 0)) {
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

/*****************************************************************************/
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
        /* I don't know how to test "quiet" nan... */
        if (0 /* || ! quiet_nan(...) */) {
            env->fpscr[6] |= 0x1;
            if (!(env->fpscr[1] & 0x8))
                env->fpscr[4] |= 0x8;
        } else {
            env->fpscr[4] |= 0x8;
        }
    }
    env->fpscr[3] = T0;
}

void do_rfi (void)
{
    env->nip = env->spr[SPR_SRR0] & ~0x00000003;
    T0 = env->spr[SPR_SRR1] & ~0xFFFF0000UL;
    do_store_msr(env, T0);
#if defined (DEBUG_OP)
    dump_rfi();
#endif
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

void do_tw (uint32_t cmp, int flags)
{
    if (!likely(!((Ts0 < (int32_t)cmp && (flags & 0x10)) ||
                  (Ts0 > (int32_t)cmp && (flags & 0x08)) ||
                  (Ts0 == (int32_t)cmp && (flags & 0x04)) ||
                  (T0 < cmp && (flags & 0x02)) ||
                  (T0 > cmp && (flags & 0x01)))))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_TRAP);
}

/* Instruction cache invalidation helper */
void do_icbi (void)
{
    uint32_t tmp;
    /* Invalidate one cache line :
     * PowerPC specification says this is to be treated like a load
     * (not a fetch) by the MMU. To be sure it will be so,
     * do the load "by hand".
     */
#if defined(TARGET_PPC64)
    if (!msr_sf)
        T0 &= 0xFFFFFFFFULL;
#endif
    tmp = ldl_kernel(T0);
    T0 &= ~(ICACHE_LINE_SIZE - 1);
    tb_invalidate_page_range(T0, T0 + ICACHE_LINE_SIZE);
}

/*****************************************************************************/
/* MMU related helpers */
/* TLB invalidation helpers */
void do_tlbia (void)
{
    tlb_flush(env, 1);
}

void do_tlbie (void)
{
#if !defined(FLUSH_ALL_TLBS)
    tlb_flush_page(env, T0);
#else
    do_tlbia();
#endif
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
    if (!likely(ret == 0)) {
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
#endif /* !CONFIG_USER_ONLY */

