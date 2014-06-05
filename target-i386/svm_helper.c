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

#include "cpu.h"
#include "exec/cpu-all.h"
#include "exec/helper-proto.h"
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

void helper_vmexit(CPUX86State *env, uint32_t exit_code, uint64_t exit_info_1)
{
}

void cpu_vmexit(CPUX86State *nenv, uint32_t exit_code, uint64_t exit_info_1)
{
}

void helper_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                      uint64_t param)
{
}

void cpu_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                   uint64_t param)
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

    stw_phys(cs->as, addr + offsetof(struct vmcb_seg, selector),
             sc->selector);
    stq_phys(cs->as, addr + offsetof(struct vmcb_seg, base),
             sc->base);
    stl_phys(cs->as, addr + offsetof(struct vmcb_seg, limit),
             sc->limit);
    stw_phys(cs->as, addr + offsetof(struct vmcb_seg, attrib),
             ((sc->flags >> 8) & 0xff) | ((sc->flags >> 12) & 0x0f00));
}

static inline void svm_load_seg(CPUX86State *env, hwaddr addr,
                                SegmentCache *sc)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    unsigned int flags;

    sc->selector = lduw_phys(cs->as,
                             addr + offsetof(struct vmcb_seg, selector));
    sc->base = ldq_phys(cs->as, addr + offsetof(struct vmcb_seg, base));
    sc->limit = ldl_phys(cs->as, addr + offsetof(struct vmcb_seg, limit));
    flags = lduw_phys(cs->as, addr + offsetof(struct vmcb_seg, attrib));
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
    uint32_t event_inj;
    uint32_t int_ctl;

    cpu_svm_check_intercept_param(env, SVM_EXIT_VMRUN, 0);

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmrun! " TARGET_FMT_lx "\n", addr);

    env->vm_vmcb = addr;

    /* save the current CPU state in the hsave page */
    stq_phys(cs->as, env->vm_hsave + offsetof(struct vmcb, save.gdtr.base),
             env->gdt.base);
    stl_phys(cs->as, env->vm_hsave + offsetof(struct vmcb, save.gdtr.limit),
             env->gdt.limit);

    stq_phys(cs->as, env->vm_hsave + offsetof(struct vmcb, save.idtr.base),
             env->idt.base);
    stl_phys(cs->as, env->vm_hsave + offsetof(struct vmcb, save.idtr.limit),
             env->idt.limit);

    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.cr0), env->cr[0]);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.cr2), env->cr[2]);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.cr3), env->cr[3]);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.cr4), env->cr[4]);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.dr6), env->dr[6]);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.dr7), env->dr[7]);

    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.efer), env->efer);
    stq_phys(cs->as,
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

    stq_phys(cs->as, env->vm_hsave + offsetof(struct vmcb, save.rip),
             env->eip + next_eip_addend);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.rsp), env->regs[R_ESP]);
    stq_phys(cs->as,
             env->vm_hsave + offsetof(struct vmcb, save.rax), env->regs[R_EAX]);

    /* load the interception bitmaps so we do not need to access the
       vmcb in svm mode */
    env->intercept = ldq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                      control.intercept));
    env->intercept_cr_read = lduw_phys(cs->as, env->vm_vmcb +
                                       offsetof(struct vmcb,
                                                control.intercept_cr_read));
    env->intercept_cr_write = lduw_phys(cs->as, env->vm_vmcb +
                                        offsetof(struct vmcb,
                                                 control.intercept_cr_write));
    env->intercept_dr_read = lduw_phys(cs->as, env->vm_vmcb +
                                       offsetof(struct vmcb,
                                                control.intercept_dr_read));
    env->intercept_dr_write = lduw_phys(cs->as, env->vm_vmcb +
                                        offsetof(struct vmcb,
                                                 control.intercept_dr_write));
    env->intercept_exceptions = ldl_phys(cs->as, env->vm_vmcb +
                                         offsetof(struct vmcb,
                                                  control.intercept_exceptions
                                                  ));

    /* enable intercepts */
    env->hflags |= HF_SVMI_MASK;

    env->tsc_offset = ldq_phys(cs->as, env->vm_vmcb +
                               offsetof(struct vmcb, control.tsc_offset));

    env->gdt.base  = ldq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.gdtr.base));
    env->gdt.limit = ldl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.gdtr.limit));

    env->idt.base  = ldq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.idtr.base));
    env->idt.limit = ldl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                      save.idtr.limit));

    /* clear exit_info_2 so we behave like the real hardware */
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), 0);

    cpu_x86_update_cr0(env, ldq_phys(cs->as,
                                     env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr0)));
    cpu_x86_update_cr4(env, ldq_phys(cs->as,
                                     env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr4)));
    cpu_x86_update_cr3(env, ldq_phys(cs->as,
                                     env->vm_vmcb + offsetof(struct vmcb,
                                                             save.cr3)));
    env->cr[2] = ldq_phys(cs->as,
                          env->vm_vmcb + offsetof(struct vmcb, save.cr2));
    int_ctl = ldl_phys(cs->as,
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
                  ldq_phys(cs->as,
                           env->vm_vmcb + offsetof(struct vmcb, save.efer)));
    env->eflags = 0;
    cpu_load_eflags(env, ldq_phys(cs->as,
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

    env->eip = ldq_phys(cs->as,
                        env->vm_vmcb + offsetof(struct vmcb, save.rip));

    env->regs[R_ESP] = ldq_phys(cs->as,
                                env->vm_vmcb + offsetof(struct vmcb, save.rsp));
    env->regs[R_EAX] = ldq_phys(cs->as,
                                env->vm_vmcb + offsetof(struct vmcb, save.rax));
    env->dr[7] = ldq_phys(cs->as,
                          env->vm_vmcb + offsetof(struct vmcb, save.dr7));
    env->dr[6] = ldq_phys(cs->as,
                          env->vm_vmcb + offsetof(struct vmcb, save.dr6));

    /* FIXME: guest state consistency checks */

    switch (ldub_phys(cs->as,
                      env->vm_vmcb + offsetof(struct vmcb, control.tlb_ctl))) {
    case TLB_CONTROL_DO_NOTHING:
        break;
    case TLB_CONTROL_FLUSH_ALL_ASID:
        /* FIXME: this is not 100% correct but should work for now */
        tlb_flush(cs, 1);
        break;
    }

    env->hflags2 |= HF2_GIF_MASK;

    if (int_ctl & V_IRQ_MASK) {
        CPUState *cs = CPU(x86_env_get_cpu(env));

        cs->interrupt_request |= CPU_INTERRUPT_VIRQ;
    }

    /* maybe we need to inject an event */
    event_inj = ldl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                 control.event_inj));
    if (event_inj & SVM_EVTINJ_VALID) {
        uint8_t vector = event_inj & SVM_EVTINJ_VEC_MASK;
        uint16_t valid_err = event_inj & SVM_EVTINJ_VALID_ERR;
        uint32_t event_inj_err = ldl_phys(cs->as, env->vm_vmcb +
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
    cpu_svm_check_intercept_param(env, SVM_EXIT_VMMCALL, 0);
    raise_exception(env, EXCP06_ILLOP);
}

void helper_vmload(CPUX86State *env, int aflag)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    target_ulong addr;

    cpu_svm_check_intercept_param(env, SVM_EXIT_VMLOAD, 0);

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmload! " TARGET_FMT_lx
                  "\nFS: %016" PRIx64 " | " TARGET_FMT_lx "\n",
                  addr, ldq_phys(cs->as, addr + offsetof(struct vmcb,
                                                          save.fs.base)),
                  env->segs[R_FS].base);

    svm_load_seg_cache(env, addr + offsetof(struct vmcb, save.fs), R_FS);
    svm_load_seg_cache(env, addr + offsetof(struct vmcb, save.gs), R_GS);
    svm_load_seg(env, addr + offsetof(struct vmcb, save.tr), &env->tr);
    svm_load_seg(env, addr + offsetof(struct vmcb, save.ldtr), &env->ldt);

