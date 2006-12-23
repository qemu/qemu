/*
 *  PowerPC emulation definitions for qemu.
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
#if !defined (__PPC_H__)
#define __PPC_H__

#include "config.h"

#include "dyngen-exec.h"

#define TARGET_LONG_BITS 32

register struct CPUPPCState *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
register uint32_t T2 asm(AREG3);

#define PARAM(n) ((uint32_t)PARAM##n)
#define SPARAM(n) ((int32_t)PARAM##n)
#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define FT2 (env->ft2)

#if defined (DEBUG_OP)
# define RETURN() __asm__ __volatile__("nop" : : : "memory");
#else
# define RETURN() __asm__ __volatile__("" : : : "memory");
#endif

#include "cpu.h"
#include "exec-all.h"

static inline uint32_t rotl (uint32_t i, int n)
{
    return ((i << n) | (i >> (32 - n)));
}

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

void do_raise_exception_err (uint32_t exception, int error_code);
void do_raise_exception (uint32_t exception);

void do_sraw(void);

void do_fctiw (void);
void do_fctiwz (void);
void do_fnmadd (void);
void do_fnmsub (void);
void do_fsqrt (void);
void do_fres (void);
void do_frsqrte (void);
void do_fsel (void);
void do_fcmpu (void);
void do_fcmpo (void);

void do_check_reservation (void);
void do_icbi (void);
void do_tlbia (void);
void do_tlbie (void);

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

int cpu_ppc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int is_user, int is_softmmu);

#endif /* !defined (__PPC_H__) */
