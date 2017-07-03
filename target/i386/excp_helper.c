/*
 *  x86 exception helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "exec/helper-proto.h"

void helper_raise_interrupt(CPUX86State *env, int intno, int next_eip_addend)
{
    raise_interrupt(env, intno, 1, 0, next_eip_addend);
}

void helper_raise_exception(CPUX86State *env, int exception_index)
{
    raise_exception(env, exception_index);
}

/*
 * Check nested exceptions and change to double or triple fault if
 * needed. It should only be called, if this is not an interrupt.
 * Returns the new exception number.
 */
static int check_exception(CPUX86State *env, int intno, int *error_code,
                           uintptr_t retaddr)
{
    int first_contributory = env->old_exception == 0 ||
                              (env->old_exception >= 10 &&
                               env->old_exception <= 13);
    int second_contributory = intno == 0 ||
                               (intno >= 10 && intno <= 13);

    qemu_log_mask(CPU_LOG_INT, "check_exception old: 0x%x new 0x%x\n",
                env->old_exception, intno);

#if !defined(CONFIG_USER_ONLY)
    if (env->old_exception == EXCP08_DBLE) {
        if (env->hflags & HF_SVMI_MASK) {
            cpu_vmexit(env, SVM_EXIT_SHUTDOWN, 0, retaddr); /* does not return */
        }

        qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");

        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return EXCP_HLT;
    }
#endif

    if ((first_contributory && second_contributory)
        || (env->old_exception == EXCP0E_PAGE &&
            (second_contributory || (intno == EXCP0E_PAGE)))) {
        intno = EXCP08_DBLE;
        *error_code = 0;
    }

    if (second_contributory || (intno == EXCP0E_PAGE) ||
        (intno == EXCP08_DBLE)) {
        env->old_exception = intno;
    }

    return intno;
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * env->eip value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.
 */
static void QEMU_NORETURN raise_interrupt2(CPUX86State *env, int intno,
                                           int is_int, int error_code,
                                           int next_eip_addend,
                                           uintptr_t retaddr)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    if (!is_int) {
        cpu_svm_check_intercept_param(env, SVM_EXIT_EXCP_BASE + intno,
                                      error_code, retaddr);
        intno = check_exception(env, intno, &error_code, retaddr);
    } else {
        cpu_svm_check_intercept_param(env, SVM_EXIT_SWINT, 0, retaddr);
    }

    cs->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit_restore(cs, retaddr);
}

/* shortcuts to generate exceptions */

void QEMU_NORETURN raise_interrupt(CPUX86State *env, int intno, int is_int,
                                   int error_code, int next_eip_addend)
{
    raise_interrupt2(env, intno, is_int, error_code, next_eip_addend, 0);
}

void raise_exception_err(CPUX86State *env, int exception_index,
                         int error_code)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, 0);
}

void raise_exception_err_ra(CPUX86State *env, int exception_index,
                            int error_code, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, retaddr);
}

void raise_exception(CPUX86State *env, int exception_index)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, 0);
}

void raise_exception_ra(CPUX86State *env, int exception_index, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, retaddr);
}

#if defined(CONFIG_USER_ONLY)
int x86_cpu_handle_mmu_fault(CPUState *cs, vaddr addr,
                             int is_write, int mmu_idx)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    /* user mode only emulation */
    is_write &= 1;
    env->cr[2] = addr;
    env->error_code = (is_write << PG_ERROR_W_BIT);
    env->error_code |= PG_ERROR_U_MASK;
    cs->exception_index = EXCP0E_PAGE;
    env->exception_is_int = 0;
    env->exception_next_eip = -1;
    return 1;
}

#else

/* return value:
 * -1 = cannot handle fault
 * 0  = nothing more to do
 * 1  = generate PF fault
 */
int x86_cpu_handle_mmu_fault(CPUState *cs, vaddr addr,
                             int is_write1, int mmu_idx)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t ptep, pte;
    int32_t a20_mask;
    target_ulong pde_addr, pte_addr;
    int error_code = 0;
    int is_dirty, prot, page_size, is_write, is_user;
    hwaddr paddr;
    uint64_t rsvd_mask = PG_HI_RSVD_MASK;
    uint32_t page_offset;
    target_ulong vaddr;

    is_user = mmu_idx == MMU_USER_IDX;
#if defined(DEBUG_MMU)
    printf("MMU fault: addr=%" VADDR_PRIx " w=%d u=%d eip=" TARGET_FMT_lx "\n",
           addr, is_write1, is_user, env->eip);
#endif
    is_write = is_write1 & 1;

    a20_mask = x86_get_a20_mask(env);
    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
