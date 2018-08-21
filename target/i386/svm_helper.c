/*
 *  x86 SVM helpers
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
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

/* Secure Virtual Machine helpers */

#if defined(CONFIG_USER_ONLY)

void helper_vmrun(CPUX86State *env, int aflag, int next_eip_addend)
{
}

void helper_vmmcall(CPUX86State *env)
{
}

void helper_vmload(CPUX86State *env, int aflag)
{
}

void helper_vmsave(CPUX86State *env, int aflag)
{
}

void helper_stgi(CPUX86State *env)
{
}

void helper_clgi(CPUX86State *env)
{
}

void helper_skinit(CPUX86State *env)
{
}

void helper_invlpga(CPUX86State *env, int aflag)
{
}

void cpu_vmexit(CPUX86State *nenv, uint32_t exit_code, uint64_t exit_info_1,
                uintptr_t retaddr)
{
    assert(0);
}

void helper_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                      uint64_t param)
{
}

void cpu_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                   uint64_t param, uintptr_t retaddr)
{
}

void helper_svm_check_io(CPUX86State *env, uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
}
#else

static inline void svm_save_seg(CPUX86State *env, hwaddr addr,
                                const SegmentCache *sc)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    x86_stw_phys(cs, addr + offsetof(struct vmcb_seg, selector),
             sc->selector);
    x86_stq_phys(cs, addr + offsetof(struct vmcb_seg, base),
             sc->base);
    x86_stl_phys(cs, addr + offsetof(struct vmcb_seg, limit),
             sc->limit);
    x86_stw_phys(cs, addr + offsetof(struct vmcb_seg, attrib),
             ((sc->flags >> 8) & 0xff) | ((sc->flags >> 12) & 0x0f00));
}

static inline void svm_load_seg(CPUX86State *env, hwaddr addr,
                                SegmentCache *sc)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    unsigned int flags;

    sc->selector = x86_lduw_phys(cs,
                             addr + offsetof(struct vmcb_seg, selector));
    sc->base = x86_ldq_phys(cs, addr + offsetof(struct vmcb_seg, base));
    sc->limit = x86_ldl_phys(cs, addr + offsetof(struct vmcb_seg, limit));
    flags = x86_lduw_phys(cs, addr + offsetof(struct vmcb_seg, attrib));
    sc->flags = ((flags & 0xff) << 8) | ((flags & 0x0f00) << 12);
}

static inline void svm_load_seg_cache(CPUX86State *env, hwaddr addr,
                                      int seg_reg)
{
    SegmentCache sc1, *sc = &sc1;

    svm_load_seg(env, addr, sc);
    cpu_x86_load_seg_cache(env, seg_reg, sc->selector,
                           sc->base, sc->limit, sc->flags);
}