#ifdef TARGET_X86_64
    env->kernelgsbase = ldq_phys(cs->as, addr + offsetof(struct vmcb,
                                                 save.kernel_gs_base));
    env->lstar = ldq_phys(cs->as, addr + offsetof(struct vmcb, save.lstar));
    env->cstar = ldq_phys(cs->as, addr + offsetof(struct vmcb, save.cstar));
    env->fmask = ldq_phys(cs->as, addr + offsetof(struct vmcb, save.sfmask));
#endif
    env->star = ldq_phys(cs->as, addr + offsetof(struct vmcb, save.star));
    env->sysenter_cs = ldq_phys(cs->as,
                                addr + offsetof(struct vmcb, save.sysenter_cs));
    env->sysenter_esp = ldq_phys(cs->as, addr + offsetof(struct vmcb,
                                                 save.sysenter_esp));
    env->sysenter_eip = ldq_phys(cs->as, addr + offsetof(struct vmcb,
                                                 save.sysenter_eip));
}

void helper_vmsave(CPUX86State *env, int aflag)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    target_ulong addr;

    cpu_svm_check_intercept_param(env, SVM_EXIT_VMSAVE, 0);

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmsave! " TARGET_FMT_lx
                  "\nFS: %016" PRIx64 " | " TARGET_FMT_lx "\n",
                  addr, ldq_phys(cs->as,
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
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.kernel_gs_base),
             env->kernelgsbase);
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.lstar), env->lstar);
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.cstar), env->cstar);
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.sfmask), env->fmask);
#endif
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.star), env->star);
    stq_phys(cs->as,
             addr + offsetof(struct vmcb, save.sysenter_cs), env->sysenter_cs);
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.sysenter_esp),
             env->sysenter_esp);
    stq_phys(cs->as, addr + offsetof(struct vmcb, save.sysenter_eip),
             env->sysenter_eip);
}

