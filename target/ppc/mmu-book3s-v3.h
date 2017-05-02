/*
 *  PowerPC ISAV3 BookS emulation generic mmu definitions for qemu.
 *
 *  Copyright (c) 2017 Suraj Jitindar Singh, IBM Corporation
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

#ifndef MMU_H
#define MMU_H

#ifndef CONFIG_USER_ONLY

/* Partition Table Entry Fields */
#define PATBE1_GR 0x8000000000000000

/* Process Table Entry */
struct prtb_entry {
    uint64_t prtbe0, prtbe1;
};

#ifdef TARGET_PPC64

static inline bool ppc64_use_proc_tbl(PowerPCCPU *cpu)
{
    return !!(cpu->env.spr[SPR_LPCR] & LPCR_UPRT);
}

static inline bool ppc64_radix_guest(PowerPCCPU *cpu)
{
    PPCVirtualHypervisorClass *vhc =
        PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);

    return !!(vhc->get_patbe(cpu->vhyp) & PATBE1_GR);
}

int ppc64_v3_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                              int mmu_idx);

#endif /* TARGET_PPC64 */

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_H */