void helper_vmrun(CPUX86State *env, int aflag, int next_eip_addend)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    target_ulong addr;
    uint64_t nested_ctl;
    uint32_t event_inj;
    uint32_t int_ctl;

    cpu_svm_check_intercept_param(env, SVM_EXIT_VMRUN, 0, GETPC());

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmrun! " TARGET_FMT_lx "\n", addr);

    env->vm_vmcb = addr;

    /* save the current CPU state in the hsave page */
    x86_stq_phys(cs, env->vm_hsave + offsetof(struct vmcb, save.gdtr.base),
             env->gdt.base);
    x86_stl_phys(cs, env->vm_hsave + offsetof(struct vmcb, save.gdtr.limit),
             env->gdt.limit);

    x86_stq_phys(cs, env->vm_hsave + offsetof(struct vmcb, save.idtr.base),
             env->idt.base);
    x86_stl_phys(cs, env->vm_hsave + offsetof(struct vmcb, save.idtr.limit),
             env->idt.limit);

    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.cr0), env->cr[0]);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.cr2), env->cr[2]);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.cr3), env->cr[3]);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.cr4), env->cr[4]);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.dr6), env->dr[6]);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.dr7), env->dr[7]);

    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.efer), env->efer);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.rflags),
             cpu_compute_eflags(env));

    svm_save_seg(env, env->vm_hsave + offsetof(struct vmcb, save.es),
                 &env->segs[R_ES]);
    svm_save_seg(env, env->vm_hsave + offsetof(struct vmcb, save.cs),
                 &env->segs[R_CS]);
    svm_save_seg(env, env->vm_hsave + offsetof(struct vmcb, save.ss),
                 &env->segs[R_SS]);
    svm_save_seg(env, env->vm_hsave + offsetof(struct vmcb, save.ds),
                 &env->segs[R_DS]);

    x86_stq_phys(cs, env->vm_hsave + offsetof(struct vmcb, save.rip),
             env->eip + next_eip_addend);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.rsp), env->regs[R_ESP]);
    x86_stq_phys(cs,
             env->vm_hsave + offsetof(struct vmcb, save.rax), env->regs[R_EAX]);

    /* load the interception bitmaps so we do not need to access the
       vmcb in svm mode */
    env->intercept = x86_ldq_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                      control.intercept));
    env->intercept_cr_read = x86_lduw_phys(cs, env->vm_vmcb +
                                       offsetof(struct vmcb,
                                                control.intercept_cr_read));
    env->intercept_cr_write = x86_lduw_phys(cs, env->vm_vmcb +
                                        offsetof(struct vmcb,
                                                 control.intercept_cr_write));
    env->intercept_dr_read = x86_lduw_phys(cs, env->vm_vmcb +
                                       offsetof(struct vmcb,
                                                control.intercept_dr_read));
    env->intercept_dr_write = x86_lduw_phys(cs, env->vm_vmcb +
                                        offsetof(struct vmcb,
                                                 control.intercept_dr_write));
    env->intercept_exceptions = x86_ldl_phys(cs, env->vm_vmcb +
                                         offsetof(struct vmcb,
                                                  control.intercept_exceptions
                                                  ));

    nested_ctl = x86_ldq_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                          control.nested_ctl));
    if (nested_ctl & SVM_NPT_ENABLED) {
        env->nested_cr3 = x86_ldq_phys(cs,
                                env->vm_vmcb + offsetof(struct vmcb,
                                                        control.nested_cr3));
        env->hflags2 |= HF2_NPT_MASK;

        env->nested_pg_mode = 0;
        if (env->cr[4] & CR4_PAE_MASK) {
            env->nested_pg_mode |= SVM_NPT_PAE;
        }
        if (env->hflags & HF_LMA_MASK) {
            env->nested_pg_mode |= SVM_NPT_LMA;
        }
        if (env->efer & MSR_EFER_NXE) {
            env->nested_pg_mode |= SVM_NPT_NXE;
        }
    }

    /* enable intercepts */
    env->hflags |= HF_GUEST_MASK;

    env->tsc_offset = x86_ldq_phys(cs, env->vm_vmcb +
                               offsetof(struct vmcb, control.tsc_offset));

    env->gdt.base  = x86_ldq_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.gdtr.base));
    env->gdt.limit = x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.gdtr.limit));

    env->idt.base  = x86_ldq_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.idtr.base));
    env->idt.limit = x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.idtr.limit));

    /* clear exit_info_2 so we behave like the real hardware */
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), 0);

    cpu_x86_update_cr0(env, x86_ldq_phys(cs,
                                     env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr0)));
    cpu_x86_update_cr4(env, x86_ldq_phys(cs,
                                     env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr4)));
    cpu_x86_update_cr3(env, x86_ldq_phys(cs,
                                     env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr3)));
    env->cr[2] = x86_ldq_phys(cs,
                          env->vm_vmcb + offsetof(struct vmcb, save.cr2));
    int_ctl = x86_ldl_phys(cs,
                       env->vm_vmcb + offsetof(struct vmcb, control.int_ctl));
    env->hflags2 &= ~(HF2_HIF_MASK | HF2_VINTR_MASK);
    if (int_ctl & V_INTR_MASKING_MASK) {
        env->v_tpr = int_ctl & V_TPR_MASK;
        env->hflags2 |= HF2_VINTR_MASK;
        if (env->eflags & IF_MASK) {
            env->hflags2 |= HF2_HIF_MASK;
        }
    }

    cpu_load_efer(env,
                  x86_ldq_phys(cs,
                           env->vm_vmcb + offsetof(struct vmcb, save.efer)));
    env->eflags = 0;
    cpu_load_eflags(env, x86_ldq_phys(cs,
                                  env->vm_vmcb + offsetof(struct vmcb,
                                                          save.rflags)),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));

    svm_load_seg_cache(env, env->vm_vmcb + offsetof(struct vmcb, save.es),
                       R_ES);
    svm_load_seg_cache(env, env->vm_vmcb + offsetof(struct vmcb, save.cs),
                       R_CS);
    svm_load_seg_cache(env, env->vm_vmcb + offsetof(struct vmcb, save.ss),
                       R_SS);
    svm_load_seg_cache(env, env->vm_vmcb + offsetof(struct vmcb, save.ds),
                       R_DS);

    env->eip = x86_ldq_phys(cs,
                        env->vm_vmcb + offsetof(struct vmcb, save.rip));

    env->regs[R_ESP] = x86_ldq_phys(cs,
                                env->vm_vmcb + offsetof(struct vmcb, save.rsp));
    env->regs[R_EAX] = x86_ldq_phys(cs,
                                env->vm_vmcb + offsetof(struct vmcb, save.rax));
    env->dr[7] = x86_ldq_phys(cs,
                          env->vm_vmcb + offsetof(struct vmcb, save.dr7));
    env->dr[6] = x86_ldq_phys(cs,
                          env->vm_vmcb + offsetof(struct vmcb, save.dr6));

    /* FIXME: guest state consistency checks */

    switch (x86_ldub_phys(cs,
                      env->vm_vmcb + offsetof(struct vmcb, control.tlb_ctl))) {
    case TLB_CONTROL_DO_NOTHING:
        break;
    case TLB_CONTROL_FLUSH_ALL_ASID:
        /* FIXME: this is not 100% correct but should work for now */
        tlb_flush(cs);
        break;
    }

    env->hflags2 |= HF2_GIF_MASK;

    if (int_ctl & V_IRQ_MASK) {
        CPUState *cs = CPU(x86_env_get_cpu(env));

        cs->interrupt_request |= CPU_INTERRUPT_VIRQ;
    }

    /* maybe we need to inject an event */
    event_inj = x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                 control.event_inj));
    if (event_inj & SVM_EVTINJ_VALID) {
        uint8_t vector = event_inj & SVM_EVTINJ_VEC_MASK;
        uint16_t valid_err = event_inj & SVM_EVTINJ_VALID_ERR;
        uint32_t event_inj_err = x86_ldl_phys(cs, env->vm_vmcb +
                                          offsetof(struct vmcb,
                                                   control.event_inj_err));

        qemu_log_mask(CPU_LOG_TB_IN_ASM, "Injecting(%#hx): ", valid_err);
        /* FIXME: need to implement valid_err */
        switch (event_inj & SVM_EVTINJ_TYPE_MASK) {
        case SVM_EVTINJ_TYPE_INTR:
            cs->exception_index = vector;
            env->error_code = event_inj_err;
            env->exception_is_int = 0;
            env->exception_next_eip = -1;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "INTR");
            /* XXX: is it always correct? */
            do_interrupt_x86_hardirq(env, vector, 1);
            break;
        case SVM_EVTINJ_TYPE_NMI:
            cs->exception_index = EXCP02_NMI;
            env->error_code = event_inj_err;
            env->exception_is_int = 0;
            env->exception_next_eip = env->eip;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "NMI");
            cpu_loop_exit(cs);
            break;
        case SVM_EVTINJ_TYPE_EXEPT:
            cs->exception_index = vector;
            env->error_code = event_inj_err;
            env->exception_is_int = 0;
            env->exception_next_eip = -1;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "EXEPT");
            cpu_loop_exit(cs);
            break;
        case SVM_EVTINJ_TYPE_SOFT:
            cs->exception_index = vector;
            env->error_code = event_inj_err;
            env->exception_is_int = 1;
            env->exception_next_eip = env->eip;
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "SOFT");
            cpu_loop_exit(cs);
            break;
        }
        qemu_log_mask(CPU_LOG_TB_IN_ASM, " %#x %#x\n", cs->exception_index,
                      env->error_code);
    }
}

