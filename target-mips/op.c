/*
 *  MIPS emulation micro-operations for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2007 Thiemo Seufer (64-bit FPU support)
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

#include "config.h"
#include "exec.h"
#include "host-utils.h"

#ifndef CALL_FROM_TB0
#define CALL_FROM_TB0(func) func()
#endif

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.c"
#undef MEMSUFFIX
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.c"
#undef MEMSUFFIX

#define MEMSUFFIX _super
#include "op_mem.c"
#undef MEMSUFFIX

#define MEMSUFFIX _kernel
#include "op_mem.c"
#undef MEMSUFFIX
#endif

/* 64 bits arithmetic */
#if TARGET_LONG_BITS > HOST_LONG_BITS
void op_madd (void)
{
    CALL_FROM_TB0(do_madd);
    FORCE_RET();
}

void op_maddu (void)
{
    CALL_FROM_TB0(do_maddu);
    FORCE_RET();
}

void op_msub (void)
{
    CALL_FROM_TB0(do_msub);
    FORCE_RET();
}

void op_msubu (void)
{
    CALL_FROM_TB0(do_msubu);
    FORCE_RET();
}

/* Multiplication variants of the vr54xx. */
void op_muls (void)
{
    CALL_FROM_TB0(do_muls);
    FORCE_RET();
}

void op_mulsu (void)
{
    CALL_FROM_TB0(do_mulsu);
    FORCE_RET();
}

void op_macc (void)
{
    CALL_FROM_TB0(do_macc);
    FORCE_RET();
}

void op_macchi (void)
{
    CALL_FROM_TB0(do_macchi);
    FORCE_RET();
}

void op_maccu (void)
{
    CALL_FROM_TB0(do_maccu);
    FORCE_RET();
}
void op_macchiu (void)
{
    CALL_FROM_TB0(do_macchiu);
    FORCE_RET();
}

void op_msac (void)
{
    CALL_FROM_TB0(do_msac);
    FORCE_RET();
}

void op_msachi (void)
{
    CALL_FROM_TB0(do_msachi);
    FORCE_RET();
}

void op_msacu (void)
{
    CALL_FROM_TB0(do_msacu);
    FORCE_RET();
}

void op_msachiu (void)
{
    CALL_FROM_TB0(do_msachiu);
    FORCE_RET();
}

void op_mulhi (void)
{
    CALL_FROM_TB0(do_mulhi);
    FORCE_RET();
}

void op_mulhiu (void)
{
    CALL_FROM_TB0(do_mulhiu);
    FORCE_RET();
}

void op_mulshi (void)
{
    CALL_FROM_TB0(do_mulshi);
    FORCE_RET();
}

void op_mulshiu (void)
{
    CALL_FROM_TB0(do_mulshiu);
    FORCE_RET();
}

#else /* TARGET_LONG_BITS > HOST_LONG_BITS */

static always_inline uint64_t get_HILO (void)
{
    return ((uint64_t)env->HI[env->current_tc][0] << 32) |
            ((uint64_t)(uint32_t)env->LO[env->current_tc][0]);
}

static always_inline void set_HILO (uint64_t HILO)
{
    env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

static always_inline void set_HIT0_LO (uint64_t HILO)
{
    env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    T0 = env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

static always_inline void set_HI_LOT0 (uint64_t HILO)
{
    T0 = env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

/* Multiplication variants of the vr54xx. */
void op_muls (void)
{
    set_HI_LOT0(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_mulsu (void)
{
    set_HI_LOT0(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_macc (void)
{
    set_HI_LOT0(get_HILO() + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_macchi (void)
{
    set_HIT0_LO(get_HILO() + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_maccu (void)
{
    set_HI_LOT0(get_HILO() + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_macchiu (void)
{
    set_HIT0_LO(get_HILO() + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_msac (void)
{
    set_HI_LOT0(get_HILO() - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_msachi (void)
{
    set_HIT0_LO(get_HILO() - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_msacu (void)
{
    set_HI_LOT0(get_HILO() - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_msachiu (void)
{
    set_HIT0_LO(get_HILO() - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_mulhi (void)
{
    set_HIT0_LO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    FORCE_RET();
}

void op_mulhiu (void)
{
    set_HIT0_LO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    FORCE_RET();
}

void op_mulshi (void)
{
    set_HIT0_LO(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_mulshiu (void)
{
    set_HIT0_LO(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */
