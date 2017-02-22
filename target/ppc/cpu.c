/*
 *  PowerPC CPU routines for qemu.
 *
 * Copyright (c) 2017 Nikunj A Dadhania, IBM Corporation.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu-models.h"

target_ulong cpu_read_xer(CPUPPCState *env)
{
    return env->xer | (env->so << XER_SO) | (env->ov << XER_OV) |
        (env->ca << XER_CA);
}

void cpu_write_xer(CPUPPCState *env, target_ulong xer)
{
    env->so = (xer >> XER_SO) & 1;
    env->ov = (xer >> XER_OV) & 1;
    env->ca = (xer >> XER_CA) & 1;
    env->xer = xer & ~((1u << XER_SO) | (1u << XER_OV) | (1u << XER_CA));
}
