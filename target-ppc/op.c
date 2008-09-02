/*
 *  PowerPC emulation micro-operations for qemu.
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

//#define DEBUG_OP

#include "config.h"
#include "exec.h"
#include "host-utils.h"
#include "helper_regs.h"
#include "op_helper.h"

#define REG 0
#include "op_template.h"

#define REG 1
#include "op_template.h"

#define REG 2
#include "op_template.h"

#define REG 3
#include "op_template.h"

#define REG 4
#include "op_template.h"

#define REG 5
#include "op_template.h"

#define REG 6
#include "op_template.h"

#define REG 7
#include "op_template.h"

#define REG 8
#include "op_template.h"

#define REG 9
#include "op_template.h"

#define REG 10
#include "op_template.h"

#define REG 11
#include "op_template.h"

#define REG 12
#include "op_template.h"

#define REG 13
#include "op_template.h"

#define REG 14
#include "op_template.h"

#define REG 15
#include "op_template.h"

#define REG 16
#include "op_template.h"

#define REG 17
#include "op_template.h"

#define REG 18
#include "op_template.h"

#define REG 19
#include "op_template.h"

#define REG 20
#include "op_template.h"

#define REG 21
#include "op_template.h"

#define REG 22
#include "op_template.h"

#define REG 23
#include "op_template.h"

#define REG 24
#include "op_template.h"

#define REG 25
#include "op_template.h"

#define REG 26
#include "op_template.h"

#define REG 27
#include "op_template.h"

#define REG 28
#include "op_template.h"

#define REG 29
#include "op_template.h"

#define REG 30
#include "op_template.h"

#define REG 31
#include "op_template.h"

void OPPROTO op_print_mem_EA (void)
{
    do_print_mem_EA(T0);
    RETURN();
}

/* PowerPC state maintenance operations */
/* set_Rc0 */
void OPPROTO op_set_Rc0 (void)
{
    env->crf[0] = T0 | xer_so;
    RETURN();
}

/* Constants load */
void OPPROTO op_reset_T0 (void)
{
    T0 = 0;
    RETURN();
}

