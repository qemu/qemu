/*
 *  x86 misc helpers - sysemu code
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include "tcg/helper-tcg.h"
#include "hw/i386/apic.h"

void helper_outb(CPUX86State *env, uint32_t port, uint32_t data)
{
    address_space_stb(&address_space_io, port, data,
                      cpu_get_mem_attrs(env), NULL);
}

target_ulong helper_inb(CPUX86State *env, uint32_t port)
{
    return address_space_ldub(&address_space_io, port,
                              cpu_get_mem_attrs(env), NULL);
}

void helper_outw(CPUX86State *env, uint32_t port, uint32_t data)
{
    address_space_stw(&address_space_io, port, data,
                      cpu_get_mem_attrs(env), NULL);
}

target_ulong helper_inw(CPUX86State *env, uint32_t port)
{
    return address_space_lduw(&address_space_io, port,
                              cpu_get_mem_attrs(env), NULL);
}

void helper_outl(CPUX86State *env, uint32_t port, uint32_t data)
{
    address_space_stl(&address_space_io, port, data,
                      cpu_get_mem_attrs(env), NULL);
}

target_ulong helper_inl(CPUX86State *env, uint32_t port)
{
    return address_space_ldl(&address_space_io, port,
                             cpu_get_mem_attrs(env), NULL);
}

target_ulong helper_read_crN(CPUX86State *env, int reg)
{
    target_ulong val;

    switch (reg) {
    default:
        val = env->cr[reg];
        break;
    case 8:
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
            val = cpu_get_apic_tpr(env_archcpu(env)->apic_state);
        } else {
            val = env->int_ctl & V_TPR_MASK;
        }
        break;
    }
    return val;
}

void helper_write_crN(CPUX86State *env, int reg, target_ulong t0)
{
    switch (reg) {
    case 0:
        /*
        * If we reach this point, the CR0 write intercept is disabled.
        * But we could still exit if the hypervisor has requested the selective
        * intercept for bits other than TS and MP
        */
        if (cpu_svm_has_intercept(env, SVM_EXIT_CR0_SEL_WRITE) &&
            ((env->cr[0] ^ t0) & ~(CR0_TS_MASK | CR0_MP_MASK))) {
            cpu_vmexit(env, SVM_EXIT_CR0_SEL_WRITE, 0, GETPC());
        }
        cpu_x86_update_cr0(env, t0);
        break;
    case 3:
        if ((env->efer & MSR_EFER_LMA) &&
                (t0 & ((~0ULL) << env_archcpu(env)->phys_bits))) {
            cpu_vmexit(env, SVM_EXIT_ERR, 0, GETPC());
        }
        if (!(env->efer & MSR_EFER_LMA)) {
            t0 &= 0xffffffffUL;
        }
        cpu_x86_update_cr3(env, t0);
        break;
    case 4:
        if (t0 & cr4_reserved_bits(env)) {
            cpu_vmexit(env, SVM_EXIT_ERR, 0, GETPC());
        }
        if (((t0 ^ env->cr[4]) & CR4_LA57_MASK) &&
            (env->hflags & HF_CS64_MASK)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        cpu_x86_update_cr4(env, t0);
        break;
    case 8:
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
            bql_lock();
            cpu_set_apic_tpr(env_archcpu(env)->apic_state, t0);
            bql_unlock();
        }
        env->int_ctl = (env->int_ctl & ~V_TPR_MASK) | (t0 & V_TPR_MASK);

        CPUState *cs = env_cpu(env);
        if (ctl_has_irq(env)) {
            cpu_interrupt(cs, CPU_INTERRUPT_VIRQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VIRQ);
        }
        break;
    default:
        env->cr[reg] = t0;
        break;
    }
}

