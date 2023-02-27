/*
 *  i386 helpers (without register variable usage)
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
#include "qapi/qapi-events-run-state.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "sysemu/runstate.h"
#include "kvm/kvm_i386.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/hw_accel.h"
#include "monitor/monitor.h"
#endif
#include "qemu/log.h"

void cpu_sync_avx_hflag(CPUX86State *env)
{
    if ((env->cr[4] & CR4_OSXSAVE_MASK)
        && (env->xcr0 & (XSTATE_SSE_MASK | XSTATE_YMM_MASK))
            == (XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
        env->hflags |= HF_AVX_EN_MASK;
    } else{
        env->hflags &= ~HF_AVX_EN_MASK;
    }
}

void cpu_sync_bndcs_hflags(CPUX86State *env)
{
    uint32_t hflags = env->hflags;
    uint32_t hflags2 = env->hflags2;
    uint32_t bndcsr;

    if ((hflags & HF_CPL_MASK) == 3) {
        bndcsr = env->bndcs_regs.cfgu;
    } else {
        bndcsr = env->msr_bndcfgs;
    }

    if ((env->cr[4] & CR4_OSXSAVE_MASK)
        && (env->xcr0 & XSTATE_BNDCSR_MASK)
        && (bndcsr & BNDCFG_ENABLE)) {
        hflags |= HF_MPX_EN_MASK;
    } else {
        hflags &= ~HF_MPX_EN_MASK;
    }

    if (bndcsr & BNDCFG_BNDPRESERVE) {
        hflags2 |= HF2_MPX_PR_MASK;
    } else {
        hflags2 &= ~HF2_MPX_PR_MASK;
    }

    env->hflags = hflags;
    env->hflags2 = hflags2;
}

static void cpu_x86_version(CPUX86State *env, int *family, int *model)
{
    int cpuver = env->cpuid_version;

    if (family == NULL || model == NULL) {
        return;
    }

    *family = (cpuver >> 8) & 0x0f;
    *model = ((cpuver >> 12) & 0xf0) + ((cpuver >> 4) & 0x0f);
}

/* Broadcast MCA signal for processor version 06H_EH and above */
int cpu_x86_support_mca_broadcast(CPUX86State *env)
{
    int family = 0;
    int model = 0;

    cpu_x86_version(env, &family, &model);
    if ((family == 6 && model >= 14) || family > 6) {
        return 1;
    }

    return 0;
}

/***********************************************************/
/* x86 mmu */
/* XXX: add PGE support */

void x86_cpu_set_a20(X86CPU *cpu, int a20_state)
{
    CPUX86State *env = &cpu->env;

    a20_state = (a20_state != 0);
    if (a20_state != ((env->a20_mask >> 20) & 1)) {
        CPUState *cs = CPU(cpu);

        qemu_log_mask(CPU_LOG_MMU, "A20 update: a20=%d\n", a20_state);
        /* if the cpu is currently executing code, we must unlink it and
           all the potentially executing TB */
        cpu_interrupt(cs, CPU_INTERRUPT_EXITTB);

        /* when a20 is changed, all the MMU mappings are invalid, so
           we must flush everything */
        tlb_flush(cs);
        env->a20_mask = ~(1 << 20) | (a20_state << 20);
    }
}

void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0)
{
    X86CPU *cpu = env_archcpu(env);
    int pe_state;

    qemu_log_mask(CPU_LOG_MMU, "CR0 update: CR0=0x%08x\n", new_cr0);
    if ((new_cr0 & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK)) !=
        (env->cr[0] & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK))) {
        tlb_flush(CPU(cpu));
    }

