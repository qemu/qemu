/*
 *  ARM execution defines
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "dyngen-exec.h"

register struct CPUARMState *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
register uint32_t T2 asm(AREG3);

/* TODO: Put these in FP regs on targets that have such things.  */
/* It is ok for FT0s and FT0d to overlap.  Likewise FT1s and FT1d.  */
#define FT0s env->vfp.tmp0s
#define FT1s env->vfp.tmp1s
#define FT0d env->vfp.tmp0d
#define FT1d env->vfp.tmp1d

#include "cpu.h"
#include "exec-all.h"

/* Implemented CPSR bits.  */
#define CACHED_CPSR_BITS 0xf8000000
static inline int compute_cpsr(void)
{
    int ZF;
    ZF = (env->NZF == 0);
    return env->cpsr | (env->NZF & 0x80000000) | (ZF << 30) | 
        (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27);
}

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

int cpu_arm_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int is_user, int is_softmmu);

/* In op_helper.c */

void cpu_lock(void);
void cpu_unlock(void);
void cpu_loop_exit(void);

void raise_exception(int);

void do_vfp_abss(void);
void do_vfp_absd(void);
void do_vfp_negs(void);
void do_vfp_negd(void);
void do_vfp_sqrts(void);
void do_vfp_sqrtd(void);
void do_vfp_cmps(void);
void do_vfp_cmpd(void);
void do_vfp_cmpes(void);
void do_vfp_cmped(void);
void do_vfp_set_fpscr(void);
void do_vfp_get_fpscr(void);