void helper_stgi(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_STGI, 0);
    env->hflags2 |= HF2_GIF_MASK;
}

void helper_clgi(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_CLGI, 0);
    env->hflags2 &= ~HF2_GIF_MASK;
}

void helper_skinit(CPUX86State *env)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_SKINIT, 0);
    /* XXX: not implemented */
    raise_exception(env, EXCP06_ILLOP);
}

void helper_invlpga(CPUX86State *env, int aflag)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    target_ulong addr;

    cpu_svm_check_intercept_param(env, SVM_EXIT_INVLPGA, 0);

    if (aflag == 2) {
        addr = env->regs[R_EAX];
    } else {
        addr = (uint32_t)env->regs[R_EAX];
    }

    /* XXX: could use the ASID to see if it is needed to do the
       flush */
    tlb_flush_page(CPU(cpu), addr);
}

void helper_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                      uint64_t param)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    if (likely(!(env->hflags & HF_SVMI_MASK))) {
        return;
    }
    switch (type) {
    case SVM_EXIT_READ_CR0 ... SVM_EXIT_READ_CR0 + 8:
        if (env->intercept_cr_read & (1 << (type - SVM_EXIT_READ_CR0))) {
            helper_vmexit(env, type, param);
        }
        break;
    case SVM_EXIT_WRITE_CR0 ... SVM_EXIT_WRITE_CR0 + 8:
        if (env->intercept_cr_write & (1 << (type - SVM_EXIT_WRITE_CR0))) {
            helper_vmexit(env, type, param);
        }
        break;
    case SVM_EXIT_READ_DR0 ... SVM_EXIT_READ_DR0 + 7:
        if (env->intercept_dr_read & (1 << (type - SVM_EXIT_READ_DR0))) {
            helper_vmexit(env, type, param);
        }
        break;
    case SVM_EXIT_WRITE_DR0 ... SVM_EXIT_WRITE_DR0 + 7:
        if (env->intercept_dr_write & (1 << (type - SVM_EXIT_WRITE_DR0))) {
            helper_vmexit(env, type, param);
        }
        break;
    case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 31:
        if (env->intercept_exceptions & (1 << (type - SVM_EXIT_EXCP_BASE))) {
            helper_vmexit(env, type, param);
        }
        break;
    case SVM_EXIT_MSR:
        if (env->intercept & (1ULL << (SVM_EXIT_MSR - SVM_EXIT_INTR))) {
            /* FIXME: this should be read in at vmrun (faster this way?) */
            uint64_t addr = ldq_phys(cs->as, env->vm_vmcb +
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
                helper_vmexit(env, type, param);
                t0 = 0;
                t1 = 0;
                break;
            }
            if (ldub_phys(cs->as, addr + t1) & ((1 << param) << t0)) {
                helper_vmexit(env, type, param);
            }
        }
        break;
    default:
        if (env->intercept & (1ULL << (type - SVM_EXIT_INTR))) {
            helper_vmexit(env, type, param);
        }
        break;
    }
}