#ifdef TARGET_X86_64
    if (!(env->cr[0] & CR0_PG_MASK) && (new_cr0 & CR0_PG_MASK) &&
        (env->efer & MSR_EFER_LME)) {
        /* enter in long mode */
        /* XXX: generate an exception */
        if (!(env->cr[4] & CR4_PAE_MASK))
            return;
        env->efer |= MSR_EFER_LMA;
        env->hflags |= HF_LMA_MASK;
    } else if ((env->cr[0] & CR0_PG_MASK) && !(new_cr0 & CR0_PG_MASK) &&
               (env->efer & MSR_EFER_LMA)) {
        /* exit long mode */
        env->efer &= ~MSR_EFER_LMA;
        env->hflags &= ~(HF_LMA_MASK | HF_CS64_MASK);
        env->eip &= 0xffffffff;
    }
#endif
    env->cr[0] = new_cr0 | CR0_ET_MASK;

    /* update PE flag in hidden flags */
    pe_state = (env->cr[0] & CR0_PE_MASK);
    env->hflags = (env->hflags & ~HF_PE_MASK) | (pe_state << HF_PE_SHIFT);
    /* ensure that ADDSEG is always set in real mode */
    env->hflags |= ((pe_state ^ 1) << HF_ADDSEG_SHIFT);
    /* update FPU flags */
    env->hflags = (env->hflags & ~(HF_MP_MASK | HF_EM_MASK | HF_TS_MASK)) |
        ((new_cr0 << (HF_MP_SHIFT - 1)) & (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK));
}

/* XXX: in legacy PAE mode, generate a GPF if reserved bits are set in
   the PDPT */
void cpu_x86_update_cr3(CPUX86State *env, target_ulong new_cr3)
{
    env->cr[3] = new_cr3;
    if (env->cr[0] & CR0_PG_MASK) {
        qemu_log_mask(CPU_LOG_MMU,
                        "CR3 update: CR3=" TARGET_FMT_lx "\n", new_cr3);
        tlb_flush(env_cpu(env));
    }
}

void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4)
{
    uint32_t hflags;

#if defined(DEBUG_MMU)
    printf("CR4 update: %08x -> %08x\n", (uint32_t)env->cr[4], new_cr4);
#endif
    if ((new_cr4 ^ env->cr[4]) &
        (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK |
         CR4_SMEP_MASK | CR4_SMAP_MASK | CR4_LA57_MASK)) {
        tlb_flush(env_cpu(env));
    }

    /* Clear bits we're going to recompute.  */
    hflags = env->hflags & ~(HF_OSFXSR_MASK | HF_SMAP_MASK | HF_UMIP_MASK);

    /* SSE handling */
    if (!(env->features[FEAT_1_EDX] & CPUID_SSE)) {
        new_cr4 &= ~CR4_OSFXSR_MASK;
    }
    if (new_cr4 & CR4_OSFXSR_MASK) {
        hflags |= HF_OSFXSR_MASK;
    }

    if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SMAP)) {
        new_cr4 &= ~CR4_SMAP_MASK;
    }
    if (new_cr4 & CR4_SMAP_MASK) {
        hflags |= HF_SMAP_MASK;
    }
    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_UMIP)) {
        new_cr4 &= ~CR4_UMIP_MASK;
    }
    if (new_cr4 & CR4_UMIP_MASK) {
        hflags |= HF_UMIP_MASK;
    }

    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKU)) {
        new_cr4 &= ~CR4_PKE_MASK;
    }
    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKS)) {
        new_cr4 &= ~CR4_PKS_MASK;
    }

    env->cr[4] = new_cr4;
    env->hflags = hflags;

    cpu_sync_bndcs_hflags(env);
    cpu_sync_avx_hflag(env);
}

