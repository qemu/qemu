/*
 *  PPC emulation definitions for qemu.
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
#if !defined (__PPC_H__)
#define __PPC_H__

#include "dyngen-exec.h"

register struct CPUPPCState *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
register uint32_t T2 asm(AREG3);

#define PARAM(n) ((uint32_t)PARAM##n)
#define SPARAM(n) ((int32_t)PARAM##n)
#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define FT2 (env->ft2)
#define FTS0 ((float)env->ft0)
#define FTS1 ((float)env->ft1)
#define FTS2 ((float)env->ft2)

#if defined (DEBUG_OP)
#define RETURN() __asm__ __volatile__("nop");
#else
#define RETURN() __asm__ __volatile__("");
#endif

#include "cpu.h"
#include "exec-all.h"

static inline uint32_t rotl (uint32_t i, int n)
{
    return ((i << n) | (i >> (32 - n)));
}

/* XXX: move that to a generic header */
#if !defined(CONFIG_USER_ONLY)

#define ldul_user ldl_user
#define ldul_kernel ldl_kernel

#define ACCESS_TYPE 0
#define MEMSUFFIX _kernel
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ACCESS_TYPE 1
#define MEMSUFFIX _user
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

/* these access are slower, they must be as rare as possible */
#define ACCESS_TYPE 2
#define MEMSUFFIX _data
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ldub(p) ldub_data(p)
#define ldsb(p) ldsb_data(p)
#define lduw(p) lduw_data(p)
#define ldsw(p) ldsw_data(p)
#define ldl(p) ldl_data(p)
#define ldq(p) ldq_data(p)

#define stb(p, v) stb_data(p, v)
#define stw(p, v) stw_data(p, v)
#define stl(p, v) stl_data(p, v)
#define stq(p, v) stq_data(p, v)

#endif /* !defined(CONFIG_USER_ONLY) */

void do_raise_exception_err (uint32_t exception, int error_code);
void do_raise_exception (uint32_t exception);

void do_load_cr (void);
void do_store_cr (uint32_t mask);
void do_load_xer (void);
void do_store_xer (void);
void do_load_msr (void);
void do_store_msr (void);
void do_load_fpscr (void);
void do_store_fpscr (uint32_t mask);

void do_sraw(void);

void do_fctiw (void);
void do_fctiwz (void);
void do_fnmadd (void);
void do_fnmsub (void);
void do_fnmadds (void);
void do_fnmsubs (void);
void do_fsqrt (void);
void do_fsqrts (void);
void do_fres (void);
void do_fsqrte (void);
void do_fsel (void);
void do_fcmpu (void);
void do_fcmpo (void);
void do_fabs (void);
void do_fnabs (void);

void do_check_reservation (void);
void do_icbi (void);
void do_store_sr (uint32_t srnum);
void do_store_ibat (int ul, int nr);
void do_store_dbat (int ul, int nr);
void do_tlbia (void);
void do_tlbie (void);

void dump_state (void);
void dump_rfi (void);
void dump_store_sr (int srnum);
void dump_store_ibat (int ul, int nr);
void dump_store_dbat (int ul, int nr);
void dump_store_tb (int ul);
void dump_update_tb(uint32_t param);

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

#endif /* !defined (__PPC_H__) */
