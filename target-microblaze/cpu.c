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
#include "qemu-common.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/exec-all.h"

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
    {NULL, 0},
};

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

/* CPUClass::reset() */
static void mb_cpu_reset(CPUState *s)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(s);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_GET_CLASS(cpu);
    CPUMBState *env = &cpu->env;

    mcc->parent_reset(s);

    memset(env, 0, offsetof(CPUMBState, pvr));
    env->res_addr = RES_ADDR_NONE;
    tlb_flush(s, 1);

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
    int i = 0;

    qemu_init_vcpu(cs);

    env->pvr.regs[0] = PVR0_USE_BARREL_MASK \
                       | PVR0_USE_DIV_MASK \
                       | PVR0_USE_HW_MUL_MASK \
                       | PVR0_USE_EXC_MASK \
                       | PVR0_USE_ICACHE_MASK \
                       | PVR0_USE_DCACHE_MASK \
                       | (0xb << 8);
    env->pvr.regs[2] = PVR2_D_OPB_MASK \
                        | PVR2_D_LMB_MASK \
                        | PVR2_I_OPB_MASK \
                        | PVR2_I_LMB_MASK \
                        | PVR2_USE_MSR_INSTR \
                        | PVR2_USE_PCMP_INSTR \
                        | PVR2_USE_BARREL_MASK \
                        | PVR2_USE_DIV_MASK \
                        | PVR2_USE_HW_MUL_MASK \
                        | PVR2_USE_MUL64_MASK \
                        | PVR2_FPU_EXC_MASK \
                        | 0;

    for (i = 0; mb_cpu_lookup[i].name && cpu->cfg.version; i++) {
        if (strcmp(mb_cpu_lookup[i].name, cpu->cfg.version) == 0) {
            version_code = mb_cpu_lookup[i].version_id;
            break;
        }
    }

    if (!version_code) {
        qemu_log("Invalid MicroBlaze version number: %s\n", cpu->cfg.version);
    }

    env->pvr.regs[0] |= (cpu->cfg.stackprot ? PVR0_SPROT_MASK : 0) |
                        (cpu->cfg.use_fpu ? PVR0_USE_FPU_MASK : 0) |
                        (cpu->cfg.use_mmu ? PVR0_USE_MMU_MASK : 0) |
                        (cpu->cfg.endi ? PVR0_ENDI_MASK : 0) |
                        (version_code << 16) |
                        (cpu->cfg.pvr == C_PVR_FULL ? PVR0_PVR_FULL_MASK : 0);

    env->pvr.regs[2] |= (cpu->cfg.use_fpu ? PVR2_USE_FPU_MASK : 0) |
                        (cpu->cfg.use_fpu > 1 ? PVR2_USE_FPU2_MASK : 0);

    env->pvr.regs[5] |= cpu->cfg.dcache_writeback ?
                                        PVR5_DCACHE_WRITEBACK_MASK : 0;

    env->pvr.regs[10] = 0x0c000000; /* Default to spartan 3a dsp family.  */
    env->pvr.regs[11] = PVR11_USE_MMU | (16 << 17);

    mcc->parent_realize(dev, errp);
}

static void mb_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(obj);
    CPUMBState *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(cs, &error_abort);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);

#ifndef CONFIG_USER_ONLY
    /* Inbound IRQ and FIR lines */
    qdev_init_gpio_in(DEVICE(cpu), microblaze_cpu_set_irq, 2);
#endif

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        mb_tcg_init();
    }
}

static const VMStateDescription vmstate_mb_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static Property mb_properties[] = {
    DEFINE_PROP_UINT32("base-vectors", MicroBlazeCPU, cfg.base_vectors, 0),
    DEFINE_PROP_BOOL("use-stack-protection", MicroBlazeCPU, cfg.stackprot,
                     false),
    /* If use-fpu > 0 - FPU is enabled
     * If use-fpu = 2 - Floating point conversion and square root instructions
     *                  are enabled
     */
    DEFINE_PROP_UINT8("use-fpu", MicroBlazeCPU, cfg.use_fpu, 2),
    DEFINE_PROP_BOOL("use-mmu", MicroBlazeCPU, cfg.use_mmu, true),
    DEFINE_PROP_BOOL("dcache-writeback", MicroBlazeCPU, cfg.dcache_writeback,
                     false),
    DEFINE_PROP_BOOL("endianness", MicroBlazeCPU, cfg.endi, false),
    DEFINE_PROP_STRING("version", MicroBlazeCPU, cfg.version),
    DEFINE_PROP_UINT8("pvr", MicroBlazeCPU, cfg.pvr, C_PVR_FULL),
    DEFINE_PROP_END_OF_LIST(),
};

static void mb_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_CLASS(oc);

    mcc->parent_realize = dc->realize;
    dc->realize = mb_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = mb_cpu_reset;

    cc->has_work = mb_cpu_has_work;
    cc->do_interrupt = mb_cpu_do_interrupt;
    cc->cpu_exec_interrupt = mb_cpu_exec_interrupt;
    cc->dump_state = mb_cpu_dump_state;
    cc->set_pc = mb_cpu_set_pc;
    cc->gdb_read_register = mb_cpu_gdb_read_register;
    cc->gdb_write_register = mb_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = mb_cpu_handle_mmu_fault;
#else
    cc->do_unassigned_access = mb_cpu_unassigned_access;
    cc->get_phys_page_debug = mb_cpu_get_phys_page_debug;
#endif
    dc->vmsd = &vmstate_mb_cpu;
    dc->props = mb_properties;
    cc->gdb_num_core_regs = 32 + 5;

    cc->disas_set_info = mb_disas_set_info;

    /*
     * Reason: mb_cpu_initfn() calls cpu_exec_init(), which saves the
     * object in cpus -> dangling pointer after final object_unref().
     */
    dc->cannot_destroy_with_object_finalize_yet = true;
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
