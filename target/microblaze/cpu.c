/*
 * QEMU MicroBlaze CPU
 *
 * Copyright (c) 2009 Edgar E. Iglesias
 * Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * Copyright (c) 2009 Edgar E. Iglesias, Axis Communications AB.
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
#include "cpu.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/exec-all.h"
#include "fpu/softfloat-helpers.h"

static const struct {
    const char *name;
    uint8_t version_id;
} mb_cpu_lookup[] = {
    /* These key value are as per MBV field in PVR0 */
    {"5.00.a", 0x01},
    {"5.00.b", 0x02},
    {"5.00.c", 0x03},
    {"6.00.a", 0x04},
    {"6.00.b", 0x06},
    {"7.00.a", 0x05},
    {"7.00.b", 0x07},
    {"7.10.a", 0x08},
    {"7.10.b", 0x09},
    {"7.10.c", 0x0a},
    {"7.10.d", 0x0b},
    {"7.20.a", 0x0c},
    {"7.20.b", 0x0d},
    {"7.20.c", 0x0e},
    {"7.20.d", 0x0f},
    {"7.30.a", 0x10},
    {"7.30.b", 0x11},
    {"8.00.a", 0x12},
    {"8.00.b", 0x13},
    {"8.10.a", 0x14},
    {"8.20.a", 0x15},
    {"8.20.b", 0x16},
    {"8.30.a", 0x17},
    {"8.40.a", 0x18},
    {"8.40.b", 0x19},
    {"8.50.a", 0x1A},
    {"9.0", 0x1B},
    {"9.1", 0x1D},
    {"9.2", 0x1F},
    {"9.3", 0x20},
    {"9.4", 0x21},
    {"9.5", 0x22},
    {"9.6", 0x23},
    {"10.0", 0x24},
    {NULL, 0},
};

/* If no specific version gets selected, default to the following.  */
#define DEFAULT_CPU_VERSION "10.0"

static void mb_cpu_set_pc(CPUState *cs, vaddr value)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);

    cpu->env.sregs[SR_PC] = value;
}

static bool mb_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI);
}

#ifndef CONFIG_USER_ONLY
static void microblaze_cpu_set_irq(void *opaque, int irq, int level)
{
    MicroBlazeCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    int type = irq ? CPU_INTERRUPT_NMI : CPU_INTERRUPT_HARD;

    if (level) {
        cpu_interrupt(cs, type);
    } else {
        cpu_reset_interrupt(cs, type);
    }
}
#endif

static void mb_cpu_reset(DeviceState *dev)
{
    CPUState *s = CPU(dev);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(s);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_GET_CLASS(cpu);
    CPUMBState *env = &cpu->env;

    mcc->parent_reset(dev);

    memset(env, 0, offsetof(CPUMBState, end_reset_fields));
    env->res_addr = RES_ADDR_NONE;

    /* Disable stack protector.  */
    env->shr = ~0;

    env->sregs[SR_PC] = cpu->cfg.base_vectors;

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->sregs[SR_MSR] = MSR_EE | MSR_IE | MSR_VM | MSR_UM;
#else
    env->sregs[SR_MSR] = 0;
    mmu_init(&env->mmu);
    env->mmu.c_mmu = 3;
    env->mmu.c_mmu_tlb_access = 3;
    env->mmu.c_mmu_zones = 16;
    env->mmu.c_addr_mask = MAKE_64BIT_MASK(0, cpu->cfg.addr_size);
#endif
}

static void mb_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_arch_microblaze;
    info->print_insn = print_insn_microblaze;
}

