/*
 *  x86 exception helpers - sysemu code
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "tcg/helper-tcg.h"

typedef struct TranslateParams {
    target_ulong addr;
    target_ulong cr3;
    int pg_mode;
    int mmu_idx;
    MMUAccessType access_type;
    bool use_stage2;
} TranslateParams;

typedef struct TranslateResult {
    hwaddr paddr;
    int prot;
    int page_size;
} TranslateResult;

typedef struct TranslateFault {
    int exception_index;
    int error_code;
    target_ulong cr2;
} TranslateFault;

#define GET_HPHYS(cs, gpa, access_type, prot)  \
	(in->use_stage2 ? get_hphys(cs, gpa, access_type, prot) : gpa)

static bool mmu_translate(CPUX86State *env, const TranslateParams *in,
                          TranslateResult *out, TranslateFault *err)
{
    CPUState *cs = env_cpu(env);
    X86CPU *cpu = env_archcpu(env);
    const int32_t a20_mask = x86_get_a20_mask(env);
    const target_ulong addr = in->addr;
    const int pg_mode = in->pg_mode;
    const bool is_user = (in->mmu_idx == MMU_USER_IDX);
    const MMUAccessType access_type = in->access_type;
    uint64_t ptep, pte;
    hwaddr pde_addr, pte_addr;
    uint64_t rsvd_mask = PG_ADDRESS_MASK & ~MAKE_64BIT_MASK(0, cpu->phys_bits);
    uint32_t pkr;
    int page_size;

    if (!(pg_mode & PG_MODE_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (pg_mode & PG_MODE_PAE) {
        uint64_t pde, pdpe;
        target_ulong pdpe_addr;

#ifdef TARGET_X86_64
        if (pg_mode & PG_MODE_LMA) {
            bool la57 = pg_mode & PG_MODE_LA57;
            uint64_t pml5e_addr, pml5e;
            uint64_t pml4e_addr, pml4e;

            if (la57) {
                pml5e_addr = ((in->cr3 & ~0xfff) +
                        (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
                pml5e_addr = GET_HPHYS(cs, pml5e_addr, MMU_DATA_STORE, NULL);
                pml5e = x86_ldq_phys(cs, pml5e_addr);
                if (!(pml5e & PG_PRESENT_MASK)) {
                    goto do_fault;
                }
                if (pml5e & (rsvd_mask | PG_PSE_MASK)) {
                    goto do_fault_rsvd;
                }
                if (!(pml5e & PG_ACCESSED_MASK)) {
                    pml5e |= PG_ACCESSED_MASK;
                    x86_stl_phys_notdirty(cs, pml5e_addr, pml5e);
                }
                ptep = pml5e ^ PG_NX_MASK;
            } else {
                pml5e = in->cr3;
                ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
            }

            pml4e_addr = ((pml5e & PG_ADDRESS_MASK) +
                    (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
            pml4e_addr = GET_HPHYS(cs, pml4e_addr, MMU_DATA_STORE, NULL);
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pml4e & (rsvd_mask | PG_PSE_MASK)) {
                goto do_fault_rsvd;
            }
            if (!(pml4e & PG_ACCESSED_MASK)) {
                pml4e |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pml4e_addr, pml4e);
            }
            ptep &= pml4e ^ PG_NX_MASK;
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) + (((addr >> 30) & 0x1ff) << 3)) &
                a20_mask;
            pdpe_addr = GET_HPHYS(cs, pdpe_addr, MMU_DATA_STORE, NULL);
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pdpe & rsvd_mask) {
                goto do_fault_rsvd;
            }
            ptep &= pdpe ^ PG_NX_MASK;
            if (!(pdpe & PG_ACCESSED_MASK)) {
                pdpe |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pdpe_addr, pdpe);
            }
            if (pdpe & PG_PSE_MASK) {
                /* 1 GB page */
                page_size = 1024 * 1024 * 1024;
                pte_addr = pdpe_addr;
                pte = pdpe;
                goto do_check_protect;
            }
        } else