#if !defined(CONFIG_USER_ONLY)
hwaddr x86_cpu_get_phys_page_attrs_debug(CPUState *cs, vaddr addr,
                                         MemTxAttrs *attrs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    target_ulong pde_addr, pte_addr;
    uint64_t pte;
    int32_t a20_mask;
    uint32_t page_offset;
    int page_size;

    *attrs = cpu_get_mem_attrs(env);

    a20_mask = x86_get_a20_mask(env);
    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr & a20_mask;
        page_size = 4096;
    } else if (env->cr[4] & CR4_PAE_MASK) {
        target_ulong pdpe_addr;
        uint64_t pde, pdpe;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            bool la57 = env->cr[4] & CR4_LA57_MASK;
            uint64_t pml5e_addr, pml5e;
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = la57 ? (int64_t)addr >> 56 : (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                return -1;
            }

            if (la57) {
                pml5e_addr = ((env->cr[3] & ~0xfff) +
                        (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
                pml5e = x86_ldq_phys(cs, pml5e_addr);
                if (!(pml5e & PG_PRESENT_MASK)) {
                    return -1;
                }
            } else {
                pml5e = env->cr[3];
            }

            pml4e_addr = ((pml5e & PG_ADDRESS_MASK) +
                    (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                return -1;
            }
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) +
                         (((addr >> 30) & 0x1ff) << 3)) & a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                return -1;
            }
            if (pdpe & PG_PSE_MASK) {
                page_size = 1024 * 1024 * 1024;
                pte = pdpe;
                goto out;
            }

        } else
#endif
        {
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) +
                    (((addr >> 21) & 0x1ff) << 3)) & a20_mask;
        pde = x86_ldq_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            return -1;
        }
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte = pde;
        } else {
            /* 4 KB page */
            pte_addr = ((pde & PG_ADDRESS_MASK) +
                        (((addr >> 12) & 0x1ff) << 3)) & a20_mask;
            page_size = 4096;
            pte = x86_ldq_phys(cs, pte_addr);
        }
        if (!(pte & PG_PRESENT_MASK)) {
            return -1;
        }
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) & a20_mask;
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK))
            return -1;
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            page_size = 4096 * 1024;
        } else {
            /* page directory entry */
            pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & a20_mask;
            pte = x86_ldl_phys(cs, pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                return -1;
            }
            page_size = 4096;
        }
        pte = pte & a20_mask;
    }

#ifdef TARGET_X86_64
out:
#endif
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);
    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    return pte | page_offset;
}

typedef struct MCEInjectionParams {
    Monitor *mon;
    int bank;
    uint64_t status;
    uint64_t mcg_status;
    uint64_t addr;
    uint64_t misc;
    int flags;
} MCEInjectionParams;

static void emit_guest_memory_failure(MemoryFailureAction action, bool ar,
                                      bool recursive)
{
    MemoryFailureFlags mff = {.action_required = ar, .recursive = recursive};

    qapi_event_send_memory_failure(MEMORY_FAILURE_RECIPIENT_GUEST, action,
                                   &mff);
}

