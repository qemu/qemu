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
#include "qemu-common.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "exec/cpu-common.h"
#include "exec/exec-all.h"
#include "translate-all.h"

void tb_flush(CPUState *cpu)
{
}

void tlb_set_dirty(CPUState *cpu, target_ulong vaddr)
{
}

void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end,
                                   int is_cpu_write_access)
{
}
