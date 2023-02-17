/*
 * QEMU ARM stubs for some TCG helper functions
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"

void write_v7m_exception(CPUARMState *env, uint32_t new_exc)
{
    g_assert_not_reached();
}

void raise_exception_ra(CPUARMState *env, uint32_t excp, uint32_t syndrome,
                        uint32_t target_el, uintptr_t ra)
{
    g_assert_not_reached();
}
/* Temporarily while cpu_get_tb_cpu_state() is still in common code */
void assert_hflags_rebuild_correctly(CPUARMState *env)
{
}