static void mb_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_GET_CLASS(dev);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    uint8_t version_code = 0;
    const char *version;
    int i = 0;
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (cpu->cfg.addr_size < 32 || cpu->cfg.addr_size > 64) {
        error_setg(errp, "addr-size %d is out of range (32 - 64)",
                   cpu->cfg.addr_size);
        return;
    }

    qemu_init_vcpu(cs);

    env->pvr.regs[0] = PVR0_USE_EXC_MASK
                       | PVR0_USE_ICACHE_MASK
                       | PVR0_USE_DCACHE_MASK;
    env->pvr.regs[2] = PVR2_D_OPB_MASK
                        | PVR2_D_LMB_MASK
                        | PVR2_I_OPB_MASK
                        | PVR2_I_LMB_MASK
                        | PVR2_FPU_EXC_MASK
                        | 0;

    version = cpu->cfg.version ? cpu->cfg.version : DEFAULT_CPU_VERSION;
    for (i = 0; mb_cpu_lookup[i].name && version; i++) {
        if (strcmp(mb_cpu_lookup[i].name, version) == 0) {
            version_code = mb_cpu_lookup[i].version_id;
            break;
        }
    }

    if (!version_code) {
        qemu_log("Invalid MicroBlaze version number: %s\n", cpu->cfg.version);
    }

    env->pvr.regs[0] |= (cpu->cfg.stackprot ? PVR0_SPROT_MASK : 0) |
                        (cpu->cfg.use_fpu ? PVR0_USE_FPU_MASK : 0) |
                        (cpu->cfg.use_hw_mul ? PVR0_USE_HW_MUL_MASK : 0) |
                        (cpu->cfg.use_barrel ? PVR0_USE_BARREL_MASK : 0) |
                        (cpu->cfg.use_div ? PVR0_USE_DIV_MASK : 0) |
                        (cpu->cfg.use_mmu ? PVR0_USE_MMU_MASK : 0) |
                        (cpu->cfg.endi ? PVR0_ENDI_MASK : 0) |
                        (version_code << PVR0_VERSION_SHIFT) |
                        (cpu->cfg.pvr == C_PVR_FULL ? PVR0_PVR_FULL_MASK : 0) |
                        cpu->cfg.pvr_user1;

    env->pvr.regs[1] = cpu->cfg.pvr_user2;
    env->pvr.regs[2] |= (cpu->cfg.use_fpu ? PVR2_USE_FPU_MASK : 0) |
                        (cpu->cfg.use_fpu > 1 ? PVR2_USE_FPU2_MASK : 0) |
                        (cpu->cfg.use_hw_mul ? PVR2_USE_HW_MUL_MASK : 0) |
                        (cpu->cfg.use_hw_mul > 1 ? PVR2_USE_MUL64_MASK : 0) |
                        (cpu->cfg.use_barrel ? PVR2_USE_BARREL_MASK : 0) |
                        (cpu->cfg.use_div ? PVR2_USE_DIV_MASK : 0) |
                        (cpu->cfg.use_msr_instr ? PVR2_USE_MSR_INSTR : 0) |
                        (cpu->cfg.use_pcmp_instr ? PVR2_USE_PCMP_INSTR : 0) |
                        (cpu->cfg.dopb_bus_exception ?
                                                 PVR2_DOPB_BUS_EXC_MASK : 0) |
                        (cpu->cfg.iopb_bus_exception ?
                                                 PVR2_IOPB_BUS_EXC_MASK : 0) |
                        (cpu->cfg.div_zero_exception ?
                                                 PVR2_DIV_ZERO_EXC_MASK : 0) |
                        (cpu->cfg.illegal_opcode_exception ?
                                                PVR2_ILL_OPCODE_EXC_MASK : 0) |
                        (cpu->cfg.unaligned_exceptions ?
                                                PVR2_UNALIGNED_EXC_MASK : 0) |
                        (cpu->cfg.opcode_0_illegal ?
                                                 PVR2_OPCODE_0x0_ILL_MASK : 0);

    env->pvr.regs[5] |= cpu->cfg.dcache_writeback ?
                                        PVR5_DCACHE_WRITEBACK_MASK : 0;

    env->pvr.regs[10] = 0x0c000000 | /* Default to spartan 3a dsp family.  */
                        (cpu->cfg.addr_size - 32) << PVR10_ASIZE_SHIFT;
    env->pvr.regs[11] = (cpu->cfg.use_mmu ? PVR11_USE_MMU : 0) |
                        16 << 17;

    mcc->parent_realize(dev, errp);
}

static void mb_cpu_initfn(Object *obj)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(obj);
    CPUMBState *env = &cpu->env;

    cpu_set_cpustate_pointers(cpu);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);

#ifndef CONFIG_USER_ONLY
    /* Inbound IRQ and FIR lines */
    qdev_init_gpio_in(DEVICE(cpu), microblaze_cpu_set_irq, 2);
#endif
}

