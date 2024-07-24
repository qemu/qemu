/*
 * ARM AArch64 specific CPU for bsd-user
 *
 * Copyright (c) 2015 Stacey Son
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
#include "target_arch.h"

/* See cpu_set_user_tls() in arm64/arm64/vm_machdep.c */
void target_cpu_set_tls(CPUARMState *env, target_ulong newtls)
{
    env->cp15.tpidr_el[0] = newtls;
}

target_ulong target_cpu_get_tls(CPUARMState *env)
{
    return env->cp15.tpidr_el[0];
}
