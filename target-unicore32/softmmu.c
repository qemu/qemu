/*
 * Softmmu related functions
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#ifdef CONFIG_USER_ONLY
#error This file only exist under softmmu circumstance
#endif

#include <cpu.h>

void switch_mode(CPUUniCore32State *env, int mode)
{
    cpu_abort(env, "%s not supported yet\n", __func__);
}

void do_interrupt(CPUUniCore32State *env)
{
    cpu_abort(env, "%s not supported yet\n", __func__);
}

int uc32_cpu_handle_mmu_fault(CPUUniCore32State *env, target_ulong address,
                              int access_type, int mmu_idx)
{
    cpu_abort(env, "%s not supported yet\n", __func__);
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUUniCore32State *env,
        target_ulong addr)
{
    cpu_abort(env, "%s not supported yet\n", __func__);
    return addr;
}
