/*
 * QEMU Nios II CPU
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
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
#include "qemu/module.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/log.h"
#include "exec/gdbstub.h"
#include "hw/qdev-properties.h"

static void nios2_cpu_set_pc(CPUState *cs, vaddr value)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    env->regs[R_PC] = value;
}

static bool nios2_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI);
}

static void nios2_cpu_reset(DeviceState *dev)
{
    CPUState *cs = CPU(dev);
    Nios2CPU *cpu = NIOS2_CPU(cs);
    Nios2CPUClass *ncc = NIOS2_CPU_GET_CLASS(cpu);
    CPUNios2State *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", cs->cpu_index);
        log_cpu_state(cs, 0);
    }

    ncc->parent_reset(dev);

    memset(env->regs, 0, sizeof(uint32_t) * NUM_CORE_REGS);
    env->regs[R_PC] = cpu->reset_addr;

#if defined(CONFIG_USER_ONLY)
    /* Start in user mode with interrupts enabled. */
    env->regs[CR_STATUS] = CR_STATUS_U | CR_STATUS_PIE;
#else
    env->regs[CR_STATUS] = 0;
#endif
}

#ifndef CONFIG_USER_ONLY
static void nios2_cpu_set_irq(void *opaque, int irq, int level)
{
    Nios2CPU *cpu = opaque;
    CPUNios2State *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    env->regs[CR_IPENDING] = deposit32(env->regs[CR_IPENDING], irq, 1, !!level);

    env->irq_pending = env->regs[CR_IPENDING] & env->regs[CR_IENABLE];

    if (env->irq_pending && (env->regs[CR_STATUS] & CR_STATUS_PIE)) {
        env->irq_pending = 0;
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else if (!env->irq_pending) {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}
#endif

static void nios2_cpu_initfn(Object *obj)
{
    Nios2CPU *cpu = NIOS2_CPU(obj);

    cpu_set_cpustate_pointers(cpu);

#if !defined(CONFIG_USER_ONLY)
    mmu_init(&cpu->env);

    /*
     * These interrupt lines model the IIC (internal interrupt
     * controller). QEMU does not currently support the EIC
     * (external interrupt controller) -- if we did it would be
     * a separate device in hw/intc with a custom interface to
     * the CPU, and boards using it would not wire up these IRQ lines.
     */
    qdev_init_gpio_in_named(DEVICE(cpu), nios2_cpu_set_irq, "IRQ", 32);
#endif
}

static ObjectClass *nios2_cpu_class_by_name(const char *cpu_model)
{
    return object_class_by_name(TYPE_NIOS2_CPU);
}

static void nios2_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    Nios2CPUClass *ncc = NIOS2_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    ncc->parent_realize(dev, errp);
}

#ifndef CONFIG_USER_ONLY
static bool nios2_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
        (env->regs[CR_STATUS] & CR_STATUS_PIE)) {
        cs->exception_index = EXCP_IRQ;
        nios2_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}
#endif /* !CONFIG_USER_ONLY */

static void nios2_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    /* NOTE: NiosII R2 is not supported yet. */
    info->mach = bfd_arch_nios2;
    info->print_insn = print_insn_nios2;
}

static int nios2_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUNios2State *env = &cpu->env;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    if (n < 32) {          /* GP regs */
        return gdb_get_reg32(mem_buf, env->regs[n]);
    } else if (n == 32) {    /* PC */
        return gdb_get_reg32(mem_buf, env->regs[R_PC]);
    } else if (n < 49) {     /* Status regs */
        return gdb_get_reg32(mem_buf, env->regs[n - 1]);
    }

    /* Invalid regs */
    return 0;
}

static int nios2_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUNios2State *env = &cpu->env;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    if (n < 32) {            /* GP regs */
        env->regs[n] = ldl_p(mem_buf);
    } else if (n == 32) {    /* PC */
        env->regs[R_PC] = ldl_p(mem_buf);
    } else if (n < 49) {     /* Status regs */
        env->regs[n - 1] = ldl_p(mem_buf);
    }

    return 4;
}

static Property nios2_properties[] = {
    DEFINE_PROP_BOOL("mmu_present", Nios2CPU, mmu_present, true),
    /* ALTR,pid-num-bits */
    DEFINE_PROP_UINT32("mmu_pid_num_bits", Nios2CPU, pid_num_bits, 8),
    /* ALTR,tlb-num-ways */
    DEFINE_PROP_UINT32("mmu_tlb_num_ways", Nios2CPU, tlb_num_ways, 16),
    /* ALTR,tlb-num-entries */
    DEFINE_PROP_UINT32("mmu_pid_num_entries", Nios2CPU, tlb_num_entries, 256),
    DEFINE_PROP_END_OF_LIST(),
};

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps nios2_sysemu_ops = {
    .get_phys_page_debug = nios2_cpu_get_phys_page_debug,
};
#endif

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps nios2_tcg_ops = {
    .initialize = nios2_tcg_init,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = nios2_cpu_record_sigsegv,
#else
    .tlb_fill = nios2_cpu_tlb_fill,
    .cpu_exec_interrupt = nios2_cpu_exec_interrupt,
    .do_interrupt = nios2_cpu_do_interrupt,
    .do_unaligned_access = nios2_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};

static void nios2_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    Nios2CPUClass *ncc = NIOS2_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, nios2_cpu_realizefn,
                                    &ncc->parent_realize);
    device_class_set_props(dc, nios2_properties);
    device_class_set_parent_reset(dc, nios2_cpu_reset, &ncc->parent_reset);

    cc->class_by_name = nios2_cpu_class_by_name;
    cc->has_work = nios2_cpu_has_work;
    cc->dump_state = nios2_cpu_dump_state;
    cc->set_pc = nios2_cpu_set_pc;
    cc->disas_set_info = nios2_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &nios2_sysemu_ops;
#endif
    cc->gdb_read_register = nios2_cpu_gdb_read_register;
    cc->gdb_write_register = nios2_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 49;
    cc->tcg_ops = &nios2_tcg_ops;
}

static const TypeInfo nios2_cpu_type_info = {
    .name = TYPE_NIOS2_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(Nios2CPU),
    .instance_init = nios2_cpu_initfn,
    .class_size = sizeof(Nios2CPUClass),
    .class_init = nios2_cpu_class_init,
};

static void nios2_cpu_register_types(void)
{
    type_register_static(&nios2_cpu_type_info);
}

type_init(nios2_cpu_register_types)
