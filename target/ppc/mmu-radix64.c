/*
 *  PowerPC Radix MMU mulation helpers for QEMU.
 *
 *  Copyright (c) 2016 Suraj Jitindar Singh, IBM Corporation
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
#include "qapi/error.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "exec/log.h"
#include "mmu-radix64.h"
#include "mmu-book3s-v3.h"

static bool ppc_radix64_get_fully_qualified_addr(CPUPPCState *env, vaddr eaddr,
                                                 uint64_t *lpid, uint64_t *pid)
{
    /* We don't have HV support yet and shouldn't get here with it set anyway */
    assert(!msr_hv);

    if (!msr_hv) { /* !MSR[HV] -> Guest */
        switch (eaddr & R_EADDR_QUADRANT) {
        case R_EADDR_QUADRANT0: /* Guest application */
            *lpid = env->spr[SPR_LPIDR];
            *pid = env->spr[SPR_BOOKS_PID];
            break;
        case R_EADDR_QUADRANT1: /* Illegal */
        case R_EADDR_QUADRANT2:
            return false;
        case R_EADDR_QUADRANT3: /* Guest OS */
            *lpid = env->spr[SPR_LPIDR];
            *pid = 0; /* pid set to 0 -> addresses guest operating system */
            break;
        }
    }

    return true;
}

static void ppc_radix64_raise_segi(PowerPCCPU *cpu, int rwx, vaddr eaddr)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    if (rwx == 2) { /* Instruction Segment Interrupt */
        cs->exception_index = POWERPC_EXCP_ISEG;
    } else { /* Data Segment Interrupt */
        cs->exception_index = POWERPC_EXCP_DSEG;
        env->spr[SPR_DAR] = eaddr;
    }
    env->error_code = 0;
}

static void ppc_radix64_raise_si(PowerPCCPU *cpu, int rwx, vaddr eaddr,
                                uint32_t cause)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    if (rwx == 2) { /* Instruction Storage Interrupt */
        cs->exception_index = POWERPC_EXCP_ISI;
        env->error_code = cause;
    } else { /* Data Storage Interrupt */
        cs->exception_index = POWERPC_EXCP_DSI;
        if (rwx == 1) { /* Write -> Store */
            cause |= DSISR_ISSTORE;
        }
        env->spr[SPR_DSISR] = cause;
        env->spr[SPR_DAR] = eaddr;
        env->error_code = 0;
    }
}


static bool ppc_radix64_check_prot(PowerPCCPU *cpu, int rwx, uint64_t pte,
                                   int *fault_cause, int *prot)
{
    CPUPPCState *env = &cpu->env;
    const int need_prot[] = { PAGE_READ, PAGE_WRITE, PAGE_EXEC };

    /* Check Page Attributes (pte58:59) */
    if (((pte & R_PTE_ATT) == R_PTE_ATT_NI_IO) && (rwx == 2)) {
        /*
         * Radix PTE entries with the non-idempotent I/O attribute are treated
         * as guarded storage
         */
        *fault_cause |= SRR1_NOEXEC_GUARD;
        return true;
    }

    /* Determine permissions allowed by Encoded Access Authority */
    if ((pte & R_PTE_EAA_PRIV) && msr_pr) { /* Insufficient Privilege */
        *prot = 0;
    } else if (msr_pr || (pte & R_PTE_EAA_PRIV)) {
        *prot = ppc_radix64_get_prot_eaa(pte);
    } else { /* !msr_pr && !(pte & R_PTE_EAA_PRIV) */
        *prot = ppc_radix64_get_prot_eaa(pte);
        *prot &= ppc_radix64_get_prot_amr(cpu); /* Least combined permissions */
    }

    /* Check if requested access type is allowed */
    if (need_prot[rwx] & ~(*prot)) { /* Page Protected for that Access */
        *fault_cause |= DSISR_PROTFAULT;
        return true;
    }

    return false;
}

static void ppc_radix64_set_rc(PowerPCCPU *cpu, int rwx, uint64_t pte,
                               hwaddr pte_addr, int *prot)
{
    CPUState *cs = CPU(cpu);
    uint64_t npte;

    npte = pte | R_PTE_R; /* Always set reference bit */

    if (rwx == 1) { /* Store/Write */
        npte |= R_PTE_C; /* Set change bit */
    } else {
        /*
         * Treat the page as read-only for now, so that a later write
         * will pass through this function again to set the C bit.
         */
        *prot &= ~PAGE_WRITE;
    }

    if (pte ^ npte) { /* If pte has changed then write it back */
        stq_phys(cs->as, pte_addr, npte);
    }
}

static uint64_t ppc_radix64_walk_tree(PowerPCCPU *cpu, vaddr eaddr,
                                      uint64_t base_addr, uint64_t nls,
                                      hwaddr *raddr, int *psize,
                                      int *fault_cause, hwaddr *pte_addr)
{
    CPUState *cs = CPU(cpu);
    uint64_t index, pde;

    if (nls < 5) { /* Directory maps less than 2**5 entries */
        *fault_cause |= DSISR_R_BADCONFIG;
        return 0;
    }

    /* Read page <directory/table> entry from guest address space */
    index = eaddr >> (*psize - nls); /* Shift */
    index &= ((1UL << nls) - 1); /* Mask */
    pde = ldq_phys(cs->as, base_addr + (index * sizeof(pde)));
    if (!(pde & R_PTE_VALID)) { /* Invalid Entry */
        *fault_cause |= DSISR_NOPTE;
        return 0;
    }

    *psize -= nls;

    /* Check if Leaf Entry -> Page Table Entry -> Stop the Search */
    if (pde & R_PTE_LEAF) {
        uint64_t rpn = pde & R_PTE_RPN;
        uint64_t mask = (1UL << *psize) - 1;

        /* Or high bits of rpn and low bits to ea to form whole real addr */
        *raddr = (rpn & ~mask) | (eaddr & mask);
        *pte_addr = base_addr + (index * sizeof(pde));
        return pde;
    }

    /* Next Level of Radix Tree */
    return ppc_radix64_walk_tree(cpu, eaddr, pde & R_PDE_NLB, pde & R_PDE_NLS,
                                 raddr, psize, fault_cause, pte_addr);
}

