/*
 *  HPPA memory access helper routines
 *
 *  Copyright (c) 2017 Helge Deller
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qom/cpu.h"

#ifdef CONFIG_USER_ONLY
int hppa_cpu_handle_mmu_fault(CPUState *cs, vaddr address,
                              int size, int rw, int mmu_idx)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    cs->exception_index = EXCP_SIGSEGV;
    cpu->env.ior = address;
    return 1;
}
#else
hwaddr hppa_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    /* Stub */
    return addr;
}

void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType type,
              int mmu_idx, uintptr_t retaddr)
{
    /* Stub */
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    hwaddr phys = addr;

    /* Success!  Store the translation into the QEMU TLB.  */
    tlb_set_page(cs, addr & TARGET_PAGE_MASK, phys & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE);
}
#endif /* CONFIG_USER_ONLY */
