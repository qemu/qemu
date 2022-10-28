/*
 *  PowerPC Radix MMU mulation helpers for QEMU.
 *
 *  Copyright (c) 2016 Suraj Jitindar Singh, IBM Corporation
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "exec/log.h"
#include "internal.h"
#include "mmu-radix64.h"
#include "mmu-book3s-v3.h"

static bool ppc_radix64_get_fully_qualified_addr(const CPUPPCState *env,
                                                 vaddr eaddr,
                                                 uint64_t *lpid, uint64_t *pid)
{
    /* When EA(2:11) are nonzero, raise a segment interrupt */
    if (eaddr & ~R_EADDR_VALID_MASK) {
        return false;
    }

    if (FIELD_EX64(env->msr, MSR, HV)) { /* MSR[HV] -> Hypervisor/bare metal */
        switch (eaddr & R_EADDR_QUADRANT) {
        case R_EADDR_QUADRANT0:
            *lpid = 0;
            *pid = env->spr[SPR_BOOKS_PID];
            break;
        case R_EADDR_QUADRANT1:
            *lpid = env->spr[SPR_LPIDR];
            *pid = env->spr[SPR_BOOKS_PID];
            break;
        case R_EADDR_QUADRANT2:
            *lpid = env->spr[SPR_LPIDR];
            *pid = 0;
            break;
        case R_EADDR_QUADRANT3:
            *lpid = 0;
            *pid = 0;
            break;
        default:
            g_assert_not_reached();
        }
    } else {  /* !MSR[HV] -> Guest */
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
        default:
            g_assert_not_reached();
        }
    }

    return true;
}

static void ppc_radix64_raise_segi(PowerPCCPU *cpu, MMUAccessType access_type,
                                   vaddr eaddr)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    switch (access_type) {
    case MMU_INST_FETCH:
        /* Instruction Segment Interrupt */
        cs->exception_index = POWERPC_EXCP_ISEG;
        break;
    case MMU_DATA_STORE:
    case MMU_DATA_LOAD:
        /* Data Segment Interrupt */
        cs->exception_index = POWERPC_EXCP_DSEG;
        env->spr[SPR_DAR] = eaddr;
        break;
    default:
        g_assert_not_reached();
    }
    env->error_code = 0;
}

static inline const char *access_str(MMUAccessType access_type)
{
    return access_type == MMU_DATA_LOAD ? "reading" :
        (access_type == MMU_DATA_STORE ? "writing" : "execute");
}

static void ppc_radix64_raise_si(PowerPCCPU *cpu, MMUAccessType access_type,
                                 vaddr eaddr, uint32_t cause)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_MMU, "%s for %s @0x%"VADDR_PRIx" cause %08x\n",
                  __func__, access_str(access_type),
                  eaddr, cause);

    switch (access_type) {
    case MMU_INST_FETCH:
        /* Instruction Storage Interrupt */
        cs->exception_index = POWERPC_EXCP_ISI;
        env->error_code = cause;
        break;
    case MMU_DATA_STORE:
        cause |= DSISR_ISSTORE;
        /* fall through */
    case MMU_DATA_LOAD:
        /* Data Storage Interrupt */
        cs->exception_index = POWERPC_EXCP_DSI;
        env->spr[SPR_DSISR] = cause;
        env->spr[SPR_DAR] = eaddr;
        env->error_code = 0;
        break;
    default:
        g_assert_not_reached();
    }
}

static void ppc_radix64_raise_hsi(PowerPCCPU *cpu, MMUAccessType access_type,
                                  vaddr eaddr, hwaddr g_raddr, uint32_t cause)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_MMU, "%s for %s @0x%"VADDR_PRIx" 0x%"
                  HWADDR_PRIx" cause %08x\n",
                  __func__, access_str(access_type),
                  eaddr, g_raddr, cause);

    switch (access_type) {
    case MMU_INST_FETCH:
        /* H Instruction Storage Interrupt */
        cs->exception_index = POWERPC_EXCP_HISI;
        env->spr[SPR_ASDR] = g_raddr;
        env->error_code = cause;
        break;
    case MMU_DATA_STORE:
        cause |= DSISR_ISSTORE;
        /* fall through */
    case MMU_DATA_LOAD:
        /* H Data Storage Interrupt */
        cs->exception_index = POWERPC_EXCP_HDSI;
        env->spr[SPR_HDSISR] = cause;
        env->spr[SPR_HDAR] = eaddr;
        env->spr[SPR_ASDR] = g_raddr;
        env->error_code = 0;
        break;
    default:
        g_assert_not_reached();
    }
}