void helper_vmmcall(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_VMMCALL, 0, GETPC());
    raise_exception(env, EXCP06_ILLOP);
}

void helper_vmload(CPUX86State *env, int aflag)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    target_ulong addr;

    cpu_svm_check_intercept_param(env, SVM_EXIT_VMLOAD, 0, GETPC());

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmload! " TARGET_FMT_lx
                  "\nFS: %016" PRIx64 " | " TARGET_FMT_lx "\n",
                  addr, x86_ldq_phys(cs, addr + offsetof(struct vmcb,
                                                          save.fs.base)),
                  env->segs[R_FS].base);

    svm_load_seg_cache(env, addr + offsetof(struct vmcb, save.fs), R_FS);
    svm_load_seg_cache(env, addr + offsetof(struct vmcb, save.gs), R_GS);
    svm_load_seg(env, addr + offsetof(struct vmcb, save.tr), &env->tr);
    svm_load_seg(env, addr + offsetof(struct vmcb, save.ldtr), &env->ldt);

#ifdef TARGET_X86_64
    env->kernelgsbase = x86_ldq_phys(cs, addr + offsetof(struct vmcb,
                                                 save.kernel_gs_base));
    env->lstar = x86_ldq_phys(cs, addr + offsetof(struct vmcb, save.lstar));
    env->cstar = x86_ldq_phys(cs, addr + offsetof(struct vmcb, save.cstar));
    env->fmask = x86_ldq_phys(cs, addr + offsetof(struct vmcb, save.sfmask));
