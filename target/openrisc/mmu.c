/*
 * OpenRISC MMU.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
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
#include "qemu-common.h"
#include "exec/gdbstub.h"
#include "qemu/host-utils.h"
#ifndef CONFIG_USER_ONLY
#include "hw/loader.h"
#endif

#ifndef CONFIG_USER_ONLY
static inline int get_phys_nommu(hwaddr *physical, int *prot,
                                 target_ulong address)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    return TLBRET_MATCH;
}

static int get_phys_code(OpenRISCCPU *cpu, hwaddr *physical, int *prot,
                         target_ulong address, int rw, bool supervisor)
{
    int vpn = address >> TARGET_PAGE_BITS;
    int idx = vpn & ITLB_MASK;
    int right = 0;
    uint32_t mr = cpu->env.tlb.itlb[idx].mr;
    uint32_t tr = cpu->env.tlb.itlb[idx].tr;

    if ((mr >> TARGET_PAGE_BITS) != vpn) {
        return TLBRET_NOMATCH;
    }
    if (!(mr & 1)) {
        return TLBRET_INVALID;
    }
    if (supervisor) {
        if (tr & SXE) {
            right |= PAGE_EXEC;
        }
    } else {
        if (tr & UXE) {
            right |= PAGE_EXEC;
        }
    }
    if ((rw & 2) && ((right & PAGE_EXEC) == 0)) {
        return TLBRET_BADADDR;
    }

    *physical = (tr & TARGET_PAGE_MASK) | (address & ~TARGET_PAGE_MASK);
    *prot = right;
    return TLBRET_MATCH;
}

static int get_phys_data(OpenRISCCPU *cpu, hwaddr *physical, int *prot,
                         target_ulong address, int rw, bool supervisor)
{
    int vpn = address >> TARGET_PAGE_BITS;
    int idx = vpn & DTLB_MASK;
    int right = 0;
    uint32_t mr = cpu->env.tlb.dtlb[idx].mr;
    uint32_t tr = cpu->env.tlb.dtlb[idx].tr;

    if ((mr >> TARGET_PAGE_BITS) != vpn) {
        return TLBRET_NOMATCH;
    }
    if (!(mr & 1)) {
        return TLBRET_INVALID;
    }
    if (supervisor) {
        if (tr & SRE) {
            right |= PAGE_READ;
        }
        if (tr & SWE) {
            right |= PAGE_WRITE;
        }
    } else {
        if (tr & URE) {
            right |= PAGE_READ;
        }
        if (tr & UWE) {
            right |= PAGE_WRITE;
        }
    }

    if (!(rw & 1) && ((right & PAGE_READ) == 0)) {
        return TLBRET_BADADDR;
    }
    if ((rw & 1) && ((right & PAGE_WRITE) == 0)) {
        return TLBRET_BADADDR;
    }

    *physical = (tr & TARGET_PAGE_MASK) | (address & ~TARGET_PAGE_MASK);
    *prot = right;
    return TLBRET_MATCH;
}

static int get_phys_addr(OpenRISCCPU *cpu, hwaddr *physical,
                         int *prot, target_ulong address, int rw)
{
    bool supervisor = (cpu->env.sr & SR_SM) != 0;
    int ret;

    /* Assume nommu results for a moment.  */
    ret = get_phys_nommu(physical, prot, address);

    /* Overwrite with TLB lookup if enabled.  */
    if (rw == MMU_INST_FETCH) {
        if (cpu->env.sr & SR_IME) {
            ret = get_phys_code(cpu, physical, prot, address, rw, supervisor);
        }
    } else {
        if (cpu->env.sr & SR_DME) {
            ret = get_phys_data(cpu, physical, prot, address, rw, supervisor);
        }
    }

    return ret;
}
#endif

static void cpu_openrisc_raise_mmu_exception(OpenRISCCPU *cpu,
                                             target_ulong address,
                                             int rw, int tlb_error)
{
    CPUState *cs = CPU(cpu);
    int exception = 0;

    switch (tlb_error) {
    default:
        if (rw == 2) {
            exception = EXCP_IPF;
        } else {
            exception = EXCP_DPF;
        }
        break;
#ifndef CONFIG_USER_ONLY
    case TLBRET_BADADDR:
        if (rw == 2) {
            exception = EXCP_IPF;
        } else {
            exception = EXCP_DPF;
        }
        break;
    case TLBRET_INVALID:
    case TLBRET_NOMATCH:
        /* No TLB match for a mapped address */
        if (rw == 2) {
            exception = EXCP_ITLBMISS;
        } else {
            exception = EXCP_DTLBMISS;
        }
        break;
#endif
    }

    cs->exception_index = exception;
    cpu->env.eear = address;
    cpu->env.lock_addr = -1;
}

#ifndef CONFIG_USER_ONLY
int openrisc_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int size,
                                  int rw, int mmu_idx)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    int ret = 0;
    hwaddr physical = 0;
    int prot = 0;

    ret = get_phys_addr(cpu, &physical, &prot, address, rw);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        ret = 0;
    } else if (ret < 0) {
        cpu_openrisc_raise_mmu_exception(cpu, address, rw, ret);
        ret = 1;
    }

    return ret;
}
#else
int openrisc_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int size,
                                  int rw, int mmu_idx)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    int ret = 0;

    cpu_openrisc_raise_mmu_exception(cpu, address, rw, ret);
    ret = 1;

    return ret;
}
#endif

#ifndef CONFIG_USER_ONLY
hwaddr openrisc_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    hwaddr phys_addr;
    int prot;
    int miss;

    /* Check memory for any kind of address, since during debug the
       gdb can ask for anything, check data tlb for address */
    miss = get_phys_addr(cpu, &phys_addr, &prot, addr, 0);

    /* Check instruction tlb */
    if (miss) {
        miss = get_phys_addr(cpu, &phys_addr, &prot, addr, MMU_INST_FETCH);
    }

    /* Last, fall back to a plain address */
    if (miss) {
        miss = get_phys_nommu(&phys_addr, &prot, addr);
    }

    if (miss) {
        return -1;
    } else {
        return phys_addr;
    }
}

void tlb_fill(CPUState *cs, target_ulong addr, int size,
              MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    int ret = openrisc_cpu_handle_mmu_fault(cs, addr, size,
                                            access_type, mmu_idx);
    if (ret) {
        /* Raise Exception.  */
        cpu_loop_exit_restore(cs, retaddr);
    }
}
#endif
