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

#define RETURN() __asm__ __volatile__("");

#include "cpu.h"
#include "exec-all.h"

static inline uint8_t ld8 (uint32_t EA)
{
    return *((uint8_t *)EA);
}

static inline uint16_t ld16 (uint32_t EA)
{
    return __be16_to_cpu(*((uint16_t *)EA));
}

static inline uint16_t ld16r (uint32_t EA)
{
    return __le16_to_cpu(*((uint16_t *)EA));
}

static inline uint32_t ld32 (uint32_t EA)
{
    return __be32_to_cpu(*((uint32_t *)EA));
}

static inline uint32_t ld32r (uint32_t EA)
{
    return __le32_to_cpu(*((uint32_t *)EA));
}

static inline uint64_t ld64 (uint32_t EA)
{
    return __be64_to_cpu(*((uint64_t *)EA));
}

static inline uint64_t ld64r (uint32_t EA)
{
    return __le64_to_cpu(*((uint64_t *)EA));
}

static inline void st8 (uint32_t EA, uint8_t data)
{
    *((uint8_t *)EA) = data;
}

static inline void st16 (uint32_t EA, uint16_t data)
{
    *((uint16_t *)EA) = __cpu_to_be16(data);
}

static inline void st16r (uint32_t EA, uint16_t data)
{
    *((uint16_t *)EA) = __cpu_to_le16(data);
}

static inline void st32 (uint32_t EA, uint32_t data)
{
    *((uint32_t *)EA) = __cpu_to_be32(data);
}

static inline void st32r (uint32_t EA, uint32_t data)
{
    *((uint32_t *)EA) = __cpu_to_le32(data);
}

static inline void st64 (uint32_t EA, uint64_t data)
{
    *((uint64_t *)EA) = __cpu_to_be64(data);
}

static inline void st64r (uint32_t EA, uint64_t data)
{
    *((uint64_t *)EA) = __cpu_to_le64(data);
}

static inline void set_CRn(int n, uint8_t value)
{
    env->crf[n] = value;
}

static inline void set_carry (void)
{
    xer_ca = 1;
}

static inline void reset_carry (void)
{
    xer_ca = 0;
}

static inline void set_overflow (void)
{
    xer_so = 1;
    xer_ov = 1;
}

static inline void reset_overflow (void)
{
    xer_ov = 0;
}

static inline uint32_t rotl (uint32_t i, int n)
{
    return ((i << n) | (i >> (32 - n)));
}

void raise_exception (int exception_index);
void raise_exception_err (int exception_index, int error_code);

uint32_t do_load_cr (void);
void do_store_cr (uint32_t crn, uint32_t value);
uint32_t do_load_xer (void);
void do_store_xer (uint32_t value);
uint32_t do_load_msr (void);
void do_store_msr (uint32_t msr_value);
void do_load_fpscr (void);
void do_store_fpscr (uint32_t mask);

int32_t do_sraw(int32_t Ta, uint32_t Tb);
void do_lmw (int reg, uint32_t src);
void do_stmw (int reg, uint32_t dest);
void do_lsw (uint32_t reg, int count, uint32_t src);
void do_stsw (uint32_t reg, int count, uint32_t dest);

void do_dcbz (void);
void do_icbi (void);

#endif /* !defined (__PPC_H__) */
