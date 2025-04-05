/*
 * QEMU HPPA CPU
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/qemu-print.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "qemu/module.h"
#include "exec/exec-all.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "fpu/softfloat.h"
#include "tcg/tcg.h"
#include "hw/hppa/hppa_hardware.h"

static void hppa_cpu_set_pc(CPUState *cs, vaddr value)
{
    HPPACPU *cpu = HPPA_CPU(cs);

#ifdef CONFIG_USER_ONLY
    value |= PRIV_USER;
#endif
    cpu->env.iaoq_f = value;
    cpu->env.iaoq_b = value + 4;
}

static vaddr hppa_cpu_get_pc(CPUState *cs)
{
    CPUHPPAState *env = cpu_env(cs);

    return hppa_form_gva_mask(env->gva_offset_mask,
                         (env->psw & PSW_C ? env->iasq_f : 0),
                         env->iaoq_f & -4);
}

void cpu_get_tb_cpu_state(CPUHPPAState *env, vaddr *pc,
                          uint64_t *pcsbase, uint32_t *pflags)
{
    uint32_t flags = 0;
    uint64_t cs_base = 0;

    /*
     * TB lookup assumes that PC contains the complete virtual address.
     * If we leave space+offset separate, we'll get ITLB misses to an
     * incomplete virtual address.  This also means that we must separate
     * out current cpu privilege from the low bits of IAOQ_F.
     */
    *pc = hppa_cpu_get_pc(env_cpu(env));
    flags |= (env->iaoq_f & 3) << TB_FLAG_PRIV_SHIFT;

    /*
     * The only really interesting case is if IAQ_Back is on the same page
     * as IAQ_Front, so that we can use goto_tb between the blocks.  In all
     * other cases, we'll be ending the TranslationBlock with one insn and
     * not linking between them.
     */
    if (env->iasq_f != env->iasq_b) {
        cs_base |= CS_BASE_DIFFSPACE;
    } else if ((env->iaoq_f ^ env->iaoq_b) & TARGET_PAGE_MASK) {
        cs_base |= CS_BASE_DIFFPAGE;
    } else {
        cs_base |= env->iaoq_b & ~TARGET_PAGE_MASK;
    }

    /* ??? E, T, H, L bits need to be here, when implemented.  */
    flags |= env->psw_n * PSW_N;
    flags |= env->psw_xb;
    flags |= env->psw & (PSW_W | PSW_C | PSW_D | PSW_P);

#ifdef CONFIG_USER_ONLY
    flags |= TB_FLAG_UNALIGN * !env_cpu(env)->prctl_unalign_sigbus;
#else
    if ((env->sr[4] == env->sr[5])
        & (env->sr[4] == env->sr[6])
        & (env->sr[4] == env->sr[7])) {
        flags |= TB_FLAG_SR_SAME;
    }
    if ((env->psw & PSW_W) &&
        (env->dr[2] & HPPA64_DIAG_SPHASH_ENABLE)) {
        flags |= TB_FLAG_SPHASH;
    }
#endif

    *pcsbase = cs_base;
    *pflags = flags;
}

static void hppa_cpu_synchronize_from_tb(CPUState *cs,
                                         const TranslationBlock *tb)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    /* IAQ is always up-to-date before goto_tb. */
    cpu->env.psw_n = (tb->flags & PSW_N) != 0;
    cpu->env.psw_xb = tb->flags & (PSW_X | PSW_B);
}

static void hppa_restore_state_to_opc(CPUState *cs,
                                      const TranslationBlock *tb,
                                      const uint64_t *data)
{
    CPUHPPAState *env = cpu_env(cs);

    env->iaoq_f = (env->iaoq_f & TARGET_PAGE_MASK) | data[0];
    if (data[1] != INT32_MIN) {
        env->iaoq_b = env->iaoq_f + data[1];
    }
    env->unwind_breg = data[2];
    /*
     * Since we were executing the instruction at IAOQ_F, and took some
     * sort of action that provoked the cpu_restore_state, we can infer
     * that the instruction was not nullified.
     */
    env->psw_n = 0;
}

#ifndef CONFIG_USER_ONLY
static bool hppa_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI);
}
#endif /* !CONFIG_USER_ONLY */

static int hppa_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    CPUHPPAState *env = cpu_env(cs);

    if (env->psw & (ifetch ? PSW_C : PSW_D)) {
        return PRIV_P_TO_MMU_IDX(env->iaoq_f & 3, env->psw & PSW_P);
    }
    /* mmu disabled */
    return env->psw & PSW_W ? MMU_ABS_W_IDX : MMU_ABS_IDX;
}

static void hppa_cpu_disas_set_info(CPUState *cs, disassemble_info *info)
{
    info->mach = bfd_mach_hppa20;
    info->endian = BFD_ENDIAN_BIG;
    info->print_insn = print_insn_hppa;
}

