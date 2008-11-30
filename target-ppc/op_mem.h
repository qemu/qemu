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

#include "op_mem_access.h"

/* External access */
void OPPROTO glue(op_eciwx, MEMSUFFIX) (void)
{
    T1 = glue(ldu32, MEMSUFFIX)((uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_eciwx_64, MEMSUFFIX) (void)
{
    T1 = glue(ldu32, MEMSUFFIX)((uint64_t)T0);
    RETURN();
}
#endif

void OPPROTO glue(op_ecowx, MEMSUFFIX) (void)
{
    glue(st32, MEMSUFFIX)((uint32_t)T0, T1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_ecowx_64, MEMSUFFIX) (void)
{
    glue(st32, MEMSUFFIX)((uint64_t)T0, T1);
    RETURN();
}
#endif

void OPPROTO glue(op_eciwx_le, MEMSUFFIX) (void)
{
    T1 = glue(ldu32r, MEMSUFFIX)((uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_eciwx_le_64, MEMSUFFIX) (void)
{
    T1 = glue(ldu32r, MEMSUFFIX)((uint64_t)T0);
    RETURN();
}
#endif

void OPPROTO glue(op_ecowx_le, MEMSUFFIX) (void)
{
    glue(st32r, MEMSUFFIX)((uint32_t)T0, T1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_ecowx_le_64, MEMSUFFIX) (void)
{
    glue(st32r, MEMSUFFIX)((uint64_t)T0, T1);
    RETURN();
}
#endif

#undef MEMSUFFIX