void helper_wrmsr(CPUX86State *env)
{
    uint64_t val;
    CPUState *cs = env_cpu(env);

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 1, GETPC());

    val = ((uint32_t)env->regs[R_EAX]) |
        ((uint64_t)((uint32_t)env->regs[R_EDX]) << 32);

    switch ((uint32_t)env->regs[R_ECX]) {
    case MSR_IA32_SYSENTER_CS:
        env->sysenter_cs = val & 0xffff;
        break;
    case MSR_IA32_SYSENTER_ESP:
        env->sysenter_esp = val;
        break;
    case MSR_IA32_SYSENTER_EIP:
        env->sysenter_eip = val;
        break;
    case MSR_IA32_APICBASE: {
        int ret;

        if (val & MSR_IA32_APICBASE_RESERVED) {
            goto error;
        }

        ret = cpu_set_apic_base(env_archcpu(env)->apic_state, val);
        if (ret < 0) {
            goto error;
        }
        break;
    }
    case MSR_EFER:
        {
            uint64_t update_mask;

            update_mask = 0;
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_SYSCALL) {
                update_mask |= MSR_EFER_SCE;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
                update_mask |= MSR_EFER_LME;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_NX) {
                update_mask |= MSR_EFER_NXE;
            }
            if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
                update_mask |= MSR_EFER_SVME;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            cpu_load_efer(env, (env->efer & ~update_mask) |
                          (val & update_mask));
        }
        break;
    case MSR_STAR:
        env->star = val;
        break;
    case MSR_PAT:
        env->pat = val;
        break;
    case MSR_IA32_PKRS:
        if (val & 0xFFFFFFFF00000000ull) {
            goto error;
        }
        env->pkrs = val;
        tlb_flush(cs);
        break;
    case MSR_VM_HSAVE_PA:
        if (val & (0xfff | ((~0ULL) << env_archcpu(env)->phys_bits))) {
            goto error;
        }
        env->vm_hsave = val;
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        env->lstar = val;
        break;
    case MSR_CSTAR:
        env->cstar = val;
        break;
    case MSR_FMASK:
        env->fmask = val;
        break;
    case MSR_FSBASE:
        env->segs[R_FS].base = val;
        break;
    case MSR_GSBASE:
        env->segs[R_GS].base = val;
        break;
    case MSR_KERNELGSBASE:
        env->kernelgsbase = val;
        break;
#endif
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                       MSR_MTRRphysBase(0)) / 2].base = val;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                       MSR_MTRRphysMask(0)) / 2].mask = val;
        break;
    case MSR_MTRRfix64K_00000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix64K_00000] = val;
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix16K_80000 + 1] = val;
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix4K_C0000 + 3] = val;
        break;
    case MSR_MTRRdefType:
        env->mtrr_deftype = val;
        break;
    case MSR_MCG_STATUS:
        env->mcg_status = val;
        break;
    case MSR_MCG_CTL:
        if ((env->mcg_cap & MCG_CTL_P)
            && (val == 0 || val == ~(uint64_t)0)) {
            env->mcg_ctl = val;
        }
        break;
    case MSR_TSC_AUX:
        env->tsc_aux = val;
        break;
    case MSR_IA32_MISC_ENABLE:
        env->msr_ia32_misc_enable = val;
        break;
    case MSR_IA32_BNDCFGS:
        /* FIXME: #GP if reserved bits are set.  */
        /* FIXME: Extend highest implemented bit of linear address.  */
        env->msr_bndcfgs = val;
        cpu_sync_bndcs_hflags(env);
        break;
    case MSR_APIC_START ... MSR_APIC_END: {
        int ret;
        int index = (uint32_t)env->regs[R_ECX] - MSR_APIC_START;

        bql_lock();
        ret = apic_msr_write(index, val);
        bql_unlock();
        if (ret < 0) {
            goto error;
        }

        break;
    }
    default:
        if ((uint32_t)env->regs[R_ECX] >= MSR_MC0_CTL
            && (uint32_t)env->regs[R_ECX] < MSR_MC0_CTL +
            (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)env->regs[R_ECX] - MSR_MC0_CTL;
            if ((offset & 0x3) != 0
                || (val == 0 || val == ~(uint64_t)0)) {
                env->mce_banks[offset] = val;
            }
            break;
        }
        /* XXX: exception? */
        break;
    }
    return;
error:
    raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
}