int ppc_radix64_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                                 int mmu_idx)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PPCVirtualHypervisorClass *vhc =
        PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
    hwaddr raddr, pte_addr;
    uint64_t lpid = 0, pid = 0, offset, size, patbe, prtbe0, pte;
    int page_size, prot, fault_cause = 0;

    assert((rwx == 0) || (rwx == 1) || (rwx == 2));
    assert(!msr_hv); /* For now there is no Radix PowerNV Support */
    assert(cpu->vhyp);
    assert(ppc64_use_proc_tbl(cpu));

    /* Real Mode Access */
    if (((rwx == 2) && (msr_ir == 0)) || ((rwx != 2) && (msr_dr == 0))) {
        /* In real mode top 4 effective addr bits (mostly) ignored */
        raddr = eaddr & 0x0FFFFFFFFFFFFFFFULL;

        tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                     PAGE_READ | PAGE_WRITE | PAGE_EXEC, mmu_idx,
                     TARGET_PAGE_SIZE);
        return 0;
    }

    /* Virtual Mode Access - get the fully qualified address */
    if (!ppc_radix64_get_fully_qualified_addr(env, eaddr, &lpid, &pid)) {
        ppc_radix64_raise_segi(cpu, rwx, eaddr);
        return 1;
    }

    /* Get Process Table */
    patbe = vhc->get_patbe(cpu->vhyp);

    /* Index Process Table by PID to Find Corresponding Process Table Entry */
    offset = pid * sizeof(struct prtb_entry);
    size = 1ULL << ((patbe & PATBE1_R_PRTS) + 12);
    if (offset >= size) {
        /* offset exceeds size of the process table */
        ppc_radix64_raise_si(cpu, rwx, eaddr, DSISR_NOPTE);
        return 1;
    }
    prtbe0 = ldq_phys(cs->as, (patbe & PATBE1_R_PRTB) + offset);

    /* Walk Radix Tree from Process Table Entry to Convert EA to RA */
    page_size = PRTBE_R_GET_RTS(prtbe0);
    pte = ppc_radix64_walk_tree(cpu, eaddr & R_EADDR_MASK,
                                prtbe0 & PRTBE_R_RPDB, prtbe0 & PRTBE_R_RPDS,
                                &raddr, &page_size, &fault_cause, &pte_addr);
    if (!pte || ppc_radix64_check_prot(cpu, rwx, pte, &fault_cause, &prot)) {
        /* Couldn't get pte or access denied due to protection */
        ppc_radix64_raise_si(cpu, rwx, eaddr, fault_cause);
        return 1;
    }

    /* Update Reference and Change Bits */
    ppc_radix64_set_rc(cpu, rwx, pte, pte_addr, &prot);

    tlb_set_page(cs, eaddr & TARGET_PAGE_MASK, raddr & TARGET_PAGE_MASK,
                 prot, mmu_idx, 1UL << page_size);
    return 0;
}

hwaddr ppc_radix64_get_phys_page_debug(PowerPCCPU *cpu, target_ulong eaddr)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PPCVirtualHypervisorClass *vhc =
        PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
    hwaddr raddr, pte_addr;
    uint64_t lpid = 0, pid = 0, offset, size, patbe, prtbe0, pte;
    int page_size, fault_cause = 0;

    /* Handle Real Mode */
    if (msr_dr == 0) {
        /* In real mode top 4 effective addr bits (mostly) ignored */
        return eaddr & 0x0FFFFFFFFFFFFFFFULL;
    }

    /* Virtual Mode Access - get the fully qualified address */
    if (!ppc_radix64_get_fully_qualified_addr(env, eaddr, &lpid, &pid)) {
        return -1;
    }

    /* Get Process Table */
    patbe = vhc->get_patbe(cpu->vhyp);

    /* Index Process Table by PID to Find Corresponding Process Table Entry */
    offset = pid * sizeof(struct prtb_entry);
    size = 1ULL << ((patbe & PATBE1_R_PRTS) + 12);
    if (offset >= size) {
        /* offset exceeds size of the process table */
        return -1;
    }
    prtbe0 = ldq_phys(cs->as, (patbe & PATBE1_R_PRTB) + offset);

    /* Walk Radix Tree from Process Table Entry to Convert EA to RA */
    page_size = PRTBE_R_GET_RTS(prtbe0);
    pte = ppc_radix64_walk_tree(cpu, eaddr & R_EADDR_MASK,
                                prtbe0 & PRTBE_R_RPDB, prtbe0 & PRTBE_R_RPDS,
                                &raddr, &page_size, &fault_cause, &pte_addr);
    if (!pte) {
        return -1;
    }

    return raddr & TARGET_PAGE_MASK;
}