void cpu_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                   uint64_t param)
{
    helper_svm_check_intercept_param(env, type, param);
}

void helper_svm_check_io(CPUX86State *env, uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));

    if (env->intercept & (1ULL << (SVM_EXIT_IOIO - SVM_EXIT_INTR))) {
        /* FIXME: this should be read in at vmrun (faster this way?) */
        uint64_t addr = ldq_phys(cs->as, env->vm_vmcb +
                                 offsetof(struct vmcb, control.iopm_base_pa));
        uint16_t mask = (1 << ((param >> 4) & 7)) - 1;

        if (lduw_phys(cs->as, addr + port / 8) & (mask << (port & 7))) {
            /* next env->eip */
            stq_phys(cs->as,
                     env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                     env->eip + next_eip_addend);
            helper_vmexit(env, SVM_EXIT_IOIO, param | (port << 16));
        }
    }
}

/* Note: currently only 32 bits of exit_code are used */
void helper_vmexit(CPUX86State *env, uint32_t exit_code, uint64_t exit_info_1)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    uint32_t int_ctl;

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmexit(%08x, %016" PRIx64 ", %016"
                  PRIx64 ", " TARGET_FMT_lx ")!\n",
                  exit_code, exit_info_1,
                  ldq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                                   control.exit_info_2)),
                  env->eip);

    if (env->hflags & HF_INHIBIT_IRQ_MASK) {
        stl_phys(cs->as,
                 env->vm_vmcb + offsetof(struct vmcb, control.int_state),
                 SVM_INTERRUPT_SHADOW_MASK);
        env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    } else {
        stl_phys(cs->as,
                 env->vm_vmcb + offsetof(struct vmcb, control.int_state), 0);
    }

    /* Save the VM state in the vmcb */
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.es),
                 &env->segs[R_ES]);
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.cs),
                 &env->segs[R_CS]);
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.ss),
                 &env->segs[R_SS]);
    svm_save_seg(env, env->vm_vmcb + offsetof(struct vmcb, save.ds),
                 &env->segs[R_DS]);

    stq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.gdtr.base),
             env->gdt.base);
    stl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.gdtr.limit),
             env->gdt.limit);

    stq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.idtr.base),
             env->idt.base);
    stl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.idtr.limit),
             env->idt.limit);

    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.efer), env->efer);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.cr0), env->cr[0]);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.cr2), env->cr[2]);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.cr3), env->cr[3]);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.cr4), env->cr[4]);

    int_ctl = ldl_phys(cs->as,
                       env->vm_vmcb + offsetof(struct vmcb, control.int_ctl));
    int_ctl &= ~(V_TPR_MASK | V_IRQ_MASK);
    int_ctl |= env->v_tpr & V_TPR_MASK;
    if (cs->interrupt_request & CPU_INTERRUPT_VIRQ) {
        int_ctl |= V_IRQ_MASK;
    }
    stl_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, control.int_ctl), int_ctl);

    stq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.rflags),
             cpu_compute_eflags(env));
    stq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.rip),
             env->eip);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.rsp), env->regs[R_ESP]);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.rax), env->regs[R_EAX]);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.dr7), env->dr[7]);
    stq_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, save.dr6), env->dr[6]);
    stb_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, save.cpl),
             env->hflags & HF_CPL_MASK);

    /* Reload the host state from vm_hsave */
    env->hflags2 &= ~(HF2_HIF_MASK | HF2_VINTR_MASK);
    env->hflags &= ~HF_SVMI_MASK;
    env->intercept = 0;
    env->intercept_exceptions = 0;
    cs->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
    env->tsc_offset = 0;

    env->gdt.base  = ldq_phys(cs->as, env->vm_hsave + offsetof(struct vmcb,
                                                       save.gdtr.base));
    env->gdt.limit = ldl_phys(cs->as, env->vm_hsave + offsetof(struct vmcb,
                                                       save.gdtr.limit));

    env->idt.base  = ldq_phys(cs->as, env->vm_hsave + offsetof(struct vmcb,
                                                       save.idtr.base));
    env->idt.limit = ldl_phys(cs->as, env->vm_hsave + offsetof(struct vmcb,
                                                       save.idtr.limit));

    cpu_x86_update_cr0(env, ldq_phys(cs->as,
                                     env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr0)) |
                       CR0_PE_MASK);
    cpu_x86_update_cr4(env, ldq_phys(cs->as,
                                     env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr4)));
    cpu_x86_update_cr3(env, ldq_phys(cs->as,
                                     env->vm_hsave + offsetof(struct vmcb,
                                                              save.cr3)));
    /* we need to set the efer after the crs so the hidden flags get
       set properly */
    cpu_load_efer(env, ldq_phys(cs->as, env->vm_hsave + offsetof(struct vmcb,
                                                         save.efer)));
    env->eflags = 0;
    cpu_load_eflags(env, ldq_phys(cs->as,
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

    env->eip = ldq_phys(cs->as,
                        env->vm_hsave + offsetof(struct vmcb, save.rip));
    env->regs[R_ESP] = ldq_phys(cs->as, env->vm_hsave +
                                offsetof(struct vmcb, save.rsp));
    env->regs[R_EAX] = ldq_phys(cs->as, env->vm_hsave +
                                offsetof(struct vmcb, save.rax));

    env->dr[6] = ldq_phys(cs->as,
                          env->vm_hsave + offsetof(struct vmcb, save.dr6));
    env->dr[7] = ldq_phys(cs->as,
                          env->vm_hsave + offsetof(struct vmcb, save.dr7));

    /* other setups */
    stq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, control.exit_code),
             exit_code);
    stq_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb, control.exit_info_1),
             exit_info_1);

    stl_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, control.exit_int_info),
             ldl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                              control.event_inj)));
    stl_phys(cs->as,
             env->vm_vmcb + offsetof(struct vmcb, control.exit_int_info_err),
             ldl_phys(cs->as, env->vm_vmcb + offsetof(struct vmcb,
                                              control.event_inj_err)));
    stl_phys(cs->as,
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

    /* remove any pending exception */
    cs->exception_index = -1;
    env->error_code = 0;
    env->old_exception = -1;

    cpu_loop_exit(cs);
}

void cpu_vmexit(CPUX86State *env, uint32_t exit_code, uint64_t exit_info_1)
{
    helper_vmexit(env, exit_code, exit_info_1);
}

#endif
