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
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "hw/loader.h"
#include "fpu/softfloat.h"
#include "tcg/debug-assert.h"
#include "accel/tcg/cpu-ops.h"

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

static TCGTBCPUState rx_get_tb_cpu_state(CPUState *cs)
{
    CPURXState *env = cpu_env(cs);
    uint32_t flags = 0;

    flags = FIELD_DP32(flags, PSW, PM, env->psw_pm);
    flags = FIELD_DP32(flags, PSW, U, env->psw_u);

    return (TCGTBCPUState){ .pc = env->pc, .flags = flags };
}

static void rx_cpu_synchronize_from_tb(CPUState *cs,
                                       const TranslationBlock *tb)
{
    RXCPU *cpu = RX_CPU(cs);

    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
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
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIR);
}

static int rx_cpu_mmu_index(CPUState *cs, bool ifunc)
{
    return 0;
}

static void rx_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    RXCPUClass *rcc = RX_CPU_GET_CLASS(obj);
    CPURXState *env = cpu_env(cs);
    uint32_t *resetvec;

    if (rcc->parent_phases.hold) {
        rcc->parent_phases.hold(obj, type);
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
    /*
     * TODO: this is not the correct NaN propagation rule for this
     * architecture. The "RX Family User's Manual: Software" table 1.6
     * defines the propagation rules as "prefer SNaN over QNaN;
     * then prefer dest over source", which is float_2nan_prop_s_ab.
     */
    set_float_2nan_prop_rule(float_2nan_prop_x87, &env->fp_status);
    /* Default NaN value: sign bit clear, set frac msb */
    set_float_default_nan_pattern(0b01000000, &env->fp_status);
    /*
     * TODO: "RX Family RXv1 Instruction Set Architecture" is not 100% clear
     * on whether flush-to-zero should happen before or after rounding, but
     * section 1.3.2 says that it happens when underflow is detected, and
     * implies that underflow is detected after rounding. So this may not
     * be the correct setting.
     */
    set_float_ftz_detection(float_ftz_before_rounding, &env->fp_status);
}

static ObjectClass *rx_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_RX_CPU) != NULL) {
        return oc;
    }
    typename = g_strdup_printf(RX_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

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
    info->endian = BFD_ENDIAN_LITTLE;
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
    RXCPU *cpu = RX_CPU(obj);

    qdev_init_gpio_in(DEVICE(cpu), rx_cpu_set_irq, 2);
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps rx_sysemu_ops = {
    .has_work = rx_cpu_has_work,
    .get_phys_page_debug = rx_cpu_get_phys_page_debug,
};

static const TCGCPUOps rx_tcg_ops = {
    /* MTTCG not yet supported: require strict ordering */
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = false,

    .initialize = rx_translate_init,
    .translate_code = rx_translate_code,
    .get_tb_cpu_state = rx_get_tb_cpu_state,
    .synchronize_from_tb = rx_cpu_synchronize_from_tb,
    .restore_state_to_opc = rx_restore_state_to_opc,
    .mmu_index = rx_cpu_mmu_index,
    .tlb_fill = rx_cpu_tlb_fill,
    .pointer_wrap = cpu_pointer_wrap_uint32,

    .cpu_exec_interrupt = rx_cpu_exec_interrupt,
    .cpu_exec_halt = rx_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = rx_cpu_do_interrupt,
};

static void rx_cpu_class_init(ObjectClass *klass, const void *data)
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
    cc->dump_state = rx_cpu_dump_state;
    cc->set_pc = rx_cpu_set_pc;
    cc->get_pc = rx_cpu_get_pc;

    cc->sysemu_ops = &rx_sysemu_ops;
    cc->gdb_read_register = rx_cpu_gdb_read_register;
    cc->gdb_write_register = rx_cpu_gdb_write_register;
    cc->disas_set_info = rx_cpu_disas_set_info;

    cc->gdb_core_xml_file = "rx-core.xml";
    cc->tcg_ops = &rx_tcg_ops;
}

static const TypeInfo rx_cpu_info = {
    .name = TYPE_RX_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(RXCPU),
    .instance_align = __alignof(RXCPU),
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
