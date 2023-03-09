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

void tb_flush(CPUState *cpu)
{
}

void tlb_set_dirty(CPUState *cpu, target_ulong vaddr)
{
}

void tcg_flush_jmp_cache(CPUState *cpu)
{
}

int probe_access_flags(CPUArchState *env, target_ulong addr, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t retaddr)
{
     g_assert_not_reached();
}

void *probe_access(CPUArchState *env, target_ulong addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
     /* Handled by hardware accelerator. */
     g_assert_not_reached();
}

G_NORETURN void cpu_loop_exit(CPUState *cpu)
{
    g_assert_not_reached();
}

G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    g_assert_not_reached();
}
