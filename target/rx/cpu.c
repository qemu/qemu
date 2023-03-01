/*
 * QEMU RX CPU
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "exec/exec-all.h"
#include "hw/loader.h"
#include "fpu/softfloat.h"

static void rx_cpu_set_pc(CPUState *cs, vaddr value)
{
    RXCPU *cpu = RX_CPU(cs);

    cpu->env.pc = value;
}

static vaddr rx_cpu_get_pc(CPUState *cs)
{
    RXCPU *cpu = RX_CPU(cs);

    return cpu->env.pc;
}

static void rx_cpu_synchronize_from_tb(CPUState *cs,
                                       const TranslationBlock *tb)
{
    RXCPU *cpu = RX_CPU(cs);

    tcg_debug_assert(!(cs->tcg_cflags & CF_PCREL));
    cpu->env.pc = tb->pc;
}

static void rx_restore_state_to_opc(CPUState *cs,
                                    const TranslationBlock *tb,
                                    const uint64_t *data)
{
    RXCPU *cpu = RX_CPU(cs);

    cpu->env.pc = data[0];
}

static bool rx_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request &
        (CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIR);
}

static void rx_cpu_reset_hold(Object *obj)
{
    RXCPU *cpu = RX_CPU(obj);
    RXCPUClass *rcc = RX_CPU_GET_CLASS(cpu);
    CPURXState *env = &cpu->env;
    uint32_t *resetvec;

    if (rcc->parent_phases.hold) {
        rcc->parent_phases.hold(obj);
    }

    memset(env, 0, offsetof(CPURXState, end_reset_fields));

    resetvec = rom_ptr(0xfffffffc, 4);
    if (resetvec) {
        /* In the case of kernel, it is ignored because it is not set. */
        env->pc = ldl_p(resetvec);
    }
    rx_cpu_unpack_psw(env, 0, 1);
    env->regs[0] = env->isp = env->usp = 0;
    env->fpsw = 0;
    set_flush_to_zero(1, &env->fp_status);
    set_flush_inputs_to_zero(1, &env->fp_status);
}

static void rx_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;

    qemu_printf("  %s\n", object_class_get_name(oc));
}

void rx_cpu_list(void)
{
    GSList *list;
    list = object_class_get_list_sorted(TYPE_RX_CPU, false);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, rx_cpu_list_entry, NULL);
    g_slist_free(list);
}

static ObjectClass *rx_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_RX_CPU) != NULL &&
        !object_class_is_abstract(oc)) {
        return oc;
    }
    typename = g_strdup_printf(RX_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && object_class_is_abstract(oc)) {
        oc = NULL;
    }

    return oc;
}

static void rx_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RXCPUClass *rcc = RX_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    rcc->parent_realize(dev, errp);
}

static void rx_cpu_set_irq(void *opaque, int no, int request)
{
    RXCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    int irq = request & 0xff;

    static const int mask[] = {
        [RX_CPU_IRQ] = CPU_INTERRUPT_HARD,
        [RX_CPU_FIR] = CPU_INTERRUPT_FIR,
    };
    if (irq) {
        cpu->env.req_irq = irq;
        cpu->env.req_ipl = (request >> 8) & 0x0f;
        cpu_interrupt(cs, mask[no]);
    } else {
        cpu_reset_interrupt(cs, mask[no]);
    }
}

static void rx_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_mach_rx;
    info->print_insn = print_insn_rx;
}

static bool rx_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr)
{
    uint32_t address, physical, prot;

    /* Linear mapping */
    address = physical = addr & TARGET_PAGE_MASK;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    tlb_set_page(cs, address, physical, prot, mmu_idx, TARGET_PAGE_SIZE);
    return true;
}

static void rx_cpu_init(Object *obj)
{
    CPUState *cs = CPU(obj);
    RXCPU *cpu = RX_CPU(obj);
    CPURXState *env = &cpu->env;

    cpu_set_cpustate_pointers(cpu);
    cs->env_ptr = env;
    qdev_init_gpio_in(DEVICE(cpu), rx_cpu_set_irq, 2);
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps rx_sysemu_ops = {
    .get_phys_page_debug = rx_cpu_get_phys_page_debug,
};
#endif

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps rx_tcg_ops = {
    .initialize = rx_translate_init,
    .synchronize_from_tb = rx_cpu_synchronize_from_tb,
    .restore_state_to_opc = rx_restore_state_to_opc,
    .tlb_fill = rx_cpu_tlb_fill,

#ifndef CONFIG_USER_ONLY
    .cpu_exec_interrupt = rx_cpu_exec_interrupt,
    .do_interrupt = rx_cpu_do_interrupt,
#endif /* !CONFIG_USER_ONLY */
};

static void rx_cpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClass *cc = CPU_CLASS(klass);
    RXCPUClass *rcc = RX_CPU_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_parent_realize(dc, rx_cpu_realize,
                                    &rcc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, rx_cpu_reset_hold, NULL,
                                       &rcc->parent_phases);

    cc->class_by_name = rx_cpu_class_by_name;
    cc->has_work = rx_cpu_has_work;
    cc->dump_state = rx_cpu_dump_state;
    cc->set_pc = rx_cpu_set_pc;
    cc->get_pc = rx_cpu_get_pc;

#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &rx_sysemu_ops;
#endif
    cc->gdb_read_register = rx_cpu_gdb_read_register;
    cc->gdb_write_register = rx_cpu_gdb_write_register;
    cc->disas_set_info = rx_cpu_disas_set_info;

    cc->gdb_num_core_regs = 26;
    cc->gdb_core_xml_file = "rx-core.xml";
    cc->tcg_ops = &rx_tcg_ops;
}

static const TypeInfo rx_cpu_info = {
    .name = TYPE_RX_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(RXCPU),
    .instance_init = rx_cpu_init,
    .abstract = true,
    .class_size = sizeof(RXCPUClass),
    .class_init = rx_cpu_class_init,
};

static const TypeInfo rx62n_rx_cpu_info = {
    .name = TYPE_RX62N_CPU,
    .parent = TYPE_RX_CPU,
};

static void rx_cpu_register_types(void)
{
    type_register_static(&rx_cpu_info);
    type_register_static(&rx62n_rx_cpu_info);
}

type_init(rx_cpu_register_types)