static bool ppc_radix64_check_prot(PowerPCCPU *cpu, MMUAccessType access_type,
                                   uint64_t pte, int *fault_cause, int *prot,
                                   int mmu_idx, bool partition_scoped)
{
    CPUPPCState *env = &cpu->env;
    int need_prot;

    /* Check Page Attributes (pte58:59) */
    if ((pte & R_PTE_ATT) == R_PTE_ATT_NI_IO && access_type == MMU_INST_FETCH) {
        /*
         * Radix PTE entries with the non-idempotent I/O attribute are treated
         * as guarded storage
         */
        *fault_cause |= SRR1_NOEXEC_GUARD;
        return true;
    }

    /* Determine permissions allowed by Encoded Access Authority */
    if (!partition_scoped && (pte & R_PTE_EAA_PRIV) &&
        FIELD_EX64(env->msr, MSR, PR)) {
        *prot = 0;
    } else if (mmuidx_pr(mmu_idx) || (pte & R_PTE_EAA_PRIV) ||
               partition_scoped) {
        *prot = ppc_radix64_get_prot_eaa(pte);
    } else { /* !MSR_PR && !(pte & R_PTE_EAA_PRIV) && !partition_scoped */
        *prot = ppc_radix64_get_prot_eaa(pte);
        *prot &= ppc_radix64_get_prot_amr(cpu); /* Least combined permissions */
    }

    /* Check if requested access type is allowed */
    need_prot = prot_for_access_type(access_type);
    if (need_prot & ~*prot) { /* Page Protected for that Access */
        *fault_cause |= access_type == MMU_INST_FETCH ? SRR1_NOEXEC_GUARD :
                                                        DSISR_PROTFAULT;
        return true;
    }

    return false;
}

static void ppc_radix64_set_rc(PowerPCCPU *cpu, MMUAccessType access_type,
                               uint64_t pte, hwaddr pte_addr, int *prot)
{
    CPUState *cs = CPU(cpu);
    uint64_t npte;

    npte = pte | R_PTE_R; /* Always set reference bit */

    if (access_type == MMU_DATA_STORE) { /* Store/Write */
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

static bool ppc_radix64_is_valid_level(int level, int psize, uint64_t nls)
{
    bool ret;

    /*
     * Check if this is a valid level, according to POWER9 and POWER10
     * Processor User's Manuals, sections 4.10.4.1 and 5.10.6.1, respectively:
     * Supported Radix Tree Configurations and Resulting Page Sizes.
     *
     * Note: these checks are specific to POWER9 and POWER10 CPUs. Any future
     * CPUs that supports a different Radix MMU configuration will need their
     * own implementation.
     */
    switch (level) {
    case 0:     /* Root Page Dir */
        ret = psize == 52 && nls == 13;
        break;
    case 1:
    case 2:
        ret = nls == 9;
        break;
    case 3:
        ret = nls == 9 || nls == 5;
        break;
    default:
        ret = false;
    }

    if (unlikely(!ret)) {
        qemu_log_mask(LOG_GUEST_ERROR, "invalid radix configuration: "
                      "level %d size %d nls %"PRIu64"\n",
                      level, psize, nls);
    }
    return ret;
}

static int ppc_radix64_next_level(AddressSpace *as, vaddr eaddr,
                                  uint64_t *pte_addr, uint64_t *nls,
                                  int *psize, uint64_t *pte, int *fault_cause)
{
    uint64_t index, mask, nlb, pde;

    /* Read page <directory/table> entry from guest address space */
    pde = ldq_phys(as, *pte_addr);
    if (!(pde & R_PTE_VALID)) {         /* Invalid Entry */
        *fault_cause |= DSISR_NOPTE;
        return 1;
    }

    *pte = pde;
    *psize -= *nls;
    if (!(pde & R_PTE_LEAF)) { /* Prepare for next iteration */
        *nls = pde & R_PDE_NLS;
        index = eaddr >> (*psize - *nls);       /* Shift */
        index &= ((1UL << *nls) - 1);           /* Mask */
        nlb = pde & R_PDE_NLB;
        mask = MAKE_64BIT_MASK(0, *nls + 3);

        if (nlb & mask) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: misaligned page dir/table base: 0x"TARGET_FMT_lx
                " page dir size: 0x"TARGET_FMT_lx"\n",
                __func__, nlb, mask + 1);
            nlb &= ~mask;
        }
        *pte_addr = nlb + index * sizeof(pde);
    }
    return 0;
}