static const VMStateDescription vmstate_mb_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static Property mb_properties[] = {
    DEFINE_PROP_UINT32("base-vectors", MicroBlazeCPU, cfg.base_vectors, 0),
    DEFINE_PROP_BOOL("use-stack-protection", MicroBlazeCPU, cfg.stackprot,
                     false),
    /*
     * This is the C_ADDR_SIZE synth-time configuration option of the
     * MicroBlaze cores. Supported values range between 32 and 64.
     *
     * When set to > 32, 32bit MicroBlaze can emit load/stores
     * with extended addressing.
     */
    DEFINE_PROP_UINT8("addr-size", MicroBlazeCPU, cfg.addr_size, 32),
    /* If use-fpu > 0 - FPU is enabled
     * If use-fpu = 2 - Floating point conversion and square root instructions
     *                  are enabled
     */
    DEFINE_PROP_UINT8("use-fpu", MicroBlazeCPU, cfg.use_fpu, 2),
    /* If use-hw-mul > 0 - Multiplier is enabled
     * If use-hw-mul = 2 - 64-bit multiplier is enabled
     */
    DEFINE_PROP_UINT8("use-hw-mul", MicroBlazeCPU, cfg.use_hw_mul, 2),
    DEFINE_PROP_BOOL("use-barrel", MicroBlazeCPU, cfg.use_barrel, true),
    DEFINE_PROP_BOOL("use-div", MicroBlazeCPU, cfg.use_div, true),
    DEFINE_PROP_BOOL("use-msr-instr", MicroBlazeCPU, cfg.use_msr_instr, true),
    DEFINE_PROP_BOOL("use-pcmp-instr", MicroBlazeCPU, cfg.use_pcmp_instr, true),
    DEFINE_PROP_BOOL("use-mmu", MicroBlazeCPU, cfg.use_mmu, true),
    DEFINE_PROP_BOOL("dcache-writeback", MicroBlazeCPU, cfg.dcache_writeback,
                     false),
    DEFINE_PROP_BOOL("endianness", MicroBlazeCPU, cfg.endi, false),
    /* Enables bus exceptions on failed data accesses (load/stores).  */
    DEFINE_PROP_BOOL("dopb-bus-exception", MicroBlazeCPU,
                     cfg.dopb_bus_exception, false),
    /* Enables bus exceptions on failed instruction fetches.  */
    DEFINE_PROP_BOOL("iopb-bus-exception", MicroBlazeCPU,
                     cfg.iopb_bus_exception, false),
    DEFINE_PROP_BOOL("ill-opcode-exception", MicroBlazeCPU,
                     cfg.illegal_opcode_exception, false),
    DEFINE_PROP_BOOL("div-zero-exception", MicroBlazeCPU,
                     cfg.div_zero_exception, false),
    DEFINE_PROP_BOOL("unaligned-exceptions", MicroBlazeCPU,
                     cfg.unaligned_exceptions, false),
    DEFINE_PROP_BOOL("opcode-0x0-illegal", MicroBlazeCPU,
                     cfg.opcode_0_illegal, false),
    DEFINE_PROP_STRING("version", MicroBlazeCPU, cfg.version),
    DEFINE_PROP_UINT8("pvr", MicroBlazeCPU, cfg.pvr, C_PVR_FULL),
    DEFINE_PROP_UINT8("pvr-user1", MicroBlazeCPU, cfg.pvr_user1, 0),
    DEFINE_PROP_UINT32("pvr-user2", MicroBlazeCPU, cfg.pvr_user2, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static ObjectClass *mb_cpu_class_by_name(const char *cpu_model)
{
    return object_class_by_name(TYPE_MICROBLAZE_CPU);
}

static void mb_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, mb_cpu_realizefn,
                                    &mcc->parent_realize);
    device_class_set_parent_reset(dc, mb_cpu_reset, &mcc->parent_reset);

    cc->class_by_name = mb_cpu_class_by_name;
    cc->has_work = mb_cpu_has_work;
    cc->do_interrupt = mb_cpu_do_interrupt;
    cc->cpu_exec_interrupt = mb_cpu_exec_interrupt;
    cc->dump_state = mb_cpu_dump_state;
    cc->set_pc = mb_cpu_set_pc;
    cc->gdb_read_register = mb_cpu_gdb_read_register;
    cc->gdb_write_register = mb_cpu_gdb_write_register;
    cc->tlb_fill = mb_cpu_tlb_fill;
#ifndef CONFIG_USER_ONLY
    cc->do_transaction_failed = mb_cpu_transaction_failed;
    cc->get_phys_page_debug = mb_cpu_get_phys_page_debug;
#endif
    dc->vmsd = &vmstate_mb_cpu;
    device_class_set_props(dc, mb_properties);
    cc->gdb_num_core_regs = 32 + 27;

    cc->disas_set_info = mb_disas_set_info;
    cc->tcg_initialize = mb_tcg_init;
}

static const TypeInfo mb_cpu_type_info = {
    .name = TYPE_MICROBLAZE_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(MicroBlazeCPU),
    .instance_init = mb_cpu_initfn,
    .class_size = sizeof(MicroBlazeCPUClass),
    .class_init = mb_cpu_class_init,
};

static void mb_cpu_register_types(void)
{
    type_register_static(&mb_cpu_type_info);
}

type_init(mb_cpu_register_types)
