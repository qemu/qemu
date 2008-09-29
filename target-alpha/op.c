/*
 *  Alpha emulation cpu micro-operations for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#define DEBUG_OP

#include "config.h"
#include "exec.h"
#include "host-utils.h"
#include "op_helper.h"

/* Load and stores */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _kernel
#include "op_mem.h"
#define MEMSUFFIX _executive
#include "op_mem.h"
#define MEMSUFFIX _supervisor
#include "op_mem.h"
#define MEMSUFFIX _user
#include "op_mem.h"
/* This is used for pal modes */
#define MEMSUFFIX _data
#include "op_mem.h"
#endif

/* PALcode support special instructions */
#if !defined (CONFIG_USER_ONLY)
void OPPROTO op_hw_rei (void)
{
    env->pc = env->ipr[IPR_EXC_ADDR] & ~3;
    env->ipr[IPR_EXC_ADDR] = env->ipr[IPR_EXC_ADDR] & 1;
    /* XXX: re-enable interrupts and memory mapping */
    RETURN();
}

void OPPROTO op_hw_ret (void)
{
    env->pc = T0 & ~3;
    env->ipr[IPR_EXC_ADDR] = T0 & 1;
    /* XXX: re-enable interrupts and memory mapping */
    RETURN();
}

void OPPROTO op_mfpr (void)
{
    helper_mfpr(PARAM(1));
    RETURN();
}

void OPPROTO op_mtpr (void)
{
    helper_mtpr(PARAM(1));
    RETURN();
}

void OPPROTO op_set_alt_mode (void)
{
    env->saved_mode = env->ps & 0xC;
    env->ps = (env->ps & ~0xC) | (env->ipr[IPR_ALT_MODE] & 0xC);
    RETURN();
}

void OPPROTO op_restore_mode (void)
{
    env->ps = (env->ps & ~0xC) | env->saved_mode;
    RETURN();
}

void OPPROTO op_ld_phys_to_virt (void)
{
    helper_ld_phys_to_virt();
    RETURN();
}

void OPPROTO op_st_phys_to_virt (void)
{
    helper_st_phys_to_virt();
    RETURN();
}
#endif /* !defined (CONFIG_USER_ONLY) */