#endif
    env->star = x86_ldq_phys(cs, addr + offsetof(struct vmcb, save.star));
    env->sysenter_cs = x86_ldq_phys(cs,
                                addr + offsetof(struct vmcb, save.sysenter_cs));
    env->sysenter_esp = x86_ldq_phys(cs, addr + offsetof(struct vmcb,
                                                 save.sysenter_esp));
    env->sysenter_eip = x86_ldq_phys(cs, addr + offsetof(struct vmcb,
                                                 save.sysenter_eip));
}

void helper_vmsave(CPUX86State *env, int aflag)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    target_ulong addr;

    cpu_svm_check_intercept_param(env, SVM_EXIT_VMSAVE, 0, GETPC());

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmsave! " TARGET_FMT_lx
                  "\nFS: %016" PRIx64 " | " TARGET_FMT_lx "\n",
                  addr, x86_ldq_phys(cs,
                                 addr + offsetof(struct vmcb, save.fs.base)),
                  env->segs[R_FS].base);

    svm_save_seg(env, addr + offsetof(struct vmcb, save.fs),
                 &env->segs[R_FS]);
    svm_save_seg(env, addr + offsetof(struct vmcb, save.gs),
                 &env->segs[R_GS]);
    svm_save_seg(env, addr + offsetof(struct vmcb, save.tr),
                 &env->tr);
    svm_save_seg(env, addr + offsetof(struct vmcb, save.ldtr),
                 &env->ldt);

