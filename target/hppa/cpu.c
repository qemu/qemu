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
#include "fpu/softfloat.h"
#include "tcg/tcg.h"

static void hppa_cpu_set_pc(CPUState *cs, vaddr value)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    cpu->env.iaoq_f = value;
    cpu->env.iaoq_b = value + 4;
}

static vaddr hppa_cpu_get_pc(CPUState *cs)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    return cpu->env.iaoq_f;
}

static void hppa_cpu_synchronize_from_tb(CPUState *cs,
                                         const TranslationBlock *tb)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    tcg_debug_assert(!(cs->tcg_cflags & CF_PCREL));

#ifdef CONFIG_USER_ONLY
    cpu->env.iaoq_f = tb->pc;
    cpu->env.iaoq_b = tb->cs_base;
#else
    /* Recover the IAOQ values from the GVA + PRIV.  */
    uint32_t priv = (tb->flags >> TB_FLAG_PRIV_SHIFT) & 3;
    target_ulong cs_base = tb->cs_base;
    target_ulong iasq_f = cs_base & ~0xffffffffull;
    int32_t diff = cs_base;

    cpu->env.iasq_f = iasq_f;
    cpu->env.iaoq_f = (tb->pc & ~iasq_f) + priv;
    if (diff) {
        cpu->env.iaoq_b = cpu->env.iaoq_f + diff;
    }
#endif

    cpu->env.psw_n = (tb->flags & PSW_N) != 0;
}

static void hppa_restore_state_to_opc(CPUState *cs,
                                      const TranslationBlock *tb,
                                      const uint64_t *data)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    cpu->env.iaoq_f = data[0];
    if (data[1] != (target_ulong)-1) {
        cpu->env.iaoq_b = data[1];
    }
    cpu->env.unwind_breg = data[2];
    /*
     * Since we were executing the instruction at IAOQ_F, and took some
     * sort of action that provoked the cpu_restore_state, we can infer
     * that the instruction was not nullified.
     */
    cpu->env.psw_n = 0;
}

static bool hppa_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI);
}

static void hppa_cpu_disas_set_info(CPUState *cs, disassemble_info *info)
{
    info->mach = bfd_mach_hppa20;
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
    if (env->psw & PSW_Q) {
        /* ??? Needs tweaking for hppa64.  */
        env->cr[CR_IOR] = addr;
        env->cr[CR_ISR] = addr >> 32;
    }

    cpu_loop_exit_restore(cs, retaddr);
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
}

static void hppa_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    HPPACPU *cpu = HPPA_CPU(obj);
    CPUHPPAState *env = &cpu->env;

    cs->exception_index = -1;
    cpu_hppa_loaded_fr0(env);
    cpu_hppa_put_psw(env, PSW_W);
}

static ObjectClass *hppa_cpu_class_by_name(const char *cpu_model)
{
    g_autofree char *typename = g_strconcat(cpu_model, "-cpu", NULL);
    ObjectClass *oc = object_class_by_name(typename);

    if (oc &&
        !object_class_is_abstract(oc) &&
        object_class_dynamic_cast(oc, TYPE_HPPA_CPU)) {
        return oc;
    }
    return NULL;
}

static void hppa_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUClass *cc = CPU_CLASS(oc);
    const char *tname = object_class_get_name(oc);
    g_autofree char *name = g_strndup(tname, strchr(tname, '-') - tname);

    if (cc->deprecation_note) {
        qemu_printf("  %s (deprecated)\n", name);
    } else {
        qemu_printf("  %s\n", name);
    }
}

void hppa_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list_sorted(TYPE_HPPA_CPU, false);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, hppa_cpu_list_entry, NULL);
    g_slist_free(list);
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps hppa_sysemu_ops = {
    .get_phys_page_debug = hppa_cpu_get_phys_page_debug,
};
#endif

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps hppa_tcg_ops = {
    .initialize = hppa_translate_init,
    .synchronize_from_tb = hppa_cpu_synchronize_from_tb,
    .restore_state_to_opc = hppa_restore_state_to_opc,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = hppa_cpu_tlb_fill,
    .cpu_exec_interrupt = hppa_cpu_exec_interrupt,
    .do_interrupt = hppa_cpu_do_interrupt,
    .do_unaligned_access = hppa_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};

static void hppa_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    HPPACPUClass *acc = HPPA_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, hppa_cpu_realizefn,
                                    &acc->parent_realize);

    cc->class_by_name = hppa_cpu_class_by_name;
    cc->has_work = hppa_cpu_has_work;
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