static int ppc_radix64_walk_tree(AddressSpace *as, vaddr eaddr,
                                 uint64_t base_addr, uint64_t nls,
                                 hwaddr *raddr, int *psize, uint64_t *pte,
                                 int *fault_cause, hwaddr *pte_addr)
{
    uint64_t index, pde, rpn, mask;
    int level = 0;

    index = eaddr >> (*psize - nls);    /* Shift */
    index &= ((1UL << nls) - 1);        /* Mask */
    mask = MAKE_64BIT_MASK(0, nls + 3);

    if (base_addr & mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: misaligned page dir base: 0x"TARGET_FMT_lx
            " page dir size: 0x"TARGET_FMT_lx"\n",
            __func__, base_addr, mask + 1);
        base_addr &= ~mask;
    }
    *pte_addr = base_addr + index * sizeof(pde);

    do {
        int ret;

        if (!ppc_radix64_is_valid_level(level++, *psize, nls)) {
            *fault_cause |= DSISR_R_BADCONFIG;
            return 1;
        }

        ret = ppc_radix64_next_level(as, eaddr, pte_addr, &nls, psize, &pde,
                                     fault_cause);
        if (ret) {
            return ret;
        }
    } while (!(pde & R_PTE_LEAF));

    *pte = pde;
    rpn = pde & R_PTE_RPN;
    mask = (1UL << *psize) - 1;

    /* Or high bits of rpn and low bits to ea to form whole real addr */
    *raddr = (rpn & ~mask) | (eaddr & mask);
    return 0;
}

static bool validate_pate(PowerPCCPU *cpu, uint64_t lpid, ppc_v3_pate_t *pate)
{
    CPUPPCState *env = &cpu->env;

    if (!(pate->dw0 & PATE0_HR)) {
        return false;
    }
    if (lpid == 0 && !FIELD_EX64(env->msr, MSR, HV)) {
        return false;
    }
    if ((pate->dw0 & PATE1_R_PRTS) < 5) {
        return false;
    }
    /* More checks ... */
    return true;
}

static int ppc_radix64_partition_scoped_xlate(PowerPCCPU *cpu,
                                              MMUAccessType access_type,
                                              vaddr eaddr, hwaddr g_raddr,
                                              ppc_v3_pate_t pate,
                                              hwaddr *h_raddr, int *h_prot,
                                              int *h_page_size, bool pde_addr,
                                              int mmu_idx, bool guest_visible)
{
    int fault_cause = 0;
    hwaddr pte_addr;
    uint64_t pte;

    qemu_log_mask(CPU_LOG_MMU, "%s for %s @0x%"VADDR_PRIx
                  " mmu_idx %u 0x%"HWADDR_PRIx"\n",
                  __func__, access_str(access_type),
                  eaddr, mmu_idx, g_raddr);

    *h_page_size = PRTBE_R_GET_RTS(pate.dw0);
    /* No valid pte or access denied due to protection */
    if (ppc_radix64_walk_tree(CPU(cpu)->as, g_raddr, pate.dw0 & PRTBE_R_RPDB,
                              pate.dw0 & PRTBE_R_RPDS, h_raddr, h_page_size,
                              &pte, &fault_cause, &pte_addr) ||
        ppc_radix64_check_prot(cpu, access_type, pte,
                               &fault_cause, h_prot, mmu_idx, true)) {
        if (pde_addr) { /* address being translated was that of a guest pde */
            fault_cause |= DSISR_PRTABLE_FAULT;
        }
        if (guest_visible) {
            ppc_radix64_raise_hsi(cpu, access_type, eaddr, g_raddr, fault_cause);
        }
        return 1;
    }

    if (guest_visible) {
        ppc_radix64_set_rc(cpu, access_type, pte, pte_addr, h_prot);
    }

    return 0;
}