#ifdef TARGET_X86_64
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.kernel_gs_base),
             env->kernelgsbase);
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.lstar), env->lstar);
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.cstar), env->cstar);
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.sfmask), env->fmask);
#endif
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.star), env->star);
    x86_stq_phys(cs,
             addr + offsetof(struct vmcb, save.sysenter_cs), env->sysenter_cs);
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.sysenter_esp),
             env->sysenter_esp);
    x86_stq_phys(cs, addr + offsetof(struct vmcb, save.sysenter_eip),
             env->sysenter_eip);
}

void helper_stgi(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_STGI, 0, GETPC());
    env->hflags2 |= HF2_GIF_MASK;
}

void helper_clgi(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_CLGI, 0, GETPC());
    env->hflags2 &= ~HF2_GIF_MASK;
}

void helper_skinit(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_SKINIT, 0, GETPC());
    /* XXX: not implemented */
    raise_exception(env, EXCP06_ILLOP);
}

void helper_invlpga(CPUX86State *env, int aflag)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    target_ulong addr;

    cpu_svm_check_intercept_param(env, SVM_EXIT_INVLPGA, 0, GETPC());

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    /* XXX: could use the ASID to see if it is needed to do the
       flush */
    tlb_flush_page(CPU(cpu), addr);
}

void cpu_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                   uint64_t param, uintptr_t retaddr)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    if (likely(!(env->hflags & HF_GUEST_MASK))) {
        return;
    }
    switch (type) {
    case SVM_EXIT_READ_CR0 ... SVM_EXIT_READ_CR0 + 8:
        if (env->intercept_cr_read & (1 << (type - SVM_EXIT_READ_CR0))) {
            cpu_vmexit(env, type, param, retaddr);
        }
        break;
    case SVM_EXIT_WRITE_CR0 ... SVM_EXIT_WRITE_CR0 + 8:
        if (env->intercept_cr_write & (1 << (type - SVM_EXIT_WRITE_CR0))) {
            cpu_vmexit(env, type, param, retaddr);
        }
        break;
    case SVM_EXIT_READ_DR0 ... SVM_EXIT_READ_DR0 + 7:
        if (env->intercept_dr_read & (1 << (type - SVM_EXIT_READ_DR0))) {
            cpu_vmexit(env, type, param, retaddr);
        }
        break;
    case SVM_EXIT_WRITE_DR0 ... SVM_EXIT_WRITE_DR0 + 7:
        if (env->intercept_dr_write & (1 << (type - SVM_EXIT_WRITE_DR0))) {
            cpu_vmexit(env, type, param, retaddr);
        }
        break;
    case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 31:
        if (env->intercept_exceptions & (1 << (type - SVM_EXIT_EXCP_BASE))) {
            cpu_vmexit(env, type, param, retaddr);
        }
        break;
    case SVM_EXIT_MSR:
        if (env->intercept & (1ULL << (SVM_EXIT_MSR - SVM_EXIT_INTR))) {
            /* FIXME: this should be read in at vmrun (faster this way?) */
            uint64_t addr = x86_ldq_phys(cs, env->vm_vmcb +
                                     offsetof(struct vmcb,
                                              control.msrpm_base_pa));
            uint32_t t0, t1;

            switch ((uint32_t)env->regs[R_ECX]) {
            case 0 ... 0x1fff:
                t0 = (env->regs[R_ECX] * 2) % 8;
                t1 = (env->regs[R_ECX] * 2) / 8;
                break;
            case 0xc0000000 ... 0xc0001fff:
                t0 = (8192 + env->regs[R_ECX] - 0xc0000000) * 2;
                t1 = (t0 / 8);
                t0 %= 8;
                break;
            case 0xc0010000 ... 0xc0011fff:
                t0 = (16384 + env->regs[R_ECX] - 0xc0010000) * 2;
                t1 = (t0 / 8);
                t0 %= 8;
                break;
            default:
                cpu_vmexit(env, type, param, retaddr);
                t0 = 0;
                t1 = 0;
                break;
            }
            if (x86_ldub_phys(cs, addr + t1) & ((1 << param) << t0)) {
                cpu_vmexit(env, type, param, retaddr);
            }
        }
        break;
    default:
        if (env->intercept & (1ULL << (type - SVM_EXIT_INTR))) {
            cpu_vmexit(env, type, param, retaddr);
        }
        break;
    }
}