#ifndef CONFIG_USER_ONLY
static G_NORETURN
void hppa_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                  MMUAccessType access_type, int mmu_idx,
                                  uintptr_t retaddr)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;

    cs->exception_index = EXCP_UNALIGN;
    cpu_restore_state(cs, retaddr);
    hppa_set_ior_and_isr(env, addr, MMU_IDX_MMU_DISABLED(mmu_idx));

    cpu_loop_exit(cs);
}
#endif /* CONFIG_USER_ONLY */

static void hppa_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    HPPACPUClass *acc = HPPA_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    acc->parent_realize(dev, errp);

#ifndef CONFIG_USER_ONLY
    {
        HPPACPU *cpu = HPPA_CPU(cs);

        cpu->alarm_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        hppa_cpu_alarm_timer, cpu);
        hppa_ptlbe(&cpu->env);
    }
#endif

    /* Use pc-relative instructions always to simplify the translator. */
    tcg_cflags_set(cs, CF_PCREL);
}

static void hppa_cpu_initfn(Object *obj)
{
    CPUHPPAState *env = cpu_env(CPU(obj));

    env->is_pa20 = !!object_dynamic_cast(obj, TYPE_HPPA64_CPU);
}

static void hppa_cpu_reset_hold(Object *obj, ResetType type)
{
    HPPACPUClass *scc = HPPA_CPU_GET_CLASS(obj);
    CPUState *cs = CPU(obj);
    HPPACPU *cpu = HPPA_CPU(obj);
    CPUHPPAState *env = &cpu->env;

    if (scc->parent_phases.hold) {
        scc->parent_phases.hold(obj, type);
    }
    cs->exception_index = -1;
    cs->halted = 0;
    cpu_set_pc(cs, 0xf0000004);

    memset(env, 0, offsetof(CPUHPPAState, end_reset_fields));

    cpu_hppa_loaded_fr0(env);

    /* 64-bit machines start with space-register hashing enabled in %dr2 */
    env->dr[2] = hppa_is_pa20(env) ? HPPA64_DIAG_SPHASH_ENABLE : 0;

    cpu_hppa_put_psw(env, PSW_M);
}

static ObjectClass *hppa_cpu_class_by_name(const char *cpu_model)
{
    g_autofree char *typename = g_strconcat(cpu_model, "-cpu", NULL);

    return object_class_by_name(typename);
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps hppa_sysemu_ops = {
    .has_work = hppa_cpu_has_work,
    .get_phys_page_debug = hppa_cpu_get_phys_page_debug,
};
#endif

#include "accel/tcg/cpu-ops.h"

static const TCGCPUOps hppa_tcg_ops = {
    /* PA-RISC 1.x processors have a strong memory model.  */
    /*
     * ??? While we do not yet implement PA-RISC 2.0, those processors have
     * a weak memory model, but with TLB bits that force ordering on a per-page
     * basis.  It's probably easier to fall back to a strong memory model.
     */
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = true,

    .initialize = hppa_translate_init,
    .translate_code = hppa_translate_code,
    .synchronize_from_tb = hppa_cpu_synchronize_from_tb,
    .restore_state_to_opc = hppa_restore_state_to_opc,
    .mmu_index = hppa_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill_align = hppa_cpu_tlb_fill_align,
    .cpu_exec_interrupt = hppa_cpu_exec_interrupt,
    .cpu_exec_halt = hppa_cpu_has_work,
    .do_interrupt = hppa_cpu_do_interrupt,
    .do_unaligned_access = hppa_cpu_do_unaligned_access,
    .do_transaction_failed = hppa_cpu_do_transaction_failed,
#endif /* !CONFIG_USER_ONLY */
};

static void hppa_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    HPPACPUClass *acc = HPPA_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, hppa_cpu_realizefn,
                                    &acc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, hppa_cpu_reset_hold, NULL,
                                       &acc->parent_phases);

    cc->class_by_name = hppa_cpu_class_by_name;
    cc->dump_state = hppa_cpu_dump_state;
    cc->set_pc = hppa_cpu_set_pc;
    cc->get_pc = hppa_cpu_get_pc;
    cc->gdb_read_register = hppa_cpu_gdb_read_register;
    cc->gdb_write_register = hppa_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    dc->vmsd = &vmstate_hppa_cpu;
    cc->sysemu_ops = &hppa_sysemu_ops;
#endif
    cc->disas_set_info = hppa_cpu_disas_set_info;
    cc->gdb_num_core_regs = 128;
    cc->tcg_ops = &hppa_tcg_ops;
}

static const TypeInfo hppa_cpu_type_infos[] = {
    {
        .name = TYPE_HPPA_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(HPPACPU),
        .instance_align = __alignof(HPPACPU),
        .instance_init = hppa_cpu_initfn,
        .abstract = false,
        .class_size = sizeof(HPPACPUClass),
        .class_init = hppa_cpu_class_init,
    },
    {
        .name = TYPE_HPPA64_CPU,
        .parent = TYPE_HPPA_CPU,
    },
};

DEFINE_TYPES(hppa_cpu_type_infos)
