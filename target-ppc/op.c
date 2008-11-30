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

/***                             Integer shift                             ***/
void OPPROTO op_srli_T1 (void)
{
    T1 = (uint32_t)T1 >> PARAM1;
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
    env->xer &= ~(1 << XER_OV);
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

/* PowerPC 4xx specific micro-ops */
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

