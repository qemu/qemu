/*
 * QEMU TCG support -- s390x specific function stubs.
 *
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "tcg_s390x.h"

void tcg_s390_tod_updated(CPUState *cs, run_on_cpu_data opaque)
{
}
void QEMU_NORETURN tcg_s390_program_interrupt(CPUS390XState *env,
                                              uint32_t code, uintptr_t ra)
{
    g_assert_not_reached();
}
void QEMU_NORETURN tcg_s390_data_exception(CPUS390XState *env, uint32_t dxc,
                                           uintptr_t ra)
{
    g_assert_not_reached();
}
