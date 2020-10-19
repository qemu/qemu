/*
 *  PowerPC ISAV3 BookS emulation generic mmu definitions for qemu.
 *
 *  Copyright (c) 2017 Suraj Jitindar Singh, IBM Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_MMU_BOOK3S_V3_H
#define PPC_MMU_BOOK3S_V3_H

#include "mmu-hash64.h"

#ifndef CONFIG_USER_ONLY

/*
 * Partition table definitions
 */
#define PTCR_PATB               0x0FFFFFFFFFFFF000ULL /* Partition Table Base */
#define PTCR_PATS               0x000000000000001FULL /* Partition Table Size */

/* Partition Table Entry Fields */
#define PATE0_HR 0x8000000000000000

/*
 * WARNING: This field doesn't actually exist in the final version of
 * the architecture and is unused by hardware. However, qemu uses it
 * as an indication of a radix guest in the pseudo-PATB entry that it
 * maintains for SPAPR guests and in the migration stream, so we need
 * to keep it around
 */
#define PATE1_GR 0x8000000000000000

/* Process Table Entry */
struct prtb_entry {
    uint64_t prtbe0, prtbe1;
};

#ifdef TARGET_PPC64

static inline bool ppc64_use_proc_tbl(PowerPCCPU *cpu)
{
    return !!(cpu->env.spr[SPR_LPCR] & LPCR_UPRT);
}

bool ppc64_v3_get_pate(PowerPCCPU *cpu, target_ulong lpid,
                       ppc_v3_pate_t *entry);

/*
 * The LPCR:HR bit is a shortcut that avoids having to
 * dig out the partition table in the fast path. This is
 * also how the HW uses it.
 */
static inline bool ppc64_v3_radix(PowerPCCPU *cpu)
{
    return !!(cpu->env.spr[SPR_LPCR] & LPCR_HR);
}

hwaddr ppc64_v3_get_phys_page_debug(PowerPCCPU *cpu, vaddr eaddr);

int ppc64_v3_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                              int mmu_idx);

static inline hwaddr ppc_hash64_hpt_base(PowerPCCPU *cpu)
{
    uint64_t base;

    if (cpu->vhyp) {
        return 0;
    }
    if (cpu->env.mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            return 0;
        }
        base = pate.dw0;
    } else {
        base = cpu->env.spr[SPR_SDR1];
    }
    return base & SDR_64_HTABORG;
}

static inline hwaddr ppc_hash64_hpt_mask(PowerPCCPU *cpu)
{
    uint64_t base;

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        return vhc->hpt_mask(cpu->vhyp);
    }
    if (cpu->env.mmu_model == POWERPC_MMU_3_00) {
        ppc_v3_pate_t pate;

        if (!ppc64_v3_get_pate(cpu, cpu->env.spr[SPR_LPIDR], &pate)) {
            return 0;
        }
        base = pate.dw0;
    } else {
        base = cpu->env.spr[SPR_SDR1];
    }
    return (1ULL << ((base & SDR_64_HTABSIZE) + 18 - 7)) - 1;
}

#endif /* TARGET_PPC64 */

#endif /* CONFIG_USER_ONLY */

#endif /* PPC_MMU_BOOK3S_V3_H */
