/*
 * QEMU MicroBlaze CPU
 *
 * Copyright (c) 2009 Edgar E. Iglesias
 * Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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

#include "cpu.h"
#include "qemu-common.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"


/* CPUClass::reset() */
static void mb_cpu_reset(CPUState *s)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(s);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_GET_CLASS(cpu);
    CPUMBState *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    mcc->parent_reset(s);

    memset(env, 0, offsetof(CPUMBState, breakpoints));
    env->res_addr = RES_ADDR_NONE;
    tlb_flush(env, 1);

    /* Disable stack protector.  */
    env->shr = ~0;

    env->pvr.regs[0] = PVR0_PVR_FULL_MASK \
                       | PVR0_USE_BARREL_MASK \
                       | PVR0_USE_DIV_MASK \
                       | PVR0_USE_HW_MUL_MASK \
                       | PVR0_USE_EXC_MASK \
                       | PVR0_USE_ICACHE_MASK \
                       | PVR0_USE_DCACHE_MASK \
                       | PVR0_USE_MMU \
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
                        | PVR2_USE_FPU_MASK \
                        | PVR2_USE_FPU2_MASK \
                        | PVR2_FPU_EXC_MASK \
                        | 0;
    env->pvr.regs[10] = 0x0c000000; /* Default to spartan 3a dsp family.  */
    env->pvr.regs[11] = PVR11_USE_MMU | (16 << 17);

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->sregs[SR_MSR] = MSR_EE | MSR_IE | MSR_VM | MSR_UM;
    env->pvr.regs[10] = 0x0c000000; /* Spartan 3a dsp.  */
#else
    env->sregs[SR_MSR] = 0;
    mmu_init(&env->mmu);
    env->mmu.c_mmu = 3;
    env->mmu.c_mmu_tlb_access = 3;
    env->mmu.c_mmu_zones = 16;
#endif
}

static void mb_cpu_realizefn(DeviceState *dev, Error **errp)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(dev);
    MicroBlazeCPUClass *mcc = MICROBLAZE_CPU_GET_CLASS(dev);

    cpu_reset(CPU(cpu));
    qemu_init_vcpu(&cpu->env);

    mcc->parent_realize(dev, errp);
}

static void mb_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(obj);
    CPUMBState *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(env);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);

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
    DEFINE_PROP_UINT32("xlnx.base-vectors", MicroBlazeCPU, base_vectors, 0),
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

    cc->do_interrupt = mb_cpu_do_interrupt;
    dc->vmsd = &vmstate_mb_cpu;

    dc->props = mb_properties;
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