/*
 * The spapr vhc has a flat partition scope provided by qemu memory when
 * not nested.
 *
 * When running a nested guest, the addressing is 2-level radix on top of the
 * vhc memory, so it works practically identically to the bare metal 2-level
 * radix. So that code is selected directly. A cleaner and more flexible nested
 * hypervisor implementation would allow the vhc to provide a ->nested_xlate()
 * function but that is not required for the moment.
 */
static bool vhyp_flat_addressing(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return !vhyp_cpu_in_nested(cpu);
    }
    return false;
}

static int ppc_radix64_process_scoped_xlate(PowerPCCPU *cpu,
                                            MMUAccessType access_type,
                                            vaddr eaddr, uint64_t pid,
                                            ppc_v3_pate_t pate, hwaddr *g_raddr,
                                            int *g_prot, int *g_page_size,
                                            int mmu_idx, bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    uint64_t offset, size, prtb, prtbe_addr, prtbe0, base_addr, nls, index, pte;
    int fault_cause = 0, h_page_size, h_prot;
    hwaddr h_raddr, pte_addr;
    int ret;

    qemu_log_mask(CPU_LOG_MMU, "%s for %s @0x%"VADDR_PRIx
                  " mmu_idx %u pid %"PRIu64"\n",
                  __func__, access_str(access_type),
                  eaddr, mmu_idx, pid);

    prtb = (pate.dw1 & PATE1_R_PRTB);
    size = 1ULL << ((pate.dw1 & PATE1_R_PRTS) + 12);
    if (prtb & (size - 1)) {
        /* Process Table not properly aligned */
        if (guest_visible) {
            ppc_radix64_raise_si(cpu, access_type, eaddr, DSISR_R_BADCONFIG);
        }
        return 1;
    }

    /* Index Process Table by PID to Find Corresponding Process Table Entry */
    offset = pid * sizeof(struct prtb_entry);
    if (offset >= size) {
        /* offset exceeds size of the process table */
        if (guest_visible) {
            ppc_radix64_raise_si(cpu, access_type, eaddr, DSISR_NOPTE);
        }
        return 1;
    }
    prtbe_addr = prtb + offset;

    if (vhyp_flat_addressing(cpu)) {
        prtbe0 = ldq_phys(cs->as, prtbe_addr);
    } else {
        /*
         * Process table addresses are subject to partition-scoped
         * translation
         *
         * On a Radix host, the partition-scoped page table for LPID=0
         * is only used to translate the effective addresses of the
         * process table entries.
         */
        ret = ppc_radix64_partition_scoped_xlate(cpu, 0, eaddr, prtbe_addr,
                                                 pate, &h_raddr, &h_prot,
                                                 &h_page_size, true,
            /* mmu_idx is 5 because we're translating from hypervisor scope */
                                                 5, guest_visible);
        if (ret) {
            return ret;
        }
        prtbe0 = ldq_phys(cs->as, h_raddr);
    }

    /* Walk Radix Tree from Process Table Entry to Convert EA to RA */
    *g_page_size = PRTBE_R_GET_RTS(prtbe0);
    base_addr = prtbe0 & PRTBE_R_RPDB;
    nls = prtbe0 & PRTBE_R_RPDS;
    if (FIELD_EX64(env->msr, MSR, HV) || vhyp_flat_addressing(cpu)) {
        /*
         * Can treat process table addresses as real addresses
         */
        ret = ppc_radix64_walk_tree(cs->as, eaddr & R_EADDR_MASK, base_addr,
                                    nls, g_raddr, g_page_size, &pte,
                                    &fault_cause, &pte_addr);
        if (ret) {
            /* No valid PTE */
            if (guest_visible) {
                ppc_radix64_raise_si(cpu, access_type, eaddr, fault_cause);
            }
            return ret;
        }
    } else {
        uint64_t rpn, mask;
        int level = 0;

        index = (eaddr & R_EADDR_MASK) >> (*g_page_size - nls); /* Shift */
        index &= ((1UL << nls) - 1);                            /* Mask */
        pte_addr = base_addr + (index * sizeof(pte));

        /*
         * Each process table address is subject to a partition-scoped
         * translation
         */
        do {
            ret = ppc_radix64_partition_scoped_xlate(cpu, 0, eaddr, pte_addr,
                                                     pate, &h_raddr, &h_prot,
                                                     &h_page_size, true,
            /* mmu_idx is 5 because we're translating from hypervisor scope */
                                                     5, guest_visible);
            if (ret) {
                return ret;
            }

            if (!ppc_radix64_is_valid_level(level++, *g_page_size, nls)) {
                fault_cause |= DSISR_R_BADCONFIG;
                ret = 1;
            } else {
                ret = ppc_radix64_next_level(cs->as, eaddr & R_EADDR_MASK,
                                             &h_raddr, &nls, g_page_size,
                                             &pte, &fault_cause);
            }

            if (ret) {
                /* No valid pte */
                if (guest_visible) {
                    ppc_radix64_raise_si(cpu, access_type, eaddr, fault_cause);
                }
                return ret;
            }
            pte_addr = h_raddr;
        } while (!(pte & R_PTE_LEAF));

        rpn = pte & R_PTE_RPN;
        mask = (1UL << *g_page_size) - 1;

        /* Or high bits of rpn and low bits to ea to form whole real addr */
        *g_raddr = (rpn & ~mask) | (eaddr & mask);
    }

    if (ppc_radix64_check_prot(cpu, access_type, pte, &fault_cause,
                               g_prot, mmu_idx, false)) {
        /* Access denied due to protection */
        if (guest_visible) {
            ppc_radix64_raise_si(cpu, access_type, eaddr, fault_cause);
        }
        return 1;
    }

    if (guest_visible) {
        ppc_radix64_set_rc(cpu, access_type, pte, pte_addr, g_prot);
    }

    return 0;
}

