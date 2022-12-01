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
    int ptw_idx;
    MMUAccessType access_type;
} TranslateParams;

typedef struct TranslateResult {
    hwaddr paddr;
    int prot;
    int page_size;
} TranslateResult;

typedef enum TranslateFaultStage2 {
    S2_NONE,
    S2_GPA,
    S2_GPT,
} TranslateFaultStage2;

typedef struct TranslateFault {
    int exception_index;
    int error_code;
    target_ulong cr2;
    TranslateFaultStage2 stage2;
} TranslateFault;

typedef struct PTETranslate {
    CPUX86State *env;
    TranslateFault *err;
    int ptw_idx;
    void *haddr;
    hwaddr gaddr;
} PTETranslate;

static bool ptw_translate(PTETranslate *inout, hwaddr addr)
{
    CPUTLBEntryFull *full;
    int flags;

    inout->gaddr = addr;
    flags = probe_access_full(inout->env, addr, MMU_DATA_STORE,
                              inout->ptw_idx, true, &inout->haddr, &full, 0);

    if (unlikely(flags & TLB_INVALID_MASK)) {
        TranslateFault *err = inout->err;

        assert(inout->ptw_idx == MMU_NESTED_IDX);
        *err = (TranslateFault){
            .error_code = inout->env->error_code,
            .cr2 = addr,
            .stage2 = S2_GPT,
        };
        return false;
    }
    return true;
}

static inline uint32_t ptw_ldl(const PTETranslate *in)
{
    if (likely(in->haddr)) {
        return ldl_p(in->haddr);
    }
    return cpu_ldl_mmuidx_ra(in->env, in->gaddr, in->ptw_idx, 0);
}

static inline uint64_t ptw_ldq(const PTETranslate *in)
{
    if (likely(in->haddr)) {
        return ldq_p(in->haddr);
    }
    return cpu_ldq_mmuidx_ra(in->env, in->gaddr, in->ptw_idx, 0);
}

/*
 * Note that we can use a 32-bit cmpxchg for all page table entries,
 * even 64-bit ones, because PG_PRESENT_MASK, PG_ACCESSED_MASK and
 * PG_DIRTY_MASK are all in the low 32 bits.
 */
static bool ptw_setl_slow(const PTETranslate *in, uint32_t old, uint32_t new)
{
    uint32_t cmp;

    /* Does x86 really perform a rmw cycle on mmio for ptw? */
    start_exclusive();
    cmp = cpu_ldl_mmuidx_ra(in->env, in->gaddr, in->ptw_idx, 0);
    if (cmp == old) {
        cpu_stl_mmuidx_ra(in->env, in->gaddr, new, in->ptw_idx, 0);
    }
    end_exclusive();
    return cmp == old;
}

static inline bool ptw_setl(const PTETranslate *in, uint32_t old, uint32_t set)
{
    if (set & ~old) {
        uint32_t new = old | set;
        if (likely(in->haddr)) {
            old = cpu_to_le32(old);
            new = cpu_to_le32(new);
            return qatomic_cmpxchg((uint32_t *)in->haddr, old, new) == old;
        }
        return ptw_setl_slow(in, old, new);
    }
    return true;
}