#ifdef TARGET_X86_64
        if (!(env->hflags & HF_LMA_MASK)) {
            /* Without long mode we can only address 32bits in real mode */
            pte = (uint32_t)pte;
        }
#endif
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = 4096;
        goto do_mapping;
    }

    if (!(env->efer & MSR_EFER_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (env->cr[4] & CR4_PAE_MASK) {
        uint64_t pde, pdpe;
        target_ulong pdpe_addr;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            bool la57 = env->cr[4] & CR4_LA57_MASK;
            uint64_t pml5e_addr, pml5e;
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = la57 ? (int64_t)addr >> 56 : (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                env->error_code = 0;
                cs->exception_index = EXCP0D_GPF;
                return 1;
            }

            if (la57) {
                pml5e_addr = ((env->cr[3] & ~0xfff) +
                        (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
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
                pml5e = env->cr[3];
                ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
            }

            pml4e_addr = ((pml5e & PG_ADDRESS_MASK) +
                    (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
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
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                a20_mask;
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
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) &
            a20_mask;
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pde | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
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

    prot = 0;
    if (mmu_idx != MMU_KSMAP_IDX || !(ptep & PG_USER_MASK)) {
        prot |= PAGE_READ;
        if ((ptep & PG_RW_MASK) || (!is_user && !(env->cr[0] & CR0_WP_MASK))) {
            prot |= PAGE_WRITE;
        }
    }
    if (!(ptep & PG_NX_MASK) &&
        (mmu_idx == MMU_USER_IDX ||
         !((env->cr[4] & CR4_SMEP_MASK) && (ptep & PG_USER_MASK)))) {
        prot |= PAGE_EXEC;
    }
    if ((env->cr[4] & CR4_PKE_MASK) && (env->hflags & HF_LMA_MASK) &&
        (ptep & PG_USER_MASK) && env->pkru) {
        uint32_t pk = (pte & PG_PKRU_MASK) >> PG_PKRU_BIT;
        uint32_t pkru_ad = (env->pkru >> pk * 2) & 1;
        uint32_t pkru_wd = (env->pkru >> pk * 2) & 2;
        uint32_t pkru_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

        if (pkru_ad) {
            pkru_prot &= ~(PAGE_READ | PAGE_WRITE);
        } else if (pkru_wd && (is_user || env->cr[0] & CR0_WP_MASK)) {
            pkru_prot &= ~PAGE_WRITE;
        }

        prot &= pkru_prot;
        if ((pkru_prot & (1 << is_write1)) == 0) {
            assert(is_write1 != 2);
            error_code |= PG_ERROR_PK_MASK;
            goto do_fault_protect;
        }
    }

    if ((prot & (1 << is_write1)) == 0) {
        goto do_fault_protect;
    }

    /* yes, it can! */
    is_dirty = is_write && !(pte & PG_DIRTY_MASK);
    if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
        pte |= PG_ACCESSED_MASK;
        if (is_dirty) {
            pte |= PG_DIRTY_MASK;
        }
        x86_stl_phys_notdirty(cs, pte_addr, pte);
    }

    if (!(pte & PG_DIRTY_MASK)) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        assert(!is_write);
        prot &= ~PAGE_WRITE;
    }

 do_mapping:
    pte = pte & a20_mask;

    /* align to page_size */
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);

    /* Even if 4MB pages, we map only one 4KB page in the cache to
       avoid filling it too fast */
    vaddr = addr & TARGET_PAGE_MASK;
    page_offset = vaddr & (page_size - 1);
    paddr = pte + page_offset;

    assert(prot & (1 << is_write1));
    tlb_set_page_with_attrs(cs, vaddr, paddr, cpu_get_mem_attrs(env),
                            prot, mmu_idx, page_size);
    return 0;
 do_fault_rsvd:
    error_code |= PG_ERROR_RSVD_MASK;
 do_fault_protect:
    error_code |= PG_ERROR_P_MASK;
 do_fault:
    error_code |= (is_write << PG_ERROR_W_BIT);
    if (is_user)
        error_code |= PG_ERROR_U_MASK;
    if (is_write1 == 2 &&
        (((env->efer & MSR_EFER_NXE) &&
          (env->cr[4] & CR4_PAE_MASK)) ||
         (env->cr[4] & CR4_SMEP_MASK)))
        error_code |= PG_ERROR_I_D_MASK;
    if (env->intercept_exceptions & (1 << EXCP0E_PAGE)) {
        /* cr2 is not modified in case of exceptions */
        x86_stq_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                 addr);
    } else {
        env->cr[2] = addr;
    }
    env->error_code = error_code;
    cs->exception_index = EXCP0E_PAGE;
    return 1;
}
#endif
