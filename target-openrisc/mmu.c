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
#include "qemu-common.h"
#include "exec/gdbstub.h"
#include "qemu/host-utils.h"
#ifndef CONFIG_USER_ONLY
#include "hw/loader.h"
#endif

#ifndef CONFIG_USER_ONLY
int cpu_openrisc_get_phys_nommu(OpenRISCCPU *cpu,
                                hwaddr *physical,
                                int *prot, target_ulong address, int rw)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    return TLBRET_MATCH;
}

int cpu_openrisc_get_phys_code(OpenRISCCPU *cpu,
                               hwaddr *physical,
                               int *prot, target_ulong address, int rw)
{
    int vpn = address >> TARGET_PAGE_BITS;
    int idx = vpn & ITLB_MASK;
    int right = 0;

    if ((cpu->env.tlb->itlb[0][idx].mr >> TARGET_PAGE_BITS) != vpn) {
        return TLBRET_NOMATCH;
    }
    if (!(cpu->env.tlb->itlb[0][idx].mr & 1)) {
        return TLBRET_INVALID;
    }

    if (cpu->env.sr & SR_SM) { /* supervisor mode */
        if (cpu->env.tlb->itlb[0][idx].tr & SXE) {
            right |= PAGE_EXEC;
        }
    } else {
        if (cpu->env.tlb->itlb[0][idx].tr & UXE) {
            right |= PAGE_EXEC;
        }
    }

    if ((rw & 2) && ((right & PAGE_EXEC) == 0)) {
        return TLBRET_BADADDR;
    }

    *physical = (cpu->env.tlb->itlb[0][idx].tr & TARGET_PAGE_MASK) |
                (address & (TARGET_PAGE_SIZE-1));
    *prot = right;
    return TLBRET_MATCH;
}

int cpu_openrisc_get_phys_data(OpenRISCCPU *cpu,
                               hwaddr *physical,
                               int *prot, target_ulong address, int rw)
{
    int vpn = address >> TARGET_PAGE_BITS;
    int idx = vpn & DTLB_MASK;
    int right = 0;

    if ((cpu->env.tlb->dtlb[0][idx].mr >> TARGET_PAGE_BITS) != vpn) {
        return TLBRET_NOMATCH;
    }
    if (!(cpu->env.tlb->dtlb[0][idx].mr & 1)) {
        return TLBRET_INVALID;
    }

    if (cpu->env.sr & SR_SM) { /* supervisor mode */
        if (cpu->env.tlb->dtlb[0][idx].tr & SRE) {
            right |= PAGE_READ;
        }
        if (cpu->env.tlb->dtlb[0][idx].tr & SWE) {
            right |= PAGE_WRITE;
        }
    } else {
        if (cpu->env.tlb->dtlb[0][idx].tr & URE) {
            right |= PAGE_READ;
        }
        if (cpu->env.tlb->dtlb[0][idx].tr & UWE) {
            right |= PAGE_WRITE;
        }
    }

    if (!(rw & 1) && ((right & PAGE_READ) == 0)) {
        return TLBRET_BADADDR;
    }
    if ((rw & 1) && ((right & PAGE_WRITE) == 0)) {
        return TLBRET_BADADDR;
    }

    *physical = (cpu->env.tlb->dtlb[0][idx].tr & TARGET_PAGE_MASK) |
                (address & (TARGET_PAGE_SIZE-1));
    *prot = right;
    return TLBRET_MATCH;
}

static int cpu_openrisc_get_phys_addr(OpenRISCCPU *cpu,
                                      hwaddr *physical,
                                      int *prot, target_ulong address,
                                      int rw)
{
    int ret = TLBRET_MATCH;

    if (rw == 2) {    /* ITLB */
       *physical = 0;
        ret = cpu->env.tlb->cpu_openrisc_map_address_code(cpu, physical,
                                                          prot, address, rw);
    } else {          /* DTLB */
        ret = cpu->env.tlb->cpu_openrisc_map_address_data(cpu, physical,
                                                          prot, address, rw);
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
}

#ifndef CONFIG_USER_ONLY
int openrisc_cpu_handle_mmu_fault(CPUState *cs,
                                  vaddr address, int rw, int mmu_idx)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    int ret = 0;
    hwaddr physical = 0;
    int prot = 0;

    ret = cpu_openrisc_get_phys_addr(cpu, &physical, &prot,
                                     address, rw);

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
int openrisc_cpu_handle_mmu_fault(CPUState *cs,
                                  vaddr address, int rw, int mmu_idx)
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

    if (cpu_openrisc_get_phys_addr(cpu, &phys_addr, &prot, addr, 0)) {
        return -1;
    }

    return phys_addr;
}

void cpu_openrisc_mmu_init(OpenRISCCPU *cpu)
{
    cpu->env.tlb = g_malloc0(sizeof(CPUOpenRISCTLBContext));

    cpu->env.tlb->cpu_openrisc_map_address_code = &cpu_openrisc_get_phys_nommu;
    cpu->env.tlb->cpu_openrisc_map_address_data = &cpu_openrisc_get_phys_nommu;
}
#endif