#endif
        {
            /* XXX: load them when cr3 is loaded ? */
            pdpe_addr = ((in->cr3 & ~0x1f) + ((addr >> 27) & 0x18)) &
                a20_mask;
            pdpe_addr = GET_HPHYS(cs, pdpe_addr, MMU_DATA_STORE, NULL);
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            rsvd_mask |= PG_HI_USER_MASK;
            if (pdpe & (rsvd_mask | PG_NX_MASK)) {
                goto do_fault_rsvd;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) + (((addr >> 21) & 0x1ff) << 3)) &
            a20_mask;
        pde_addr = GET_HPHYS(cs, pde_addr, MMU_DATA_STORE, NULL);
        pde = x86_ldq_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pde & rsvd_mask) {
            goto do_fault_rsvd;
        }
        ptep &= pde ^ PG_NX_MASK;
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte_addr = pde_addr;
            pte = pde;
            goto do_check_protect;
        }
        /* 4 KB page */
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }
        pte_addr = ((pde & PG_ADDRESS_MASK) + (((addr >> 12) & 0x1ff) << 3)) &
            a20_mask;
        pte_addr = GET_HPHYS(cs, pte_addr, MMU_DATA_STORE, NULL);
        pte = x86_ldq_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pte & rsvd_mask) {
            goto do_fault_rsvd;
        }
        /* combine pde and pte nx, user and rw protections */
        ptep &= pte ^ PG_NX_MASK;
        page_size = 4096;
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((in->cr3 & ~0xfff) + ((addr >> 20) & 0xffc)) &
            a20_mask;
        pde_addr = GET_HPHYS(cs, pde_addr, MMU_DATA_STORE, NULL);
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pde | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (pg_mode & PG_MODE_PSE)) {
            page_size = 4096 * 1024;
            pte_addr = pde_addr;

            /* Bits 20-13 provide bits 39-32 of the address, bit 21 is reserved.
             * Leave bits 20-13 in place for setting accessed/dirty bits below.
             */
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            rsvd_mask = 0x200000;
            goto do_check_protect_pse36;
        }

        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }

        /* page directory entry */
        pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) &
            a20_mask;
        pte_addr = GET_HPHYS(cs, pte_addr, MMU_DATA_STORE, NULL);
        pte = x86_ldl_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        /* combine pde and pte user and rw protections */
        ptep &= pte | PG_NX_MASK;
        page_size = 4096;
        rsvd_mask = 0;
    }

do_check_protect:
    rsvd_mask |= (page_size - 1) & PG_ADDRESS_MASK & ~PG_PSE_PAT_MASK;
do_check_protect_pse36:
    if (pte & rsvd_mask) {
        goto do_fault_rsvd;
    }
    ptep ^= PG_NX_MASK;

    /* can the page can be put in the TLB?  prot will tell us */
    if (is_user && !(ptep & PG_USER_MASK)) {
        goto do_fault_protect;
    }

    int prot = 0;
    if (in->mmu_idx != MMU_KSMAP_IDX || !(ptep & PG_USER_MASK)) {
        prot |= PAGE_READ;
        if ((ptep & PG_RW_MASK) || !(is_user || (pg_mode & PG_MODE_WP))) {
            prot |= PAGE_WRITE;
        }
    }
    if (!(ptep & PG_NX_MASK) &&
        (is_user ||
         !((pg_mode & PG_MODE_SMEP) && (ptep & PG_USER_MASK)))) {
        prot |= PAGE_EXEC;
    }

    if (ptep & PG_USER_MASK) {
        pkr = pg_mode & PG_MODE_PKE ? env->pkru : 0;
    } else {
        pkr = pg_mode & PG_MODE_PKS ? env->pkrs : 0;
    }
    if (pkr) {
        uint32_t pk = (pte & PG_PKRU_MASK) >> PG_PKRU_BIT;
        uint32_t pkr_ad = (pkr >> pk * 2) & 1;
        uint32_t pkr_wd = (pkr >> pk * 2) & 2;
        uint32_t pkr_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

        if (pkr_ad) {
            pkr_prot &= ~(PAGE_READ | PAGE_WRITE);
        } else if (pkr_wd && (is_user || (pg_mode & PG_MODE_WP))) {
            pkr_prot &= ~PAGE_WRITE;
        }
        if ((pkr_prot & (1 << access_type)) == 0) {
            goto do_fault_pk_protect;
        }
        prot &= pkr_prot;
    }

    if ((prot & (1 << access_type)) == 0) {
        goto do_fault_protect;
    }

    /* yes, it can! */
    {
        uint32_t set = PG_ACCESSED_MASK;
        if (access_type == MMU_DATA_STORE) {
            set |= PG_DIRTY_MASK;
        }
        if (set & ~pte) {
            pte |= set;
            x86_stl_phys_notdirty(cs, pte_addr, pte);
        }
    }

    if (!(pte & PG_DIRTY_MASK)) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        assert(access_type != MMU_DATA_STORE);
        prot &= ~PAGE_WRITE;
    }
    out->prot = prot;
    out->page_size = page_size;

    /* align to page_size */
    out->paddr = (pte & a20_mask & PG_ADDRESS_MASK & ~(page_size - 1))
               | (addr & (page_size - 1));
    out->paddr = GET_HPHYS(cs, out->paddr, access_type, &out->prot);
    return true;

    int error_code;
 do_fault_rsvd:
    error_code = PG_ERROR_RSVD_MASK;
    goto do_fault_cont;
 do_fault_protect:
    error_code = PG_ERROR_P_MASK;
    goto do_fault_cont;
 do_fault_pk_protect:
    assert(access_type != MMU_INST_FETCH);
    error_code = PG_ERROR_PK_MASK | PG_ERROR_P_MASK;
    goto do_fault_cont;
 do_fault:
    error_code = 0;
 do_fault_cont:
    if (is_user) {
        error_code |= PG_ERROR_U_MASK;
    }
    switch (access_type) {
    case MMU_DATA_LOAD:
        break;
    case MMU_DATA_STORE:
        error_code |= PG_ERROR_W_MASK;
        break;
    case MMU_INST_FETCH:
        if (pg_mode & (PG_MODE_NXE | PG_MODE_SMEP)) {
            error_code |= PG_ERROR_I_D_MASK;
        }
        break;
    }
    err->exception_index = EXCP0E_PAGE;
    err->error_code = error_code;
    err->cr2 = addr;
    return false;
}