static bool mmu_translate(CPUX86State *env, const TranslateParams *in,
                          TranslateResult *out, TranslateFault *err)
{
    const int32_t a20_mask = x86_get_a20_mask(env);
    const target_ulong addr = in->addr;
    const int pg_mode = in->pg_mode;
    const bool is_user = (in->mmu_idx == MMU_USER_IDX);
    const MMUAccessType access_type = in->access_type;
    uint64_t ptep, pte, rsvd_mask;
    PTETranslate pte_trans = {
        .env = env,
        .err = err,
        .ptw_idx = in->ptw_idx,
    };
    hwaddr pte_addr, paddr;
    uint32_t pkr;
    int page_size;

 restart_all:
    rsvd_mask = ~MAKE_64BIT_MASK(0, env_archcpu(env)->phys_bits);
    rsvd_mask &= PG_ADDRESS_MASK;
    if (!(pg_mode & PG_MODE_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (pg_mode & PG_MODE_PAE) {
#ifdef TARGET_X86_64
        if (pg_mode & PG_MODE_LMA) {
            if (pg_mode & PG_MODE_LA57) {
                /*
                 * Page table level 5
                 */
                pte_addr = ((in->cr3 & ~0xfff) +
                            (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
                if (!ptw_translate(&pte_trans, pte_addr)) {
                    return false;
                }
            restart_5:
                pte = ptw_ldq(&pte_trans);
                if (!(pte & PG_PRESENT_MASK)) {
                    goto do_fault;
                }
                if (pte & (rsvd_mask | PG_PSE_MASK)) {
                    goto do_fault_rsvd;
                }
                if (!ptw_setl(&pte_trans, pte, PG_ACCESSED_MASK)) {
                    goto restart_5;
                }
                ptep = pte ^ PG_NX_MASK;
            } else {
                pte = in->cr3;
                ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
            }

            /*
             * Page table level 4
             */
            pte_addr = ((pte & PG_ADDRESS_MASK) +
                        (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
            if (!ptw_translate(&pte_trans, pte_addr)) {
                return false;
            }
        restart_4:
            pte = ptw_ldq(&pte_trans);
            if (!(pte & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pte & (rsvd_mask | PG_PSE_MASK)) {
                goto do_fault_rsvd;
            }
            if (!ptw_setl(&pte_trans, pte, PG_ACCESSED_MASK)) {
                goto restart_4;
            }
            ptep &= pte ^ PG_NX_MASK;

            /*
             * Page table level 3
             */
            pte_addr = ((pte & PG_ADDRESS_MASK) +
                        (((addr >> 30) & 0x1ff) << 3)) & a20_mask;
            if (!ptw_translate(&pte_trans, pte_addr)) {
                return false;
            }
        restart_3_lma:
            pte = ptw_ldq(&pte_trans);
            if (!(pte & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pte & rsvd_mask) {
                goto do_fault_rsvd;
            }
            if (!ptw_setl(&pte_trans, pte, PG_ACCESSED_MASK)) {
                goto restart_3_lma;
            }
            ptep &= pte ^ PG_NX_MASK;
            if (pte & PG_PSE_MASK) {
                /* 1 GB page */
                page_size = 1024 * 1024 * 1024;
                goto do_check_protect;
            }
        } else
#endif
        {
            /*
             * Page table level 3
             */
            pte_addr = ((in->cr3 & ~0x1f) + ((addr >> 27) & 0x18)) & a20_mask;
            if (!ptw_translate(&pte_trans, pte_addr)) {
                return false;
            }
            rsvd_mask |= PG_HI_USER_MASK;
        restart_3_nolma:
            pte = ptw_ldq(&pte_trans);
            if (!(pte & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pte & (rsvd_mask | PG_NX_MASK)) {
                goto do_fault_rsvd;
            }
            if (!ptw_setl(&pte_trans, pte, PG_ACCESSED_MASK)) {
                goto restart_3_nolma;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        /*
         * Page table level 2
         */
        pte_addr = ((pte & PG_ADDRESS_MASK) +
                    (((addr >> 21) & 0x1ff) << 3)) & a20_mask;
        if (!ptw_translate(&pte_trans, pte_addr)) {
            return false;
        }
    restart_2_pae:
        pte = ptw_ldq(&pte_trans);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pte & rsvd_mask) {
            goto do_fault_rsvd;
        }
        if (pte & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            ptep &= pte ^ PG_NX_MASK;
            goto do_check_protect;
        }
        if (!ptw_setl(&pte_trans, pte, PG_ACCESSED_MASK)) {
            goto restart_2_pae;
        }
        ptep &= pte ^ PG_NX_MASK;

        /*
         * Page table level 1
         */
        pte_addr = ((pte & PG_ADDRESS_MASK) +
                    (((addr >> 12) & 0x1ff) << 3)) & a20_mask;
        if (!ptw_translate(&pte_trans, pte_addr)) {
            return false;
        }
        pte = ptw_ldq(&pte_trans);
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
        /*
         * Page table level 2
         */
        pte_addr = ((in->cr3 & ~0xfff) + ((addr >> 20) & 0xffc)) & a20_mask;
        if (!ptw_translate(&pte_trans, pte_addr)) {
            return false;
        }
    restart_2_nopae:
        pte = ptw_ldl(&pte_trans);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pte | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pte & PG_PSE_MASK) && (pg_mode & PG_MODE_PSE)) {
            page_size = 4096 * 1024;
            /*
             * Bits 20-13 provide bits 39-32 of the address, bit 21 is reserved.
             * Leave bits 20-13 in place for setting accessed/dirty bits below.
             */
            pte = (uint32_t)pte | ((pte & 0x1fe000LL) << (32 - 13));
            rsvd_mask = 0x200000;
            goto do_check_protect_pse36;
        }
        if (!ptw_setl(&pte_trans, pte, PG_ACCESSED_MASK)) {
            goto restart_2_nopae;
        }

        /*
         * Page table level 1
         */
        pte_addr = ((pte & ~0xfffu) + ((addr >> 10) & 0xffc)) & a20_mask;
        if (!ptw_translate(&pte_trans, pte_addr)) {
            return false;
        }
        pte = ptw_ldl(&pte_trans);
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
        } else if (!(pte & PG_DIRTY_MASK)) {
            /*
             * Only set write access if already dirty...
             * otherwise wait for dirty access.
             */
            prot &= ~PAGE_WRITE;
        }
        if (!ptw_setl(&pte_trans, pte, set)) {
            /*
             * We can arrive here from any of 3 levels and 2 formats.
             * The only safe thing is to restart the entire lookup.
             */
            goto restart_all;
        }
    }

    /* align to page_size */
    paddr = (pte & a20_mask & PG_ADDRESS_MASK & ~(page_size - 1))
          | (addr & (page_size - 1));

    if (in->ptw_idx == MMU_NESTED_IDX) {
        CPUTLBEntryFull *full;
        int flags, nested_page_size;

        flags = probe_access_full(env, paddr, access_type,
                                  MMU_NESTED_IDX, true,
                                  &pte_trans.haddr, &full, 0);
        if (unlikely(flags & TLB_INVALID_MASK)) {
            *err = (TranslateFault){
                .error_code = env->error_code,
                .cr2 = paddr,
                .stage2 = S2_GPA,
            };
            return false;
        }

        /* Merge stage1 & stage2 protection bits. */
        prot &= full->prot;

        /* Re-verify resulting protection. */
        if ((prot & (1 << access_type)) == 0) {
            goto do_fault_protect;
        }

        /* Merge stage1 & stage2 addresses to final physical address. */
        nested_page_size = 1 << full->lg_page_size;
        paddr = (full->phys_addr & ~(nested_page_size - 1))
              | (paddr & (nested_page_size - 1));

        /*
         * Use the larger of stage1 & stage2 page sizes, so that
         * invalidation works.
         */
        if (nested_page_size > page_size) {
            page_size = nested_page_size;
        }
    }

    out->paddr = paddr;
    out->prot = prot;
    out->page_size = page_size;
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
    *err = (TranslateFault){
        .exception_index = EXCP0E_PAGE,
        .error_code = error_code,
        .cr2 = addr,
    };
    return false;
}

static G_NORETURN void raise_stage2(CPUX86State *env, TranslateFault *err,
                                    uintptr_t retaddr)
{
    uint64_t exit_info_1 = err->error_code;

    switch (err->stage2) {
    case S2_GPT:
        exit_info_1 |= SVM_NPTEXIT_GPT;
        break;
    case S2_GPA:
        exit_info_1 |= SVM_NPTEXIT_GPA;
        break;
    default:
        g_assert_not_reached();
    }

    x86_stq_phys(env_cpu(env),
                 env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                 err->cr2);
    cpu_vmexit(env, SVM_EXIT_NPF, exit_info_1, retaddr);
}

static bool get_physical_address(CPUX86State *env, vaddr addr,
                                 MMUAccessType access_type, int mmu_idx,
                                 TranslateResult *out, TranslateFault *err)
{
    TranslateParams in;
    bool use_stage2 = env->hflags2 & HF2_NPT_MASK;

    in.addr = addr;
    in.access_type = access_type;

    switch (mmu_idx) {
    case MMU_PHYS_IDX:
        break;

    case MMU_NESTED_IDX:
        if (likely(use_stage2)) {
            in.cr3 = env->nested_cr3;
            in.pg_mode = env->nested_pg_mode;
            in.mmu_idx = MMU_USER_IDX;
            in.ptw_idx = MMU_PHYS_IDX;

            if (!mmu_translate(env, &in, out, err)) {
                err->stage2 = S2_GPA;
                return false;
            }
            return true;
        }
        break;

    default:
        if (likely(env->cr[0] & CR0_PG_MASK)) {
            in.cr3 = env->cr[3];
            in.mmu_idx = mmu_idx;
            in.ptw_idx = use_stage2 ? MMU_NESTED_IDX : MMU_PHYS_IDX;
            in.pg_mode = get_pg_mode(env);

            if (in.pg_mode & PG_MODE_LMA) {
                /* test virtual address sign extension */
                int shift = in.pg_mode & PG_MODE_LA57 ? 56 : 47;
                int64_t sext = (int64_t)addr >> shift;
                if (sext != 0 && sext != -1) {
                    *err = (TranslateFault){
                        .exception_index = EXCP0D_GPF,
                        .cr2 = addr,
                    };
                    return false;
                }
            }
            return mmu_translate(env, &in, out, err);
        }
        break;
    }

    /* Translation disabled. */
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

    if (probe) {
        /* This will be used if recursing for stage2 translation. */
        env->error_code = err.error_code;
        return false;
    }

    if (err.stage2 != S2_NONE) {
        raise_stage2(env, &err, retaddr);
    }

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