void helper_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                      uint64_t param)
{
    cpu_svm_check_intercept_param(env, type, param, GETPC());
}

void helper_svm_check_io(CPUX86State *env, uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    if (env->intercept & (1ULL << (SVM_EXIT_IOIO - SVM_EXIT_INTR))) {
        /* FIXME: this should be read in at vmrun (faster this way?) */
        uint64_t addr = x86_ldq_phys(cs, env->vm_vmcb +
                                 offsetof(struct vmcb, control.iopm_base_pa));
        uint16_t mask = (1 << ((param >> 4) & 7)) - 1;

        if (x86_lduw_phys(cs, addr + port / 8) & (mask << (port & 7))) {
            /* next env->eip */
            x86_stq_phys(cs,
                     env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                     env->eip + next_eip_addend);
            cpu_vmexit(env, SVM_EXIT_IOIO, param | (port << 16), GETPC());
        }
    }
}

void cpu_vmexit(CPUX86State *env, uint32_t exit_code, uint64_t exit_info_1,
                uintptr_t retaddr)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    cpu_restore_state(cs, retaddr, true);

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmexit(%08x, %016" PRIx64 ", %016"
                  PRIx64 ", " TARGET_FMT_lx ")!\n",
                  exit_code, exit_info_1,
                  x86_ldq_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                   control.exit_info_2)),
                  env->eip);

    cs->exception_index = EXCP_VMEXIT + exit_code;
    env->error_code = exit_info_1;

    /* remove any pending exception */
    env->old_exception = -1;
    cpu_loop_exit(cs);
}