hwaddr get_hphys(CPUState *cs, hwaddr gphys, MMUAccessType access_type,
                 int *prot)
{
    CPUX86State *env = &X86_CPU(cs)->env;

    if (likely(!(env->hflags2 & HF2_NPT_MASK))) {
        return gphys;
    } else {
        TranslateParams in = {
            .addr = gphys,
            .cr3 = env->nested_cr3,
            .pg_mode = env->nested_pg_mode,
            .mmu_idx = MMU_USER_IDX,
            .access_type = access_type,
            .use_stage2 = false,
        };
        TranslateResult out;
        TranslateFault err;
        uint64_t exit_info_1;

        if (mmu_translate(env, &in, &out, &err)) {
            if (prot) {
                *prot &= out.prot;
            }
            return out.paddr;
        }

        x86_stq_phys(cs, env->vm_vmcb +
                     offsetof(struct vmcb, control.exit_info_2), gphys);
        exit_info_1 = err.error_code
                    | (prot ? SVM_NPTEXIT_GPA : SVM_NPTEXIT_GPT);
        cpu_vmexit(env, SVM_EXIT_NPF, exit_info_1, env->retaddr);
    }
}

static bool get_physical_address(CPUX86State *env, vaddr addr,
                                 MMUAccessType access_type, int mmu_idx,
                                 TranslateResult *out, TranslateFault *err)
{
    if (!(env->cr[0] & CR0_PG_MASK)) {
        out->paddr = addr & x86_get_a20_mask(env);

#ifdef TARGET_X86_64
        if (!(env->hflags & HF_LMA_MASK)) {
            /* Without long mode we can only address 32bits in real mode */
            out->paddr = (uint32_t)out->paddr;
        }
#endif
        out->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        out->page_size = TARGET_PAGE_SIZE;
        return true;
    } else {
        TranslateParams in = {
            .addr = addr,
            .cr3 = env->cr[3],
            .pg_mode = get_pg_mode(env),
            .mmu_idx = mmu_idx,
            .access_type = access_type,
            .use_stage2 = true
        };

        if (in.pg_mode & PG_MODE_LMA) {
            /* test virtual address sign extension */
            int shift = in.pg_mode & PG_MODE_LA57 ? 56 : 47;
            int64_t sext = (int64_t)addr >> shift;
            if (sext != 0 && sext != -1) {
                err->exception_index = EXCP0D_GPF;
                err->error_code = 0;
                err->cr2 = addr;
                return false;
            }
        }
        return mmu_translate(env, &in, out, err);
    }
}

bool x86_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    CPUX86State *env = cs->env_ptr;
    TranslateResult out;
    TranslateFault err;

    if (get_physical_address(env, addr, access_type, mmu_idx, &out, &err)) {
        /*
         * Even if 4MB pages, we map only one 4KB page in the cache to
         * avoid filling it too fast.
         */
        assert(out.prot & (1 << access_type));
        tlb_set_page_with_attrs(cs, addr & TARGET_PAGE_MASK,
                                out.paddr & TARGET_PAGE_MASK,
                                cpu_get_mem_attrs(env),
                                out.prot, mmu_idx, out.page_size);
        return true;
    }

    /* FIXME: On error in get_hphys we have already jumped out.  */
    g_assert(!probe);

    if (env->intercept_exceptions & (1 << err.exception_index)) {
        /* cr2 is not modified in case of exceptions */
        x86_stq_phys(cs, env->vm_vmcb +
                     offsetof(struct vmcb, control.exit_info_2),
                     err.cr2);
    } else {
        env->cr[2] = err.cr2;
    }
    raise_exception_err_ra(env, err.exception_index, err.error_code, retaddr);
}

G_NORETURN void x86_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                            MMUAccessType access_type,
                                            int mmu_idx, uintptr_t retaddr)
{
    X86CPU *cpu = X86_CPU(cs);
    handle_unaligned_access(&cpu->env, vaddr, access_type, retaddr);
}