/*
 * Radix tree translation is a 2 steps translation process:
 *
 * 1. Process-scoped translation:   Guest Eff Addr  -> Guest Real Addr
 * 2. Partition-scoped translation: Guest Real Addr -> Host Real Addr
 *
 *                                  MSR[HV]
 *              +-------------+----------------+---------------+
 *              |             |     HV = 0     |     HV = 1    |
 *              +-------------+----------------+---------------+
 *              | Relocation  |    Partition   |      No       |
 *              | = Off       |     Scoped     |  Translation  |
 *  Relocation  +-------------+----------------+---------------+
 *              | Relocation  |   Partition &  |    Process    |
 *              | = On        | Process Scoped |    Scoped     |
 *              +-------------+----------------+---------------+
 */
static bool ppc_radix64_xlate_impl(PowerPCCPU *cpu, vaddr eaddr,
                                   MMUAccessType access_type, hwaddr *raddr,
                                   int *psizep, int *protp, int mmu_idx,
                                   bool guest_visible)
{
    CPUPPCState *env = &cpu->env;
    uint64_t lpid, pid;
    ppc_v3_pate_t pate;
    int psize, prot;
    hwaddr g_raddr;
    bool relocation;

    assert(!(mmuidx_hv(mmu_idx) && cpu->vhyp));

    relocation = !mmuidx_real(mmu_idx);

    /* HV or virtual hypervisor Real Mode Access */
    if (!relocation && (mmuidx_hv(mmu_idx) || vhyp_flat_addressing(cpu))) {
        /* In real mode top 4 effective addr bits (mostly) ignored */
        *raddr = eaddr & 0x0FFFFFFFFFFFFFFFULL;

        /* In HV mode, add HRMOR if top EA bit is clear */
        if (mmuidx_hv(mmu_idx) || !env->has_hv_mode) {
            if (!(eaddr >> 63)) {
                *raddr |= env->spr[SPR_HRMOR];
           }
        }
        *protp = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        *psizep = TARGET_PAGE_BITS;
        return true;
    }

    /*
     * Check UPRT (we avoid the check in real mode to deal with
     * transitional states during kexec.
     */
    if (guest_visible && !ppc64_use_proc_tbl(cpu)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "LPCR:UPRT not set in radix mode ! LPCR="
                      TARGET_FMT_lx "\n", env->spr[SPR_LPCR]);
    }

    /* Virtual Mode Access - get the fully qualified address */
    if (!ppc_radix64_get_fully_qualified_addr(&cpu->env, eaddr, &lpid, &pid)) {
        if (guest_visible) {
            ppc_radix64_raise_segi(cpu, access_type, eaddr);
        }
        return false;
    }

    /* Get Partition Table */
    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc;
        vhc = PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        if (!vhc->get_pate(cpu->vhyp, cpu, lpid, &pate)) {
            if (guest_visible) {
                ppc_radix64_raise_hsi(cpu, access_type, eaddr, eaddr,
                                      DSISR_R_BADCONFIG);
            }
            return false;
        }
    } else {
        if (!ppc64_v3_get_pate(cpu, lpid, &pate)) {
            if (guest_visible) {
                ppc_radix64_raise_hsi(cpu, access_type, eaddr, eaddr,
                                      DSISR_R_BADCONFIG);
            }
            return false;
        }
        if (!validate_pate(cpu, lpid, &pate)) {
            if (guest_visible) {
                ppc_radix64_raise_hsi(cpu, access_type, eaddr, eaddr,
                                      DSISR_R_BADCONFIG);
            }
            return false;
        }
    }

    *psizep = INT_MAX;
    *protp = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    /*
     * Perform process-scoped translation if relocation enabled.
     *
     * - Translates an effective address to a host real address in
     *   quadrants 0 and 3 when HV=1.
     *
     * - Translates an effective address to a guest real address.
     */
    if (relocation) {
        int ret = ppc_radix64_process_scoped_xlate(cpu, access_type, eaddr, pid,
                                                   pate, &g_raddr, &prot,
                                                   &psize, mmu_idx, guest_visible);
        if (ret) {
            return false;
        }
        *psizep = MIN(*psizep, psize);
        *protp &= prot;
    } else {
        g_raddr = eaddr & R_EADDR_MASK;
    }

    if (vhyp_flat_addressing(cpu)) {
        *raddr = g_raddr;
    } else {
        /*
         * Perform partition-scoped translation if !HV or HV access to
         * quadrants 1 or 2. Translates a guest real address to a host
         * real address.
         */
        if (lpid || !mmuidx_hv(mmu_idx)) {
            int ret;

            ret = ppc_radix64_partition_scoped_xlate(cpu, access_type, eaddr,
                                                     g_raddr, pate, raddr,
                                                     &prot, &psize, false,
                                                     mmu_idx, guest_visible);
            if (ret) {
                return false;
            }
            *psizep = MIN(*psizep, psize);
            *protp &= prot;
        } else {
            *raddr = g_raddr;
        }
    }

    return true;
}

bool ppc_radix64_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                       hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                       bool guest_visible)
{
    bool ret = ppc_radix64_xlate_impl(cpu, eaddr, access_type, raddrp,
                                      psizep, protp, mmu_idx, guest_visible);

    qemu_log_mask(CPU_LOG_MMU, "%s for %s @0x%"VADDR_PRIx
                  " mmu_idx %u (prot %c%c%c) -> 0x%"HWADDR_PRIx"\n",
                  __func__, access_str(access_type),
                  eaddr, mmu_idx,
                  *protp & PAGE_READ ? 'r' : '-',
                  *protp & PAGE_WRITE ? 'w' : '-',
                  *protp & PAGE_EXEC ? 'x' : '-',
                  *raddrp);

    return ret;
}