static void do_inject_x86_mce(CPUState *cs, run_on_cpu_data data)
{
    MCEInjectionParams *params = data.host_ptr;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *cenv = &cpu->env;
    uint64_t *banks = cenv->mce_banks + 4 * params->bank;
    g_autofree char *msg = NULL;
    bool need_reset = false;
    bool recursive;
    bool ar = !!(params->status & MCI_STATUS_AR);

    cpu_synchronize_state(cs);
    recursive = !!(cenv->mcg_status & MCG_STATUS_MCIP);

    /*
     * If there is an MCE exception being processed, ignore this SRAO MCE
     * unless unconditional injection was requested.
     */
    if (!(params->flags & MCE_INJECT_UNCOND_AO) && !ar && recursive) {
        emit_guest_memory_failure(MEMORY_FAILURE_ACTION_IGNORE, ar, recursive);
        return;
    }

    if (params->status & MCI_STATUS_UC) {
        /*
         * if MSR_MCG_CTL is not all 1s, the uncorrected error
         * reporting is disabled
         */
        if ((cenv->mcg_cap & MCG_CTL_P) && cenv->mcg_ctl != ~(uint64_t)0) {
            monitor_printf(params->mon,
                           "CPU %d: Uncorrected error reporting disabled\n",
                           cs->cpu_index);
            return;
        }

        /*
         * if MSR_MCi_CTL is not all 1s, the uncorrected error
         * reporting is disabled for the bank
         */
        if (banks[0] != ~(uint64_t)0) {
            monitor_printf(params->mon,
                           "CPU %d: Uncorrected error reporting disabled for"
                           " bank %d\n",
                           cs->cpu_index, params->bank);
            return;
        }

        if (!(cenv->cr[4] & CR4_MCE_MASK)) {
            need_reset = true;
            msg = g_strdup_printf("CPU %d: MCE capability is not enabled, "
                                  "raising triple fault", cs->cpu_index);
        } else if (recursive) {
            need_reset = true;
            msg = g_strdup_printf("CPU %d: Previous MCE still in progress, "
                                  "raising triple fault", cs->cpu_index);
        }

        if (need_reset) {
            emit_guest_memory_failure(MEMORY_FAILURE_ACTION_RESET, ar,
                                      recursive);
            monitor_puts(params->mon, msg);
            qemu_log_mask(CPU_LOG_RESET, "%s\n", msg);
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return;
        }

        if (banks[1] & MCI_STATUS_VAL) {
            params->status |= MCI_STATUS_OVER;
        }
        banks[2] = params->addr;
        banks[3] = params->misc;
        cenv->mcg_status = params->mcg_status;
        banks[1] = params->status;
        cpu_interrupt(cs, CPU_INTERRUPT_MCE);
    } else if (!(banks[1] & MCI_STATUS_VAL)
               || !(banks[1] & MCI_STATUS_UC)) {
        if (banks[1] & MCI_STATUS_VAL) {
            params->status |= MCI_STATUS_OVER;
        }
        banks[2] = params->addr;
        banks[3] = params->misc;
        banks[1] = params->status;
    } else {
        banks[1] |= MCI_STATUS_OVER;
    }

    emit_guest_memory_failure(MEMORY_FAILURE_ACTION_INJECT, ar, recursive);
}

void cpu_x86_inject_mce(Monitor *mon, X86CPU *cpu, int bank,
                        uint64_t status, uint64_t mcg_status, uint64_t addr,
                        uint64_t misc, int flags)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *cenv = &cpu->env;
    MCEInjectionParams params = {
        .mon = mon,
        .bank = bank,
        .status = status,
        .mcg_status = mcg_status,
        .addr = addr,
        .misc = misc,
        .flags = flags,
    };
    unsigned bank_num = cenv->mcg_cap & 0xff;

    if (!cenv->mcg_cap) {
        monitor_printf(mon, "MCE injection not supported\n");
        return;
    }
    if (bank >= bank_num) {
        monitor_printf(mon, "Invalid MCE bank number\n");
        return;
    }
    if (!(status & MCI_STATUS_VAL)) {
        monitor_printf(mon, "Invalid MCE status code\n");
        return;
    }
    if ((flags & MCE_INJECT_BROADCAST)
        && !cpu_x86_support_mca_broadcast(cenv)) {
        monitor_printf(mon, "Guest CPU does not support MCA broadcast\n");
        return;
    }

    run_on_cpu(cs, do_inject_x86_mce, RUN_ON_CPU_HOST_PTR(&params));
    if (flags & MCE_INJECT_BROADCAST) {
        CPUState *other_cs;

        params.bank = 1;
        params.status = MCI_STATUS_VAL | MCI_STATUS_UC;
        params.mcg_status = MCG_STATUS_MCIP | MCG_STATUS_RIPV;
        params.addr = 0;
        params.misc = 0;
        CPU_FOREACH(other_cs) {
            if (other_cs == cs) {
                continue;
            }
            run_on_cpu(other_cs, do_inject_x86_mce, RUN_ON_CPU_HOST_PTR(&params));
        }
    }
}

static inline target_ulong get_memio_eip(CPUX86State *env)
{
#ifdef CONFIG_TCG
    uint64_t data[TARGET_INSN_START_WORDS];
    CPUState *cs = env_cpu(env);

    if (!cpu_unwind_state_data(cs, cs->mem_io_pc, data)) {
        return env->eip;
    }

    /* Per x86_restore_state_to_opc. */
    if (cs->tcg_cflags & CF_PCREL) {
        return (env->eip & TARGET_PAGE_MASK) | data[0];
    } else {
        return data[0] - env->segs[R_CS].base;
    }
#else
    qemu_build_not_reached();
#endif
}

