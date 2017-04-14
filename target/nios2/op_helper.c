/*
 * Altera Nios II helper routines.
 *
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "qemu/main-loop.h"

#if !defined(CONFIG_USER_ONLY)
void helper_mmu_read_debug(CPUNios2State *env, uint32_t rn)
{
    mmu_read_debug(env, rn);
}

void helper_mmu_write(CPUNios2State *env, uint32_t rn, uint32_t v)
{
    mmu_write(env, rn, v);
}

void helper_check_interrupts(CPUNios2State *env)
{
    qemu_mutex_lock_iothread();
    nios2_check_interrupts(env);
    qemu_mutex_unlock_iothread();
}
#endif /* !CONFIG_USER_ONLY */

void helper_raise_exception(CPUNios2State *env, uint32_t index)
{
    CPUState *cs = ENV_GET_CPU(env);
    cs->exception_index = index;
    cpu_loop_exit(cs);
}