void do_vmexit(CPUX86State *env, uint32_t exit_code, uint64_t exit_info_1)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    uint32_t int_ctl;

    if (env->hflags & HF_INHIBIT_IRQ_MASK) {
        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.int_state),
                 SVM_INTERRUPT_SHADOW_MASK);
        env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    } else {
        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.int_state), 0);
    }
    env->hflags2 &= ~HF2_NPT_MASK;

    /* Save the VM state in the vmcb */
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.es),
                 &env->segs[R_ES]);
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.cs),
                 &env->segs[R_CS]);
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.ss),
                 &env->segs[R_SS]);
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.ds),
                 &env->segs[R_DS]);

    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.gdtr.base),
             env->gdt.base);
    x86_stl_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.gdtr.limit),
             env->gdt.limit);

    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.idtr.base),
             env->idt.base);
    x86_stl_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.idtr.limit),
             env->idt.limit);

    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.efer), env->efer);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.cr0), env->cr[0]);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.cr2), env->cr[2]);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.cr3), env->cr[3]);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.cr4), env->cr[4]);

    int_ctl = x86_ldl_phys(cs,
                       env->vm_vmcb + offsetof(struct vmcb, control.int_ctl));
    int_ctl &= ~(V_TPR_MASK | V_IRQ_MASK);
    int_ctl |= env->v_tpr & V_TPR_MASK;
    if (cs->interrupt_request & CPU_INTERRUPT_VIRQ) {
        int_ctl |= V_IRQ_MASK;
    }
    x86_stl_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, control.int_ctl), int_ctl);

    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.rflags),
             cpu_compute_eflags(env));
    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.rip),
             env->eip);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.rsp), env->regs[R_ESP]);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.rax), env->regs[R_EAX]);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.dr7), env->dr[7]);
    x86_stq_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, save.dr6), env->dr[6]);
    x86_stb_phys(cs, env->vm_vmcb + offsetof(struct vmcb, save.cpl),
             env->hflags & HF_CPL_MASK);

    /* Reload the host state from vm_hsave */
    env->hflags2 &= ~(HF2_HIF_MASK | HF2_VINTR_MASK);
    env->hflags &= ~HF_GUEST_MASK;
    env->intercept = 0;
    env->intercept_exceptions = 0;
    cs->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
    env->tsc_offset = 0;

    env->gdt.base  = x86_ldq_phys(cs, env->vm_hsave + offsetof(struct vmcb,
                                                       save.gdtr.base));
    env->gdt.limit = x86_ldl_phys(cs, env->vm_hsave + offsetof(struct vmcb,
                                                       save.gdtr.limit));

    env->idt.base  = x86_ldq_phys(cs, env->vm_hsave + offsetof(struct vmcb,
                                                       save.idtr.base));
    env->idt.limit = x86_ldl_phys(cs, env->vm_hsave + offsetof(struct vmcb,
                                                       save.idtr.limit));

    cpu_x86_update_cr0(env, x86_ldq_phys(cs,
                                     env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr0)) |
                       CR0_PE_MASK);
    cpu_x86_update_cr4(env, x86_ldq_phys(cs,
                                     env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr4)));
    cpu_x86_update_cr3(env, x86_ldq_phys(cs,
                                     env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr3)));
    /* we need to set the efer after the crs so the hidden flags get
       set properly */
    cpu_load_efer(env, x86_ldq_phys(cs, env->vm_hsave + offsetof(struct vmcb,
                                                         save.efer)));
    env->eflags = 0;
    cpu_load_eflags(env, x86_ldq_phys(cs,
                                  env->vm_hsave + offsetof(struct vmcb,
                                                           save.rflags)),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK |
                      VM_MASK));

    svm_load_seg_cache(env, env->vm_hsave + offsetof(struct vmcb, save.es),
                       R_ES);
    svm_load_seg_cache(env, env->vm_hsave + offsetof(struct vmcb, save.cs),
                       R_CS);
    svm_load_seg_cache(env, env->vm_hsave + offsetof(struct vmcb, save.ss),
                       R_SS);
    svm_load_seg_cache(env, env->vm_hsave + offsetof(struct vmcb, save.ds),
                       R_DS);

    env->eip = x86_ldq_phys(cs,
                        env->vm_hsave + offsetof(struct vmcb, save.rip));
    env->regs[R_ESP] = x86_ldq_phys(cs, env->vm_hsave +
                                offsetof(struct vmcb, save.rsp));
    env->regs[R_EAX] = x86_ldq_phys(cs, env->vm_hsave +
                                offsetof(struct vmcb, save.rax));

    env->dr[6] = x86_ldq_phys(cs,
                          env->vm_hsave + offsetof(struct vmcb, save.dr6));
    env->dr[7] = x86_ldq_phys(cs,
                          env->vm_hsave + offsetof(struct vmcb, save.dr7));

    /* other setups */
    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, control.exit_code),
             exit_code);
    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, control.exit_info_1),
             exit_info_1);

    x86_stl_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, control.exit_int_info),
             x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                              control.event_inj)));
    x86_stl_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, control.exit_int_info_err),
             x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                              control.event_inj_err)));
    x86_stl_phys(cs,
             env->vm_vmcb + offsetof(struct vmcb, control.event_inj), 0);

    env->hflags2 &= ~HF2_GIF_MASK;
    /* FIXME: Resets the current ASID register to zero (host ASID). */

    /* Clears the V_IRQ and V_INTR_MASKING bits inside the processor. */

    /* Clears the TSC_OFFSET inside the processor. */

    /* If the host is in PAE mode, the processor reloads the host's PDPEs
       from the page table indicated the host's CR3. If the PDPEs contain
       illegal state, the processor causes a shutdown. */

    /* Disables all breakpoints in the host DR7 register. */

    /* Checks the reloaded host state for consistency. */

    /* If the host's rIP reloaded by #VMEXIT is outside the limit of the
       host's code segment or non-canonical (in the case of long mode), a
       #GP fault is delivered inside the host. */
}

#endif