void cpu_report_tpr_access(CPUX86State *env, TPRAccess access)
{
    X86CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (kvm_enabled() || whpx_enabled() || nvmm_enabled()) {
        env->tpr_access_type = access;

        cpu_interrupt(cs, CPU_INTERRUPT_TPR);
    } else if (tcg_enabled()) {
        target_ulong eip = get_memio_eip(env);

        apic_handle_tpr_access_report(cpu->apic_state, eip, access);
    }
}
#endif /* !CONFIG_USER_ONLY */

int cpu_x86_get_descr_debug(CPUX86State *env, unsigned int selector,
                            target_ulong *base, unsigned int *limit,
                            unsigned int *flags)
{
    CPUState *cs = env_cpu(env);
    SegmentCache *dt;
    target_ulong ptr;
    uint32_t e1, e2;
    int index;

    if (selector & 0x4)
        dt = &env->ldt;
    else
        dt = &env->gdt;
    index = selector & ~7;
    ptr = dt->base + index;
    if ((index + 7) > dt->limit
        || cpu_memory_rw_debug(cs, ptr, (uint8_t *)&e1, sizeof(e1), 0) != 0
        || cpu_memory_rw_debug(cs, ptr+4, (uint8_t *)&e2, sizeof(e2), 0) != 0)
        return 0;

    *base = ((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
    *limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK)
        *limit = (*limit << 12) | 0xfff;
    *flags = e2;

    return 1;
}

#if !defined(CONFIG_USER_ONLY)
void do_cpu_init(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    CPUX86State *save = g_new(CPUX86State, 1);
    int sipi = cs->interrupt_request & CPU_INTERRUPT_SIPI;

    *save = *env;

    cpu_reset(cs);
    cs->interrupt_request = sipi;
    memcpy(&env->start_init_save, &save->start_init_save,
           offsetof(CPUX86State, end_init_save) -
           offsetof(CPUX86State, start_init_save));
    g_free(save);

    if (kvm_enabled()) {
        kvm_arch_do_init_vcpu(cpu);
    }
    apic_init_reset(cpu->apic_state);
}

void do_cpu_sipi(X86CPU *cpu)
{
    apic_sipi(cpu->apic_state);
}
#else
void do_cpu_init(X86CPU *cpu)
{
}
void do_cpu_sipi(X86CPU *cpu)
{
}
#endif

#ifndef CONFIG_USER_ONLY

void cpu_load_efer(CPUX86State *env, uint64_t val)
{
    env->efer = val;
    env->hflags &= ~(HF_LMA_MASK | HF_SVME_MASK);
    if (env->efer & MSR_EFER_LMA) {
        env->hflags |= HF_LMA_MASK;
    }
    if (env->efer & MSR_EFER_SVME) {
        env->hflags |= HF_SVME_MASK;
    }
}

uint8_t x86_ldub_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldub(as, addr, attrs, NULL);
}

uint32_t x86_lduw_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_lduw(as, addr, attrs, NULL);
}

uint32_t x86_ldl_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldl(as, addr, attrs, NULL);
}

uint64_t x86_ldq_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldq(as, addr, attrs, NULL);
}

void x86_stb_phys(CPUState *cs, hwaddr addr, uint8_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stb(as, addr, val, attrs, NULL);
}

void x86_stl_phys_notdirty(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stl_notdirty(as, addr, val, attrs, NULL);
}

void x86_stw_phys(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stw(as, addr, val, attrs, NULL);
}

void x86_stl_phys(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stl(as, addr, val, attrs, NULL);
}

void x86_stq_phys(CPUState *cs, hwaddr addr, uint64_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stq(as, addr, val, attrs, NULL);
}
#endif