void helper_rdmsr(CPUX86State *env)
{
    X86CPU *x86_cpu = env_archcpu(env);
    uint64_t val;

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 0, GETPC());

    switch ((uint32_t)env->regs[R_ECX]) {
    case MSR_IA32_SYSENTER_CS:
        val = env->sysenter_cs;
        break;
    case MSR_IA32_SYSENTER_ESP:
        val = env->sysenter_esp;
        break;
    case MSR_IA32_SYSENTER_EIP:
        val = env->sysenter_eip;
        break;
    case MSR_IA32_APICBASE:
        val = cpu_get_apic_base(env_archcpu(env)->apic_state);
        break;
    case MSR_EFER:
        val = env->efer;
        break;
    case MSR_STAR:
        val = env->star;
        break;
    case MSR_PAT:
        val = env->pat;
        break;
    case MSR_IA32_PKRS:
        val = env->pkrs;
        break;
    case MSR_VM_HSAVE_PA:
        val = env->vm_hsave;
        break;
    case MSR_IA32_PERF_STATUS:
        /* tsc_increment_by_tick */
        val = 1000ULL;
        /* CPU multiplier */
        val |= (((uint64_t)4ULL) << 40);
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        val = env->lstar;
        break;
    case MSR_CSTAR:
        val = env->cstar;
        break;
    case MSR_FMASK:
        val = env->fmask;
        break;
    case MSR_FSBASE:
        val = env->segs[R_FS].base;
        break;
    case MSR_GSBASE:
        val = env->segs[R_GS].base;
        break;
    case MSR_KERNELGSBASE:
        val = env->kernelgsbase;
        break;
    case MSR_TSC_AUX:
        val = env->tsc_aux;
        break;
#endif
    case MSR_SMI_COUNT:
        val = env->msr_smi_count;
        break;
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        val = env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                             MSR_MTRRphysBase(0)) / 2].base;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        val = env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                             MSR_MTRRphysMask(0)) / 2].mask;
        break;
    case MSR_MTRRfix64K_00000:
        val = env->mtrr_fixed[0];
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        val = env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                              MSR_MTRRfix16K_80000 + 1];
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        val = env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                              MSR_MTRRfix4K_C0000 + 3];
        break;
    case MSR_MTRRdefType:
        val = env->mtrr_deftype;
        break;
    case MSR_MTRRcap:
        if (env->features[FEAT_1_EDX] & CPUID_MTRR) {
            val = MSR_MTRRcap_VCNT | MSR_MTRRcap_FIXRANGE_SUPPORT |
                MSR_MTRRcap_WC_SUPPORTED;
        } else {
            /* XXX: exception? */
            val = 0;
        }
        break;
    case MSR_MCG_CAP:
        val = env->mcg_cap;
        break;
    case MSR_MCG_CTL:
        if (env->mcg_cap & MCG_CTL_P) {
            val = env->mcg_ctl;
        } else {
            val = 0;
        }
        break;
    case MSR_MCG_STATUS:
        val = env->mcg_status;
        break;
    case MSR_IA32_MISC_ENABLE:
        val = env->msr_ia32_misc_enable;
        break;
    case MSR_IA32_BNDCFGS:
        val = env->msr_bndcfgs;
        break;
     case MSR_IA32_UCODE_REV:
        val = x86_cpu->ucode_rev;
        break;
    case MSR_CORE_THREAD_COUNT: {
        CPUState *cs = CPU(x86_cpu);
        val = (cs->nr_threads * cs->nr_cores) | (cs->nr_cores << 16);
        break;
    }
    case MSR_APIC_START ... MSR_APIC_END: {
        int ret;
        int index = (uint32_t)env->regs[R_ECX] - MSR_APIC_START;

        bql_lock();
        ret = apic_msr_read(index, &val);
        bql_unlock();
        if (ret < 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }

        break;
    }
    default:
        if ((uint32_t)env->regs[R_ECX] >= MSR_MC0_CTL
            && (uint32_t)env->regs[R_ECX] < MSR_MC0_CTL +
            (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)env->regs[R_ECX] - MSR_MC0_CTL;
            val = env->mce_banks[offset];
            break;
        }
        /* XXX: exception? */
        val = 0;
        break;
    }
    env->regs[R_EAX] = (uint32_t)(val);
    env->regs[R_EDX] = (uint32_t)(val >> 32);
}

void helper_flush_page(CPUX86State *env, target_ulong addr)
{
    tlb_flush_page(env_cpu(env), addr);
}

static G_NORETURN
void do_hlt(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    env->hflags &= ~HF_INHIBIT_IRQ_MASK; /* needed if sti is just before */
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

G_NORETURN void helper_hlt(CPUX86State *env, int next_eip_addend)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_HLT, 0, GETPC());
    env->eip += next_eip_addend;

    do_hlt(env);
}

void helper_monitor(CPUX86State *env, target_ulong ptr)
{
    if ((uint32_t)env->regs[R_ECX] != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    /* XXX: store address? */
    cpu_svm_check_intercept_param(env, SVM_EXIT_MONITOR, 0, GETPC());
}

G_NORETURN void helper_mwait(CPUX86State *env, int next_eip_addend)
{
    CPUState *cs = env_cpu(env);

    if ((uint32_t)env->regs[R_ECX] != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_MWAIT, 0, GETPC());
    env->eip += next_eip_addend;

    /* XXX: not complete but not completely erroneous */
    if (cs->cpu_index != 0 || CPU_NEXT(cs) != NULL) {
        do_pause(env);
    } else {
        do_hlt(env);
    }
}