void OPPROTO op_set_T0 (void)
{
    T0 = (uint32_t)PARAM1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_set_T0_64 (void)
{
    T0 = ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    RETURN();
}
#endif

void OPPROTO op_set_T1 (void)
{
    T1 = (uint32_t)PARAM1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_set_T1_64 (void)
{
    T1 = ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    RETURN();
}
#endif

#if 0 // unused
void OPPROTO op_set_T2 (void)
{
    T2 = (uint32_t)PARAM1;
    RETURN();
}
#endif

void OPPROTO op_moven_T2_T0 (void)
{
    T2 = ~T0;
    RETURN();
}

/* Generate exceptions */
void OPPROTO op_raise_exception_err (void)
{
    do_raise_exception_err(PARAM1, PARAM2);
}

void OPPROTO op_update_nip (void)
{
    env->nip = (uint32_t)PARAM1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_update_nip_64 (void)
{
    env->nip = ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    RETURN();
}
#endif

void OPPROTO op_debug (void)
{
    do_raise_exception(EXCP_DEBUG);
}

/* Load/store special registers */
void OPPROTO op_load_cr (void)
{
    do_load_cr();
    RETURN();
}

void OPPROTO op_store_cr (void)
{
    do_store_cr(PARAM1);
    RETURN();
}

void OPPROTO op_load_cro (void)
{
    T0 = env->crf[PARAM1];
    RETURN();
}

void OPPROTO op_store_cro (void)
{
    env->crf[PARAM1] = T0;
    RETURN();
}

void OPPROTO op_load_xer_cr (void)
{
    T0 = (xer_so << 3) | (xer_ov << 2) | (xer_ca << 1);
    RETURN();
}

void OPPROTO op_clear_xer_ov (void)
{
    xer_so = 0;
    xer_ov = 0;
    RETURN();
}

void OPPROTO op_clear_xer_ca (void)
{
    xer_ca = 0;
    RETURN();
}

void OPPROTO op_load_xer_bc (void)
{
    T1 = xer_bc;
    RETURN();
}

void OPPROTO op_store_xer_bc (void)
{
    xer_bc = T0;
    RETURN();
}

void OPPROTO op_load_xer (void)
{
    T0 = hreg_load_xer(env);
    RETURN();
}

void OPPROTO op_store_xer (void)
{
    hreg_store_xer(env, T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_store_pri (void)
{
    do_store_pri(PARAM1);
    RETURN();
}
#endif

#if !defined(CONFIG_USER_ONLY)
/* Segment registers load and store */
void OPPROTO op_load_sr (void)
{
    T0 = env->sr[T1];
    RETURN();
}

void OPPROTO op_store_sr (void)
{
    do_store_sr(env, T1, T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_load_slb (void)
{
    T0 = ppc_load_slb(env, T1);
    RETURN();
}

void OPPROTO op_store_slb (void)
{
    ppc_store_slb(env, T1, T0);
    RETURN();
}
#endif /* defined(TARGET_PPC64) */

void OPPROTO op_load_sdr1 (void)
{
    T0 = env->sdr1;
    RETURN();
}

void OPPROTO op_store_sdr1 (void)
{
    do_store_sdr1(env, T0);
    RETURN();
}

#if defined (TARGET_PPC64)
void OPPROTO op_load_asr (void)
{
    T0 = env->asr;
    RETURN();
}

void OPPROTO op_store_asr (void)
{
    ppc_store_asr(env, T0);
    RETURN();
}
#endif

void OPPROTO op_load_msr (void)
{
    T0 = env->msr;
    RETURN();
}

void OPPROTO op_store_msr (void)
{
    do_store_msr();
    RETURN();
}

#if defined (TARGET_PPC64)
void OPPROTO op_store_msr_32 (void)
{
    T0 = (env->msr & ~0xFFFFFFFFULL) | (T0 & 0xFFFFFFFF);
    do_store_msr();
    RETURN();
}
#endif

void OPPROTO op_update_riee (void)
{
    /* We don't call do_store_msr here as we won't trigger
     * any special case nor change hflags
     */
    T0 &= (1 << MSR_RI) | (1 << MSR_EE);
    env->msr &= ~(1 << MSR_RI) | (1 << MSR_EE);
    env->msr |= T0;
    RETURN();
}
#endif

/* SPR */
void OPPROTO op_load_spr (void)
{
    T0 = env->spr[PARAM1];
    RETURN();
}

void OPPROTO op_store_spr (void)
{
    env->spr[PARAM1] = T0;
    RETURN();
}

void OPPROTO op_load_dump_spr (void)
{
    T0 = ppc_load_dump_spr(PARAM1);
    RETURN();
}

void OPPROTO op_store_dump_spr (void)
{
    ppc_store_dump_spr(PARAM1, T0);
    RETURN();
}

void OPPROTO op_mask_spr (void)
{
    env->spr[PARAM1] &= ~T0;
    RETURN();
}

void OPPROTO op_load_lr (void)
{
    T0 = env->lr;
    RETURN();
}

void OPPROTO op_store_lr (void)
{
    env->lr = T0;
    RETURN();
}

void OPPROTO op_load_ctr (void)
{
    T0 = env->ctr;
    RETURN();
}

void OPPROTO op_store_ctr (void)
{
    env->ctr = T0;
    RETURN();
}

void OPPROTO op_load_tbl (void)
{
    T0 = cpu_ppc_load_tbl(env);
    RETURN();
}

void OPPROTO op_load_tbu (void)
{
    T0 = cpu_ppc_load_tbu(env);
    RETURN();
}

void OPPROTO op_load_atbl (void)
{
    T0 = cpu_ppc_load_atbl(env);
    RETURN();
}

void OPPROTO op_load_atbu (void)
{
    T0 = cpu_ppc_load_atbu(env);
    RETURN();
}

#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_store_tbl (void)
{
    cpu_ppc_store_tbl(env, T0);
    RETURN();
}

void OPPROTO op_store_tbu (void)
{
    cpu_ppc_store_tbu(env, T0);
    RETURN();
}

void OPPROTO op_store_atbl (void)
{
    cpu_ppc_store_atbl(env, T0);
    RETURN();
}

void OPPROTO op_store_atbu (void)
{
    cpu_ppc_store_atbu(env, T0);
    RETURN();
}

void OPPROTO op_load_decr (void)
{
    T0 = cpu_ppc_load_decr(env);
    RETURN();
}

void OPPROTO op_store_decr (void)
{
    cpu_ppc_store_decr(env, T0);
    RETURN();
}

void OPPROTO op_load_ibat (void)
{
    T0 = env->IBAT[PARAM1][PARAM2];
    RETURN();
}

void OPPROTO op_store_ibatu (void)
{
    do_store_ibatu(env, PARAM1, T0);
    RETURN();
}

void OPPROTO op_store_ibatl (void)
{
#if 1
    env->IBAT[1][PARAM1] = T0;
#else
    do_store_ibatl(env, PARAM1, T0);
#endif
    RETURN();
}

void OPPROTO op_load_dbat (void)
{
    T0 = env->DBAT[PARAM1][PARAM2];
    RETURN();
}

void OPPROTO op_store_dbatu (void)
{
    do_store_dbatu(env, PARAM1, T0);
    RETURN();
}

void OPPROTO op_store_dbatl (void)
{
#if 1
    env->DBAT[1][PARAM1] = T0;
#else
    do_store_dbatl(env, PARAM1, T0);
#endif
    RETURN();
}
#endif /* !defined(CONFIG_USER_ONLY) */

/* FPSCR */
#ifdef CONFIG_SOFTFLOAT
void OPPROTO op_reset_fpstatus (void)
{
    env->fp_status.float_exception_flags = 0;
    RETURN();
}
#endif

void OPPROTO op_compute_fprf (void)
{
    do_compute_fprf(PARAM1);
    RETURN();
}

#ifdef CONFIG_SOFTFLOAT
void OPPROTO op_float_check_status (void)
{
    do_float_check_status();
    RETURN();
}
#else
void OPPROTO op_float_check_status (void)
{
    if (env->exception_index == POWERPC_EXCP_PROGRAM &&
        (env->error_code & POWERPC_EXCP_FP)) {
        /* Differred floating-point exception after target FPR update */
        if (msr_fe0 != 0 || msr_fe1 != 0)
            do_raise_exception_err(env->exception_index, env->error_code);
    }
    RETURN();
}
#endif

void OPPROTO op_load_fpscr_FT0 (void)
{
    /* The 32 MSB of the target fpr are undefined.
     * They'll be zero...
     */
    CPU_DoubleU u;

    u.l.upper = 0;
    u.l.lower = env->fpscr;
    FT0 = u.d;
    RETURN();
}

void OPPROTO op_set_FT0 (void)
{
    CPU_DoubleU u;

    u.l.upper = 0;
    u.l.lower = PARAM1;
    FT0 = u.d;
    RETURN();
}

void OPPROTO op_load_fpscr_T0 (void)
{
    T0 = (env->fpscr >> PARAM1) & 0xF;
    RETURN();
}

void OPPROTO op_load_fpcc (void)
{
    T0 = fpscr_fpcc;
    RETURN();
}

void OPPROTO op_fpscr_resetbit (void)
{
    env->fpscr &= PARAM1;
    RETURN();
}

void OPPROTO op_fpscr_setbit (void)
{
    do_fpscr_setbit(PARAM1);
    RETURN();
}

void OPPROTO op_store_fpscr (void)
{
    do_store_fpscr(PARAM1);
    RETURN();
}

/* Branch */
#define EIP env->nip

void OPPROTO op_setlr (void)
{
    env->lr = (uint32_t)PARAM1;
    RETURN();
}

#if defined (TARGET_PPC64)
void OPPROTO op_setlr_64 (void)
{
    env->lr = ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    RETURN();
}
#endif

void OPPROTO op_b_T1 (void)
{
    env->nip = (uint32_t)(T1 & ~3);
    RETURN();
}

#if defined (TARGET_PPC64)
void OPPROTO op_b_T1_64 (void)
{
    env->nip = (uint64_t)(T1 & ~3);
    RETURN();
}
#endif

void OPPROTO op_jz_T0 (void)
{
    if (!T0)
        GOTO_LABEL_PARAM(1);
    RETURN();
}

void OPPROTO op_btest_T1 (void)
{
    if (T0) {
        env->nip = (uint32_t)(T1 & ~3);
    } else {
        env->nip = (uint32_t)PARAM1;
    }
    RETURN();
}

#if defined (TARGET_PPC64)
void OPPROTO op_btest_T1_64 (void)
{
    if (T0) {
        env->nip = (uint64_t)(T1 & ~3);
    } else {
        env->nip = ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    }
    RETURN();
}
#endif

void OPPROTO op_movl_T1_ctr (void)
{
    T1 = env->ctr;
    RETURN();
}

void OPPROTO op_movl_T1_lr (void)
{
    T1 = env->lr;
    RETURN();
}

/* tests with result in T0 */
void OPPROTO op_test_ctr (void)
{
    T0 = (uint32_t)env->ctr;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_test_ctr_64 (void)
{
    T0 = (uint64_t)env->ctr;
    RETURN();
}
#endif

void OPPROTO op_test_ctr_true (void)
{
    T0 = ((uint32_t)env->ctr != 0 && (T0 & PARAM1) != 0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_test_ctr_true_64 (void)
{
    T0 = ((uint64_t)env->ctr != 0 && (T0 & PARAM1) != 0);
    RETURN();
}
#endif

void OPPROTO op_test_ctr_false (void)
{
    T0 = ((uint32_t)env->ctr != 0 && (T0 & PARAM1) == 0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_test_ctr_false_64 (void)
{
    T0 = ((uint64_t)env->ctr != 0 && (T0 & PARAM1) == 0);
    RETURN();
}
#endif

void OPPROTO op_test_ctrz (void)
{
    T0 = ((uint32_t)env->ctr == 0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_test_ctrz_64 (void)
{
    T0 = ((uint64_t)env->ctr == 0);
    RETURN();
}
#endif

void OPPROTO op_test_ctrz_true (void)
{
    T0 = ((uint32_t)env->ctr == 0 && (T0 & PARAM1) != 0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_test_ctrz_true_64 (void)
{
    T0 = ((uint64_t)env->ctr == 0 && (T0 & PARAM1) != 0);
    RETURN();
}
#endif

void OPPROTO op_test_ctrz_false (void)
{
    T0 = ((uint32_t)env->ctr == 0 && (T0 & PARAM1) == 0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_test_ctrz_false_64 (void)
{
    T0 = ((uint64_t)env->ctr == 0 && (T0 & PARAM1) == 0);
    RETURN();
}
#endif

void OPPROTO op_test_true (void)
{
    T0 = (T0 & PARAM1);
    RETURN();
}

void OPPROTO op_test_false (void)
{
    T0 = ((T0 & PARAM1) == 0);
    RETURN();
}

/* CTR maintenance */
void OPPROTO op_dec_ctr (void)
{
    env->ctr--;
    RETURN();
}

/***                           Integer arithmetic                          ***/
/* add */
void OPPROTO op_add (void)
{
    T0 += T1;
    RETURN();
}

void OPPROTO op_check_addo (void)
{
    xer_ov = (((uint32_t)T2 ^ (uint32_t)T1 ^ UINT32_MAX) &
              ((uint32_t)T2 ^ (uint32_t)T0)) >> 31;
    xer_so |= xer_ov;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_check_addo_64 (void)
{
    xer_ov = (((uint64_t)T2 ^ (uint64_t)T1 ^ UINT64_MAX) &
              ((uint64_t)T2 ^ (uint64_t)T0)) >> 63;
    xer_so |= xer_ov;
    RETURN();
}
#endif

/* add carrying */
void OPPROTO op_check_addc (void)
{
    if (likely((uint32_t)T0 >= (uint32_t)T2)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_check_addc_64 (void)
{
    if (likely((uint64_t)T0 >= (uint64_t)T2)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    RETURN();
}
#endif

/* add extended */
void OPPROTO op_adde (void)
{
    do_adde();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_adde_64 (void)
{
    do_adde_64();
    RETURN();
}
#endif

/* add immediate */
void OPPROTO op_addi (void)
{
    T0 += (int32_t)PARAM1;
    RETURN();
}

/* add to minus one extended */
void OPPROTO op_add_me (void)
{
    T0 += xer_ca + (-1);
    if (likely((uint32_t)T1 != 0))
        xer_ca = 1;
    else
        xer_ca = 0;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_add_me_64 (void)
{
    T0 += xer_ca + (-1);
    if (likely((uint64_t)T1 != 0))
        xer_ca = 1;
    else
        xer_ca = 0;
    RETURN();
}
#endif

void OPPROTO op_addmeo (void)
{
    do_addmeo();
    RETURN();
}

void OPPROTO op_addmeo_64 (void)
{
    do_addmeo();
    RETURN();
}

/* add to zero extended */
void OPPROTO op_add_ze (void)
{
    T0 += xer_ca;
    RETURN();
}

/* divide word */
void OPPROTO op_divw (void)
{
    if (unlikely(((int32_t)T0 == INT32_MIN && (int32_t)T1 == (int32_t)-1) ||
                 (int32_t)T1 == 0)) {
        T0 = (int32_t)(UINT32_MAX * ((uint32_t)T0 >> 31));
    } else {
        T0 = (int32_t)T0 / (int32_t)T1;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_divd (void)
{
    if (unlikely(((int64_t)T0 == INT64_MIN && (int64_t)T1 == (int64_t)-1LL) ||
                 (int64_t)T1 == 0)) {
        T0 = (int64_t)(UINT64_MAX * ((uint64_t)T0 >> 63));
    } else {
        T0 = (int64_t)T0 / (int64_t)T1;
    }
    RETURN();
}
#endif

void OPPROTO op_divwo (void)
{
    do_divwo();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_divdo (void)
{
    do_divdo();
    RETURN();
}
#endif

/* divide word unsigned */
void OPPROTO op_divwu (void)
{
    if (unlikely(T1 == 0)) {
        T0 = 0;
    } else {
        T0 = (uint32_t)T0 / (uint32_t)T1;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_divdu (void)
{
    if (unlikely(T1 == 0)) {
        T0 = 0;
    } else {
        T0 /= T1;
    }
    RETURN();
}
#endif

void OPPROTO op_divwuo (void)
{
    do_divwuo();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_divduo (void)
{
    do_divduo();
    RETURN();
}
#endif

/* multiply high word */
void OPPROTO op_mulhw (void)
{
    T0 = ((int64_t)((int32_t)T0) * (int64_t)((int32_t)T1)) >> 32;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_mulhd (void)
{
    uint64_t tl, th;

    muls64(&tl, &th, T0, T1);
    T0 = th;
    RETURN();
}
#endif

/* multiply high word unsigned */
void OPPROTO op_mulhwu (void)
{
    T0 = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1) >> 32;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_mulhdu (void)
{
    uint64_t tl, th;

    mulu64(&tl, &th, T0, T1);
    T0 = th;
    RETURN();
}
#endif

/* multiply low immediate */
void OPPROTO op_mulli (void)
{
    T0 = ((int32_t)T0 * (int32_t)PARAM1);
    RETURN();
}

/* multiply low word */
void OPPROTO op_mullw (void)
{
    T0 = (int32_t)(T0 * T1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_mulld (void)
{
    T0 *= T1;
    RETURN();
}
#endif

void OPPROTO op_mullwo (void)
{
    do_mullwo();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_mulldo (void)
{
    do_mulldo();
    RETURN();
}
#endif

/* negate */
void OPPROTO op_neg (void)
{
    if (likely(T0 != INT32_MIN)) {
        T0 = -(int32_t)T0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_neg_64 (void)
{
    if (likely(T0 != INT64_MIN)) {
        T0 = -(int64_t)T0;
    }
    RETURN();
}
#endif

void OPPROTO op_nego (void)
{
    do_nego();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_nego_64 (void)
{
    do_nego_64();
    RETURN();
}
#endif

/* subtract from */
void OPPROTO op_subf (void)
{
    T0 = T1 - T0;
    RETURN();
}

/* subtract from carrying */
void OPPROTO op_check_subfc (void)
{
    if (likely((uint32_t)T0 > (uint32_t)T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_check_subfc_64 (void)
{
    if (likely((uint64_t)T0 > (uint64_t)T1)) {
        xer_ca = 0;
    } else {
        xer_ca = 1;
    }
    RETURN();
}
#endif

/* subtract from extended */
void OPPROTO op_subfe (void)
{
    do_subfe();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_subfe_64 (void)
{
    do_subfe_64();
    RETURN();
}
#endif

/* subtract from immediate carrying */
void OPPROTO op_subfic (void)
{
    T0 = (int32_t)PARAM1 + ~T0 + 1;
    if ((uint32_t)T0 <= (uint32_t)PARAM1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_subfic_64 (void)
{
    T0 = (int64_t)PARAM1 + ~T0 + 1;
    if ((uint64_t)T0 <= (uint64_t)PARAM1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}
#endif

/* subtract from minus one extended */
void OPPROTO op_subfme (void)
{
    T0 = ~T0 + xer_ca - 1;
    if (likely((uint32_t)T0 != UINT32_MAX))
        xer_ca = 1;
    else
        xer_ca = 0;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_subfme_64 (void)
{
    T0 = ~T0 + xer_ca - 1;
    if (likely((uint64_t)T0 != UINT64_MAX))
        xer_ca = 1;
    else
        xer_ca = 0;
    RETURN();
}
#endif

void OPPROTO op_subfmeo (void)
{
    do_subfmeo();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_subfmeo_64 (void)
{
    do_subfmeo_64();
    RETURN();
}
#endif

/* subtract from zero extended */
void OPPROTO op_subfze (void)
{
    T1 = ~T0;
    T0 = T1 + xer_ca;
    if ((uint32_t)T0 < (uint32_t)T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_subfze_64 (void)
{
    T1 = ~T0;
    T0 = T1 + xer_ca;
    if ((uint64_t)T0 < (uint64_t)T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}
#endif

void OPPROTO op_subfzeo (void)
{
    do_subfzeo();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_subfzeo_64 (void)
{
    do_subfzeo_64();
    RETURN();
}
#endif

/***                           Integer comparison                          ***/
/* compare */
void OPPROTO op_cmp (void)
{
    if ((int32_t)T0 < (int32_t)T1) {
        T0 = 0x08;
    } else if ((int32_t)T0 > (int32_t)T1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_cmp_64 (void)
{
    if ((int64_t)T0 < (int64_t)T1) {
        T0 = 0x08;
    } else if ((int64_t)T0 > (int64_t)T1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}
#endif

/* compare immediate */
void OPPROTO op_cmpi (void)
{
    if ((int32_t)T0 < (int32_t)PARAM1) {
        T0 = 0x08;
    } else if ((int32_t)T0 > (int32_t)PARAM1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_cmpi_64 (void)
{
    if ((int64_t)T0 < (int64_t)((int32_t)PARAM1)) {
        T0 = 0x08;
    } else if ((int64_t)T0 > (int64_t)((int32_t)PARAM1)) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}
#endif

/* compare logical */
void OPPROTO op_cmpl (void)
{
    if ((uint32_t)T0 < (uint32_t)T1) {
        T0 = 0x08;
    } else if ((uint32_t)T0 > (uint32_t)T1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_cmpl_64 (void)
{
    if ((uint64_t)T0 < (uint64_t)T1) {
        T0 = 0x08;
    } else if ((uint64_t)T0 > (uint64_t)T1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}
#endif

/* compare logical immediate */
void OPPROTO op_cmpli (void)
{
    if ((uint32_t)T0 < (uint32_t)PARAM1) {
        T0 = 0x08;
    } else if ((uint32_t)T0 > (uint32_t)PARAM1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_cmpli_64 (void)
{
    if ((uint64_t)T0 < (uint64_t)PARAM1) {
        T0 = 0x08;
    } else if ((uint64_t)T0 > (uint64_t)PARAM1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    T0 |= xer_so;
    RETURN();
}
#endif

void OPPROTO op_isel (void)
{
    if (T0)
        T0 = T1;
    else
        T0 = T2;
    RETURN();
}

void OPPROTO op_popcntb (void)
{
    do_popcntb();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_popcntb_64 (void)
{
    do_popcntb_64();
    RETURN();
}
#endif

/***                            Integer logical                            ***/
/* and */
void OPPROTO op_and (void)
{
    T0 &= T1;
    RETURN();
}

/* andc */
void OPPROTO op_andc (void)
{
    T0 &= ~T1;
    RETURN();
}

/* andi. */
void OPPROTO op_andi_T0 (void)
{
    T0 &= (uint32_t)PARAM1;
    RETURN();
}

void OPPROTO op_andi_T1 (void)
{
    T1 &= (uint32_t)PARAM1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_andi_T0_64 (void)
{
    T0 &= ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    RETURN();
}

void OPPROTO op_andi_T1_64 (void)
{
    T1 &= ((uint64_t)PARAM1 << 32) | (uint64_t)PARAM2;
    RETURN();
}
#endif

/* count leading zero */
void OPPROTO op_cntlzw (void)
{
    do_cntlzw();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_cntlzd (void)
{
    do_cntlzd();
    RETURN();
}
#endif

/* eqv */
void OPPROTO op_eqv (void)
{
    T0 = ~(T0 ^ T1);
    RETURN();
}

/* extend sign byte */
void OPPROTO op_extsb (void)
{
#if defined (TARGET_PPC64)
    T0 = (int64_t)((int8_t)T0);
#else
    T0 = (int32_t)((int8_t)T0);
#endif
    RETURN();
}

/* extend sign half word */
void OPPROTO op_extsh (void)
{
#if defined (TARGET_PPC64)
    T0 = (int64_t)((int16_t)T0);
#else
    T0 = (int32_t)((int16_t)T0);
#endif
    RETURN();
}

#if defined (TARGET_PPC64)
void OPPROTO op_extsw (void)
{
    T0 = (int64_t)((int32_t)T0);
    RETURN();
}
#endif

/* nand */
void OPPROTO op_nand (void)
{
    T0 = ~(T0 & T1);
    RETURN();
}

/* nor */
void OPPROTO op_nor (void)
{
    T0 = ~(T0 | T1);
    RETURN();
}

/* or */
void OPPROTO op_or (void)
{
    T0 |= T1;
    RETURN();
}

/* orc */
void OPPROTO op_orc (void)
{
    T0 |= ~T1;
    RETURN();
}

/* ori */
void OPPROTO op_ori (void)
{
    T0 |= (uint32_t)PARAM1;
    RETURN();
}

/* xor */
void OPPROTO op_xor (void)
{
    T0 ^= T1;
    RETURN();
}

/* xori */
void OPPROTO op_xori (void)
{
    T0 ^= (uint32_t)PARAM1;
    RETURN();
}

/***                             Integer rotate                            ***/
void OPPROTO op_rotl32_T0_T1 (void)
{
    T0 = rotl32(T0, T1 & 0x1F);
    RETURN();
}

void OPPROTO op_rotli32_T0 (void)
{
    T0 = rotl32(T0, PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_rotl64_T0_T1 (void)
{
    T0 = rotl64(T0, T1 & 0x3F);
    RETURN();
}

void OPPROTO op_rotli64_T0 (void)
{
    T0 = rotl64(T0, PARAM1);
    RETURN();
}
#endif

/***                             Integer shift                             ***/
/* shift left word */
void OPPROTO op_slw (void)
{
    if (T1 & 0x20) {
        T0 = 0;
    } else {
        T0 = (uint32_t)(T0 << T1);
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_sld (void)
{
    if (T1 & 0x40) {
        T0 = 0;
    } else {
        T0 = T0 << T1;
    }
    RETURN();
}
#endif

/* shift right algebraic word */
void OPPROTO op_sraw (void)
{
    do_sraw();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_srad (void)
{
    do_srad();
    RETURN();
}
#endif

/* shift right algebraic word immediate */
void OPPROTO op_srawi (void)
{
    uint32_t mask = (uint32_t)PARAM2;

    T0 = (int32_t)T0 >> PARAM1;
    if ((int32_t)T1 < 0 && (T1 & mask) != 0) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_sradi (void)
{
    uint64_t mask = ((uint64_t)PARAM2 << 32) | (uint64_t)PARAM3;

    T0 = (int64_t)T0 >> PARAM1;
    if ((int64_t)T1 < 0 && ((uint64_t)T1 & mask) != 0) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}
#endif

/* shift right word */
void OPPROTO op_srw (void)
{
    if (T1 & 0x20) {
        T0 = 0;
    } else {
        T0 = (uint32_t)T0 >> T1;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_srd (void)
{
    if (T1 & 0x40) {
        T0 = 0;
    } else {
        T0 = (uint64_t)T0 >> T1;
    }
    RETURN();
}
#endif

void OPPROTO op_sl_T0_T1 (void)
{
    T0 = T0 << T1;
    RETURN();
}

void OPPROTO op_sli_T0 (void)
{
    T0 = T0 << PARAM1;
    RETURN();
}

void OPPROTO op_sli_T1 (void)
{
    T1 = T1 << PARAM1;
    RETURN();
}

void OPPROTO op_srl_T0_T1 (void)
{
    T0 = (uint32_t)T0 >> T1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_srl_T0_T1_64 (void)
{
    T0 = (uint32_t)T0 >> T1;
    RETURN();
}
#endif

void OPPROTO op_srli_T0 (void)
{
    T0 = (uint32_t)T0 >> PARAM1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_srli_T0_64 (void)
{
    T0 = (uint64_t)T0 >> PARAM1;
    RETURN();
}
#endif

void OPPROTO op_srli_T1 (void)
{
    T1 = (uint32_t)T1 >> PARAM1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_srli_T1_64 (void)
{
    T1 = (uint64_t)T1 >> PARAM1;
    RETURN();
}
#endif

/***                       Floating-Point arithmetic                       ***/
/* fadd - fadd. */
void OPPROTO op_fadd (void)
{
#if USE_PRECISE_EMULATION
    do_fadd();
#else
    FT0 = float64_add(FT0, FT1, &env->fp_status);
#endif
    RETURN();
}

/* fsub - fsub. */
void OPPROTO op_fsub (void)
{
#if USE_PRECISE_EMULATION
    do_fsub();
#else
    FT0 = float64_sub(FT0, FT1, &env->fp_status);
#endif
    RETURN();
}

/* fmul - fmul. */
void OPPROTO op_fmul (void)
{
#if USE_PRECISE_EMULATION
    do_fmul();
#else
    FT0 = float64_mul(FT0, FT1, &env->fp_status);
#endif
    RETURN();
}

/* fdiv - fdiv. */
void OPPROTO op_fdiv (void)
{
#if USE_PRECISE_EMULATION
    do_fdiv();
#else
    FT0 = float64_div(FT0, FT1, &env->fp_status);
#endif
    RETURN();
}

/* fsqrt - fsqrt. */
void OPPROTO op_fsqrt (void)
{
    do_fsqrt();
    RETURN();
}

/* fre - fre. */
void OPPROTO op_fre (void)
{
    do_fre();
    RETURN();
}

/* fres - fres. */
void OPPROTO op_fres (void)
{
    do_fres();
    RETURN();
}

/* frsqrte  - frsqrte. */
void OPPROTO op_frsqrte (void)
{
    do_frsqrte();
    RETURN();
}

/* fsel - fsel. */
void OPPROTO op_fsel (void)
{
    do_fsel();
    RETURN();
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd - fmadd. */
void OPPROTO op_fmadd (void)
{
#if USE_PRECISE_EMULATION
    do_fmadd();
#else
    FT0 = float64_mul(FT0, FT1, &env->fp_status);
    FT0 = float64_add(FT0, FT2, &env->fp_status);
#endif
    RETURN();
}

/* fmsub - fmsub. */
void OPPROTO op_fmsub (void)
{
#if USE_PRECISE_EMULATION
    do_fmsub();
#else
    FT0 = float64_mul(FT0, FT1, &env->fp_status);
    FT0 = float64_sub(FT0, FT2, &env->fp_status);
#endif
    RETURN();
}

/* fnmadd - fnmadd. - fnmadds - fnmadds. */
void OPPROTO op_fnmadd (void)
{
    do_fnmadd();
    RETURN();
}

/* fnmsub - fnmsub. */
void OPPROTO op_fnmsub (void)
{
    do_fnmsub();
    RETURN();
}

/***                     Floating-Point round & convert                    ***/
/* frsp - frsp. */
void OPPROTO op_frsp (void)
{
#if USE_PRECISE_EMULATION
    do_frsp();
#else
    FT0 = float64_to_float32(FT0, &env->fp_status);
#endif
    RETURN();
}

/* fctiw - fctiw. */
void OPPROTO op_fctiw (void)
{
    do_fctiw();
    RETURN();
}

/* fctiwz - fctiwz. */
void OPPROTO op_fctiwz (void)
{
    do_fctiwz();
    RETURN();
}

#if defined(TARGET_PPC64)
/* fcfid - fcfid. */
void OPPROTO op_fcfid (void)
{
    do_fcfid();
    RETURN();
}

/* fctid - fctid. */
void OPPROTO op_fctid (void)
{
    do_fctid();
    RETURN();
}

/* fctidz - fctidz. */
void OPPROTO op_fctidz (void)
{
    do_fctidz();
    RETURN();
}
#endif

void OPPROTO op_frin (void)
{
    do_frin();
    RETURN();
}

void OPPROTO op_friz (void)
{
    do_friz();
    RETURN();
}

void OPPROTO op_frip (void)
{
    do_frip();
    RETURN();
}

void OPPROTO op_frim (void)
{
    do_frim();
    RETURN();
}

/***                         Floating-Point compare                        ***/
/* fcmpu */
void OPPROTO op_fcmpu (void)
{
    do_fcmpu();
    RETURN();
}

/* fcmpo */
void OPPROTO op_fcmpo (void)
{
    do_fcmpo();
    RETURN();
}

/***                         Floating-point move                           ***/
/* fabs */
void OPPROTO op_fabs (void)
{
    FT0 = float64_abs(FT0);
    RETURN();
}

/* fnabs */
void OPPROTO op_fnabs (void)
{
    FT0 = float64_abs(FT0);
    FT0 = float64_chs(FT0);
    RETURN();
}

/* fneg */
void OPPROTO op_fneg (void)
{
    FT0 = float64_chs(FT0);
    RETURN();
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_helper.h"
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_helper.h"
#include "op_mem.h"
#define MEMSUFFIX _kernel
#include "op_helper.h"
#include "op_mem.h"
#define MEMSUFFIX _hypv
#include "op_helper.h"
#include "op_mem.h"
#endif

/* Special op to check and maybe clear reservation */
void OPPROTO op_check_reservation (void)
{
    if ((uint32_t)env->reserve == (uint32_t)(T0 & ~0x00000003))
        env->reserve = (target_ulong)-1ULL;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_check_reservation_64 (void)
{
    if ((uint64_t)env->reserve == (uint64_t)(T0 & ~0x00000003))
        env->reserve = (target_ulong)-1ULL;
    RETURN();
}
#endif

void OPPROTO op_wait (void)
{
    env->halted = 1;
    RETURN();
}

/* Return from interrupt */
#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_rfi (void)
{
    do_rfi();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_rfid (void)
{
    do_rfid();
    RETURN();
}

void OPPROTO op_hrfid (void)
{
    do_hrfid();
    RETURN();
}
#endif

/* Exception vectors */
void OPPROTO op_store_excp_prefix (void)
{
    T0 &= env->ivpr_mask;
    env->excp_prefix = T0;
    RETURN();
}

void OPPROTO op_store_excp_vector (void)
{
    T0 &= env->ivor_mask;
    env->excp_vectors[PARAM1] = T0;
    RETURN();
}
#endif

/* Trap word */
void OPPROTO op_tw (void)
{
    do_tw(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_td (void)
{
    do_td(PARAM1);
    RETURN();
}
#endif

#if !defined(CONFIG_USER_ONLY)
/* tlbia */
void OPPROTO op_tlbia (void)
{
    ppc_tlb_invalidate_all(env);
    RETURN();
}

/* tlbie */
void OPPROTO op_tlbie (void)
{
    ppc_tlb_invalidate_one(env, (uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO op_tlbie_64 (void)
{
    ppc_tlb_invalidate_one(env, T0);
    RETURN();
}
#endif

#if defined(TARGET_PPC64)
void OPPROTO op_slbia (void)
{
    ppc_slb_invalidate_all(env);
    RETURN();
}

void OPPROTO op_slbie (void)
{
    ppc_slb_invalidate_one(env, (uint32_t)T0);
    RETURN();
}

void OPPROTO op_slbie_64 (void)
{
    ppc_slb_invalidate_one(env, T0);
    RETURN();
}
#endif
#endif

#if !defined(CONFIG_USER_ONLY)
/* PowerPC 602/603/755 software TLB load instructions */
void OPPROTO op_6xx_tlbld (void)
{
    do_load_6xx_tlb(0);
    RETURN();
}

void OPPROTO op_6xx_tlbli (void)
{
    do_load_6xx_tlb(1);
    RETURN();
}

/* PowerPC 74xx software TLB load instructions */
void OPPROTO op_74xx_tlbld (void)
{
    do_load_74xx_tlb(0);
    RETURN();
}

void OPPROTO op_74xx_tlbli (void)
{
    do_load_74xx_tlb(1);
    RETURN();
}
#endif

/* 601 specific */
void OPPROTO op_load_601_rtcl (void)
{
    T0 = cpu_ppc601_load_rtcl(env);
    RETURN();
}

void OPPROTO op_load_601_rtcu (void)
{
    T0 = cpu_ppc601_load_rtcu(env);
    RETURN();
}

#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_store_601_rtcl (void)
{
    cpu_ppc601_store_rtcl(env, T0);
    RETURN();
}

void OPPROTO op_store_601_rtcu (void)
{
    cpu_ppc601_store_rtcu(env, T0);
    RETURN();
}

void OPPROTO op_store_hid0_601 (void)
{
    do_store_hid0_601();
    RETURN();
}

void OPPROTO op_load_601_bat (void)
{
    T0 = env->IBAT[PARAM1][PARAM2];
    RETURN();
}

void OPPROTO op_store_601_batl (void)
{
    do_store_ibatl_601(env, PARAM1, T0);
    RETURN();
}

void OPPROTO op_store_601_batu (void)
{
    do_store_ibatu_601(env, PARAM1, T0);
    RETURN();
}
#endif /* !defined(CONFIG_USER_ONLY) */

/* PowerPC 601 specific instructions (POWER bridge) */
/* XXX: those micro-ops need tests ! */
void OPPROTO op_POWER_abs (void)
{
    if ((int32_t)T0 == INT32_MIN)
        T0 = INT32_MAX;
    else if ((int32_t)T0 < 0)
        T0 = -T0;
    RETURN();
}

void OPPROTO op_POWER_abso (void)
{
    do_POWER_abso();
    RETURN();
}

void OPPROTO op_POWER_clcs (void)
{
    do_POWER_clcs();
    RETURN();
}

void OPPROTO op_POWER_div (void)
{
    do_POWER_div();
    RETURN();
}

void OPPROTO op_POWER_divo (void)
{
    do_POWER_divo();
    RETURN();
}

void OPPROTO op_POWER_divs (void)
{
    do_POWER_divs();
    RETURN();
}

void OPPROTO op_POWER_divso (void)
{
    do_POWER_divso();
    RETURN();
}

void OPPROTO op_POWER_doz (void)
{
    if ((int32_t)T1 > (int32_t)T0)
        T0 = T1 - T0;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_POWER_dozo (void)
{
    do_POWER_dozo();
    RETURN();
}

void OPPROTO op_load_xer_cmp (void)
{
    T2 = xer_cmp;
    RETURN();
}

void OPPROTO op_POWER_maskg (void)
{
    do_POWER_maskg();
    RETURN();
}

void OPPROTO op_POWER_maskir (void)
{
    T0 = (T0 & ~T2) | (T1 & T2);
    RETURN();
}

void OPPROTO op_POWER_mul (void)
{
    uint64_t tmp;

    tmp = (uint64_t)T0 * (uint64_t)T1;
    env->spr[SPR_MQ] = tmp >> 32;
    T0 = tmp;
    RETURN();
}

void OPPROTO op_POWER_mulo (void)
{
    do_POWER_mulo();
    RETURN();
}

void OPPROTO op_POWER_nabs (void)
{
    if (T0 > 0)
        T0 = -T0;
    RETURN();
}

void OPPROTO op_POWER_nabso (void)
{
    /* nabs never overflows */
    if (T0 > 0)
        T0 = -T0;
    xer_ov = 0;
    RETURN();
}

/* XXX: factorise POWER rotates... */
void OPPROTO op_POWER_rlmi (void)
{
    T0 = rotl32(T0, T2) & PARAM1;
    T0 |= T1 & (uint32_t)PARAM2;
    RETURN();
}

void OPPROTO op_POWER_rrib (void)
{
    T2 &= 0x1FUL;
    T0 = rotl32(T0 & INT32_MIN, T2);
    T0 |= T1 & ~rotl32(INT32_MIN, T2);
    RETURN();
}

void OPPROTO op_POWER_sle (void)
{
    T1 &= 0x1FUL;
    env->spr[SPR_MQ] = rotl32(T0, T1);
    T0 = T0 << T1;
    RETURN();
}

void OPPROTO op_POWER_sleq (void)
{
    uint32_t tmp = env->spr[SPR_MQ];

    T1 &= 0x1FUL;
    env->spr[SPR_MQ] = rotl32(T0, T1);
    T0 = T0 << T1;
    T0 |= tmp >> (32 - T1);
    RETURN();
}

void OPPROTO op_POWER_sllq (void)
{
    uint32_t msk = UINT32_MAX;

    msk = msk << (T1 & 0x1FUL);
    if (T1 & 0x20UL)
        msk = ~msk;
    T1 &= 0x1FUL;
    T0 = (T0 << T1) & msk;
    T0 |= env->spr[SPR_MQ] & ~msk;
    RETURN();
}

void OPPROTO op_POWER_slq (void)
{
    uint32_t msk = UINT32_MAX, tmp;

    msk = msk << (T1 & 0x1FUL);
    if (T1 & 0x20UL)
        msk = ~msk;
    T1 &= 0x1FUL;
    tmp = rotl32(T0, T1);
    T0 = tmp & msk;
    env->spr[SPR_MQ] = tmp;
    RETURN();
}

void OPPROTO op_POWER_sraq (void)
{
    env->spr[SPR_MQ] = rotl32(T0, 32 - (T1 & 0x1FUL));
    if (T1 & 0x20UL)
        T0 = UINT32_MAX;
    else
        T0 = (int32_t)T0 >> T1;
    RETURN();
}

void OPPROTO op_POWER_sre (void)
{
    T1 &= 0x1FUL;
    env->spr[SPR_MQ] = rotl32(T0, 32 - T1);
    T0 = (int32_t)T0 >> T1;
    RETURN();
}

void OPPROTO op_POWER_srea (void)
{
    T1 &= 0x1FUL;
    env->spr[SPR_MQ] = T0 >> T1;
    T0 = (int32_t)T0 >> T1;
    RETURN();
}

void OPPROTO op_POWER_sreq (void)
{
    uint32_t tmp;
    int32_t msk;

    T1 &= 0x1FUL;
    msk = INT32_MIN >> T1;
    tmp = env->spr[SPR_MQ];
    env->spr[SPR_MQ] = rotl32(T0, 32 - T1);
    T0 = T0 >> T1;
    T0 |= tmp & msk;
    RETURN();
}

void OPPROTO op_POWER_srlq (void)
{
    uint32_t tmp;
    int32_t msk;

    msk = INT32_MIN >> (T1 & 0x1FUL);
    if (T1 & 0x20UL)
        msk = ~msk;
    T1 &= 0x1FUL;
    tmp = env->spr[SPR_MQ];
    env->spr[SPR_MQ] = rotl32(T0, 32 - T1);
    T0 = T0 >> T1;
    T0 &= msk;
    T0 |= tmp & ~msk;
    RETURN();
}

void OPPROTO op_POWER_srq (void)
{
    T1 &= 0x1FUL;
    env->spr[SPR_MQ] = rotl32(T0, 32 - T1);
    T0 = T0 >> T1;
    RETURN();
}

/* POWER instructions not implemented in PowerPC 601 */
#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_POWER_mfsri (void)
{
    T1 = T0 >> 28;
    T0 = env->sr[T1];
    RETURN();
}

void OPPROTO op_POWER_rac (void)
{
    do_POWER_rac();
    RETURN();
}

void OPPROTO op_POWER_rfsvc (void)
{
    do_POWER_rfsvc();
    RETURN();
}
#endif

/* PowerPC 602 specific instruction */
#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_602_mfrom (void)
{
    do_op_602_mfrom();
    RETURN();
}
#endif

/* PowerPC 4xx specific micro-ops */
void OPPROTO op_405_add_T0_T2 (void)
{
    T0 = (int32_t)T0 + (int32_t)T2;
    RETURN();
}

void OPPROTO op_405_mulchw (void)
{
    T0 = ((int16_t)T0) * ((int16_t)(T1 >> 16));
    RETURN();
}

void OPPROTO op_405_mulchwu (void)
{
    T0 = ((uint16_t)T0) * ((uint16_t)(T1 >> 16));
    RETURN();
}

void OPPROTO op_405_mulhhw (void)
{
    T0 = ((int16_t)(T0 >> 16)) * ((int16_t)(T1 >> 16));
    RETURN();
}

void OPPROTO op_405_mulhhwu (void)
{
    T0 = ((uint16_t)(T0 >> 16)) * ((uint16_t)(T1 >> 16));
    RETURN();
}

void OPPROTO op_405_mullhw (void)
{
    T0 = ((int16_t)T0) * ((int16_t)T1);
    RETURN();
}

void OPPROTO op_405_mullhwu (void)
{
    T0 = ((uint16_t)T0) * ((uint16_t)T1);
    RETURN();
}

void OPPROTO op_405_check_sat (void)
{
    do_405_check_sat();
    RETURN();
}

void OPPROTO op_405_check_ovu (void)
{
    if (likely(T0 >= T2)) {
        xer_ov = 0;
    } else {
        xer_ov = 1;
        xer_so = 1;
    }
    RETURN();
}

void OPPROTO op_405_check_satu (void)
{
    if (unlikely(T0 < T2)) {
        /* Saturate result */
        T0 = UINT32_MAX;
    }
    RETURN();
}

void OPPROTO op_load_dcr (void)
{
    do_load_dcr();
    RETURN();
}

void OPPROTO op_store_dcr (void)
{
    do_store_dcr();
    RETURN();
}

#if !defined(CONFIG_USER_ONLY)
/* Return from critical interrupt :
 * same as rfi, except nip & MSR are loaded from SRR2/3 instead of SRR0/1
 */
void OPPROTO op_40x_rfci (void)
{
    do_40x_rfci();
    RETURN();
}

void OPPROTO op_rfci (void)
{
    do_rfci();
    RETURN();
}

void OPPROTO op_rfdi (void)
{
    do_rfdi();
    RETURN();
}

void OPPROTO op_rfmci (void)
{
    do_rfmci();
    RETURN();
}

void OPPROTO op_wrte (void)
{
    /* We don't call do_store_msr here as we won't trigger
     * any special case nor change hflags
     */
    T0 &= 1 << MSR_EE;
    env->msr &= ~(1 << MSR_EE);
    env->msr |= T0;
    RETURN();
}

void OPPROTO op_440_tlbre (void)
{
    do_440_tlbre(PARAM1);
    RETURN();
}

void OPPROTO op_440_tlbsx (void)
{
    T0 = ppcemb_tlb_search(env, T0, env->spr[SPR_440_MMUCR] & 0xFF);
    RETURN();
}

void OPPROTO op_4xx_tlbsx_check (void)
{
    int tmp;

    tmp = xer_so;
    if ((int)T0 != -1)
        tmp |= 0x02;
    env->crf[0] = tmp;
    RETURN();
}

void OPPROTO op_440_tlbwe (void)
{
    do_440_tlbwe(PARAM1);
    RETURN();
}

void OPPROTO op_4xx_tlbre_lo (void)
{
    do_4xx_tlbre_lo();
    RETURN();
}

void OPPROTO op_4xx_tlbre_hi (void)
{
    do_4xx_tlbre_hi();
    RETURN();
}

void OPPROTO op_4xx_tlbsx (void)
{
    T0 = ppcemb_tlb_search(env, T0, env->spr[SPR_40x_PID]);
    RETURN();
}

void OPPROTO op_4xx_tlbwe_lo (void)
{
    do_4xx_tlbwe_lo();
    RETURN();
}

void OPPROTO op_4xx_tlbwe_hi (void)
{
    do_4xx_tlbwe_hi();
    RETURN();
}
#endif

/* SPR micro-ops */
/* 440 specific */
void OPPROTO op_440_dlmzb (void)
{
    do_440_dlmzb();
    RETURN();
}

void OPPROTO op_440_dlmzb_update_Rc (void)
{
    if (T0 == 8)
        T0 = 0x2;
    else if (T0 < 4)
        T0 = 0x4;
    else
        T0 = 0x8;
    RETURN();
}

#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_store_pir (void)
{
    env->spr[SPR_PIR] = T0 & 0x0000000FUL;
    RETURN();
}

void OPPROTO op_load_403_pb (void)
{
    do_load_403_pb(PARAM1);
    RETURN();
}

void OPPROTO op_store_403_pb (void)
{
    do_store_403_pb(PARAM1);
    RETURN();
}

void OPPROTO op_load_40x_pit (void)
{
    T0 = load_40x_pit(env);
    RETURN();
}

void OPPROTO op_store_40x_pit (void)
{
    store_40x_pit(env, T0);
    RETURN();
}

void OPPROTO op_store_40x_dbcr0 (void)
{
    store_40x_dbcr0(env, T0);
    RETURN();
}

void OPPROTO op_store_40x_sler (void)
{
    store_40x_sler(env, T0);
    RETURN();
}

void OPPROTO op_store_booke_tcr (void)
{
    store_booke_tcr(env, T0);
    RETURN();
}

void OPPROTO op_store_booke_tsr (void)
{
    store_booke_tsr(env, T0);
    RETURN();
}
#endif /* !defined(CONFIG_USER_ONLY) */

/* SPE extension */
void OPPROTO op_splatw_T1_64 (void)
{
    T1_64 = (T1_64 << 32) | (T1_64 & 0x00000000FFFFFFFFULL);
    RETURN();
}

void OPPROTO op_splatwi_T0_64 (void)
{
    uint64_t tmp = PARAM1;

    T0_64 = (tmp << 32) | tmp;
    RETURN();
}

void OPPROTO op_splatwi_T1_64 (void)
{
    uint64_t tmp = PARAM1;

    T1_64 = (tmp << 32) | tmp;
    RETURN();
}

void OPPROTO op_extsh_T1_64 (void)
{
    T1_64 = (int32_t)((int16_t)T1_64);
    RETURN();
}

void OPPROTO op_sli16_T1_64 (void)
{
    T1_64 = T1_64 << 16;
    RETURN();
}

void OPPROTO op_sli32_T1_64 (void)
{
    T1_64 = T1_64 << 32;
    RETURN();
}

void OPPROTO op_srli32_T1_64 (void)
{
    T1_64 = T1_64 >> 32;
    RETURN();
}

void OPPROTO op_evsel (void)
{
    do_evsel();
    RETURN();
}

void OPPROTO op_evaddw (void)
{
    do_evaddw();
    RETURN();
}

void OPPROTO op_evsubfw (void)
{
    do_evsubfw();
    RETURN();
}

void OPPROTO op_evneg (void)
{
    do_evneg();
    RETURN();
}

void OPPROTO op_evabs (void)
{
    do_evabs();
    RETURN();
}

void OPPROTO op_evextsh (void)
{
    T0_64 = ((uint64_t)((int32_t)(int16_t)(T0_64 >> 32)) << 32) |
        (uint64_t)((int32_t)(int16_t)T0_64);
    RETURN();
}

void OPPROTO op_evextsb (void)
{
    T0_64 = ((uint64_t)((int32_t)(int8_t)(T0_64 >> 32)) << 32) |
        (uint64_t)((int32_t)(int8_t)T0_64);
    RETURN();
}

void OPPROTO op_evcntlzw (void)
{
    do_evcntlzw();
    RETURN();
}

void OPPROTO op_evrndw (void)
{
    do_evrndw();
    RETURN();
}

void OPPROTO op_brinc (void)
{
    do_brinc();
    RETURN();
}

void OPPROTO op_evcntlsw (void)
{
    do_evcntlsw();
    RETURN();
}

void OPPROTO op_evand (void)
{
    T0_64 &= T1_64;
    RETURN();
}

void OPPROTO op_evandc (void)
{
    T0_64 &= ~T1_64;
    RETURN();
}

void OPPROTO op_evor (void)
{
    T0_64 |= T1_64;
    RETURN();
}

void OPPROTO op_evxor (void)
{
    T0_64 ^= T1_64;
    RETURN();
}

void OPPROTO op_eveqv (void)
{
    T0_64 = ~(T0_64 ^ T1_64);
    RETURN();
}

void OPPROTO op_evnor (void)
{
    T0_64 = ~(T0_64 | T1_64);
    RETURN();
}

void OPPROTO op_evorc (void)
{
    T0_64 |= ~T1_64;
    RETURN();
}

void OPPROTO op_evnand (void)
{
    T0_64 = ~(T0_64 & T1_64);
    RETURN();
}

void OPPROTO op_evsrws (void)
{
    do_evsrws();
    RETURN();
}

void OPPROTO op_evsrwu (void)
{
    do_evsrwu();
    RETURN();
}

void OPPROTO op_evslw (void)
{
    do_evslw();
    RETURN();
}

void OPPROTO op_evrlw (void)
{
    do_evrlw();
    RETURN();
}

void OPPROTO op_evmergelo (void)
{
    T0_64 = (T0_64 << 32) | (T1_64 & 0x00000000FFFFFFFFULL);
    RETURN();
}

void OPPROTO op_evmergehi (void)
{
    T0_64 = (T0_64 & 0xFFFFFFFF00000000ULL) | (T1_64 >> 32);
    RETURN();
}

void OPPROTO op_evmergelohi (void)
{
    T0_64 = (T0_64 << 32) | (T1_64 >> 32);
    RETURN();
}

void OPPROTO op_evmergehilo (void)
{
    T0_64 = (T0_64 & 0xFFFFFFFF00000000ULL) | (T1_64 & 0x00000000FFFFFFFFULL);
    RETURN();
}

void OPPROTO op_evcmpgts (void)
{
    do_evcmpgts();
    RETURN();
}

void OPPROTO op_evcmpgtu (void)
{
    do_evcmpgtu();
    RETURN();
}

void OPPROTO op_evcmplts (void)
{
    do_evcmplts();
    RETURN();
}

void OPPROTO op_evcmpltu (void)
{
    do_evcmpltu();
    RETURN();
}

void OPPROTO op_evcmpeq (void)
{
    do_evcmpeq();
    RETURN();
}

void OPPROTO op_evfssub (void)
{
    do_evfssub();
    RETURN();
}

void OPPROTO op_evfsadd (void)
{
    do_evfsadd();
    RETURN();
}

void OPPROTO op_evfsnabs (void)
{
    do_evfsnabs();
    RETURN();
}

void OPPROTO op_evfsabs (void)
{
    do_evfsabs();
    RETURN();
}

void OPPROTO op_evfsneg (void)
{
    do_evfsneg();
    RETURN();
}

void OPPROTO op_evfsdiv (void)
{
    do_evfsdiv();
    RETURN();
}

void OPPROTO op_evfsmul (void)
{
    do_evfsmul();
    RETURN();
}

void OPPROTO op_evfscmplt (void)
{
    do_evfscmplt();
    RETURN();
}

void OPPROTO op_evfscmpgt (void)
{
    do_evfscmpgt();
    RETURN();
}

void OPPROTO op_evfscmpeq (void)
{
    do_evfscmpeq();
    RETURN();
}

void OPPROTO op_evfscfsi (void)
{
    do_evfscfsi();
    RETURN();
}

void OPPROTO op_evfscfui (void)
{
    do_evfscfui();
    RETURN();
}

void OPPROTO op_evfscfsf (void)
{
    do_evfscfsf();
    RETURN();
}

void OPPROTO op_evfscfuf (void)
{
    do_evfscfuf();
    RETURN();
}

void OPPROTO op_evfsctsi (void)
{
    do_evfsctsi();
    RETURN();
}

void OPPROTO op_evfsctui (void)
{
    do_evfsctui();
    RETURN();
}

void OPPROTO op_evfsctsf (void)
{
    do_evfsctsf();
    RETURN();
}

void OPPROTO op_evfsctuf (void)
{
    do_evfsctuf();
    RETURN();
}

void OPPROTO op_evfsctuiz (void)
{
    do_evfsctuiz();
    RETURN();
}

void OPPROTO op_evfsctsiz (void)
{
    do_evfsctsiz();
    RETURN();
}

void OPPROTO op_evfststlt (void)
{
    do_evfststlt();
    RETURN();
}

void OPPROTO op_evfststgt (void)
{
    do_evfststgt();
    RETURN();
}

void OPPROTO op_evfststeq (void)
{
    do_evfststeq();
    RETURN();
}

void OPPROTO op_efssub (void)
{
    T0_64 = _do_efssub(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efsadd (void)
{
    T0_64 = _do_efsadd(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efsnabs (void)
{
    T0_64 = _do_efsnabs(T0_64);
    RETURN();
}

void OPPROTO op_efsabs (void)
{
    T0_64 = _do_efsabs(T0_64);
    RETURN();
}

void OPPROTO op_efsneg (void)
{
    T0_64 = _do_efsneg(T0_64);
    RETURN();
}

void OPPROTO op_efsdiv (void)
{
    T0_64 = _do_efsdiv(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efsmul (void)
{
    T0_64 = _do_efsmul(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efscmplt (void)
{
    do_efscmplt();
    RETURN();
}

void OPPROTO op_efscmpgt (void)
{
    do_efscmpgt();
    RETURN();
}

void OPPROTO op_efscfd (void)
{
    do_efscfd();
    RETURN();
}

void OPPROTO op_efscmpeq (void)
{
    do_efscmpeq();
    RETURN();
}

void OPPROTO op_efscfsi (void)
{
    do_efscfsi();
    RETURN();
}

void OPPROTO op_efscfui (void)
{
    do_efscfui();
    RETURN();
}

void OPPROTO op_efscfsf (void)
{
    do_efscfsf();
    RETURN();
}

void OPPROTO op_efscfuf (void)
{
    do_efscfuf();
    RETURN();
}

void OPPROTO op_efsctsi (void)
{
    do_efsctsi();
    RETURN();
}

void OPPROTO op_efsctui (void)
{
    do_efsctui();
    RETURN();
}

void OPPROTO op_efsctsf (void)
{
    do_efsctsf();
    RETURN();
}

void OPPROTO op_efsctuf (void)
{
    do_efsctuf();
    RETURN();
}

void OPPROTO op_efsctsiz (void)
{
    do_efsctsiz();
    RETURN();
}

void OPPROTO op_efsctuiz (void)
{
    do_efsctuiz();
    RETURN();
}

void OPPROTO op_efststlt (void)
{
    T0 = _do_efststlt(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efststgt (void)
{
    T0 = _do_efststgt(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efststeq (void)
{
    T0 = _do_efststeq(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efdsub (void)
{
    CPU_DoubleU u1, u2;
    u1.ll = T0_64;
    u2.ll = T1_64;
    u1.d = float64_sub(u1.d, u2.d, &env->spe_status);
    T0_64 = u1.ll;
    RETURN();
}

void OPPROTO op_efdadd (void)
{
    CPU_DoubleU u1, u2;
    u1.ll = T0_64;
    u2.ll = T1_64;
    u1.d = float64_add(u1.d, u2.d, &env->spe_status);
    T0_64 = u1.ll;
    RETURN();
}

void OPPROTO op_efdcfsid (void)
{
    do_efdcfsi();
    RETURN();
}

void OPPROTO op_efdcfuid (void)
{
    do_efdcfui();
    RETURN();
}

void OPPROTO op_efdnabs (void)
{
    T0_64 |= 0x8000000000000000ULL;
    RETURN();
}

void OPPROTO op_efdabs (void)
{
    T0_64 &= ~0x8000000000000000ULL;
    RETURN();
}

void OPPROTO op_efdneg (void)
{
    T0_64 ^= 0x8000000000000000ULL;
    RETURN();
}

void OPPROTO op_efddiv (void)
{
    CPU_DoubleU u1, u2;
    u1.ll = T0_64;
    u2.ll = T1_64;
    u1.d = float64_div(u1.d, u2.d, &env->spe_status);
    T0_64 = u1.ll;
    RETURN();
}

void OPPROTO op_efdmul (void)
{
    CPU_DoubleU u1, u2;
    u1.ll = T0_64;
    u2.ll = T1_64;
    u1.d = float64_mul(u1.d, u2.d, &env->spe_status);
    T0_64 = u1.ll;
    RETURN();
}

void OPPROTO op_efdctsidz (void)
{
    do_efdctsiz();
    RETURN();
}

void OPPROTO op_efdctuidz (void)
{
    do_efdctuiz();
    RETURN();
}

void OPPROTO op_efdcmplt (void)
{
    do_efdcmplt();
    RETURN();
}

void OPPROTO op_efdcmpgt (void)
{
    do_efdcmpgt();
    RETURN();
}

void OPPROTO op_efdcfs (void)
{
    do_efdcfs();
    RETURN();
}

void OPPROTO op_efdcmpeq (void)
{
    do_efdcmpeq();
    RETURN();
}

void OPPROTO op_efdcfsi (void)
{
    do_efdcfsi();
    RETURN();
}

void OPPROTO op_efdcfui (void)
{
    do_efdcfui();
    RETURN();
}

void OPPROTO op_efdcfsf (void)
{
    do_efdcfsf();
    RETURN();
}

void OPPROTO op_efdcfuf (void)
{
    do_efdcfuf();
    RETURN();
}

void OPPROTO op_efdctsi (void)
{
    do_efdctsi();
    RETURN();
}

void OPPROTO op_efdctui (void)
{
    do_efdctui();
    RETURN();
}

void OPPROTO op_efdctsf (void)
{
    do_efdctsf();
    RETURN();
}

void OPPROTO op_efdctuf (void)
{
    do_efdctuf();
    RETURN();
}

void OPPROTO op_efdctuiz (void)
{
    do_efdctuiz();
    RETURN();
}

void OPPROTO op_efdctsiz (void)
{
    do_efdctsiz();
    RETURN();
}

void OPPROTO op_efdtstlt (void)
{
    T0 = _do_efdtstlt(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efdtstgt (void)
{
    T0 = _do_efdtstgt(T0_64, T1_64);
    RETURN();
}

void OPPROTO op_efdtsteq (void)
{
    T0 = _do_efdtsteq(T0_64, T1_64);
    RETURN();
}
