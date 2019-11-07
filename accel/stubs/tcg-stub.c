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
#include "cpu.h"
#include "tcg/tcg.h"
#include "exec/exec-all.h"

void tb_flush(CPUState *cpu)
{
}

void tlb_set_dirty(CPUState *cpu, target_ulong vaddr)
{
}
