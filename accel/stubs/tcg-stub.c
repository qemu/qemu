/*
 * QEMU TCG accelerator stub
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "exec/tb-flush.h"
#include "exec/exec-all.h"

G_NORETURN void cpu_loop_exit(CPUState *cpu)
{
    g_assert_not_reached();
}

G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    g_assert_not_reached();
}
