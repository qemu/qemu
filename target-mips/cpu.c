/*
 * QEMU MIPS CPU
 *
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


/* CPUClass::reset() */
static void mips_cpu_reset(CPUState *s)
{
    MIPSCPU *cpu = MIPS_CPU(s);
    MIPSCPUClass *mcc = MIPS_CPU_GET_CLASS(cpu);
    CPUMIPSState *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    mcc->parent_reset(s);

    memset(env, 0, offsetof(CPUMIPSState, breakpoints));
    tlb_flush(env, 1);

    cpu_state_reset(env);
}

static void mips_cpu_realizefn(DeviceState *dev, Error **errp)
{
    MIPSCPU *cpu = MIPS_CPU(dev);
    MIPSCPUClass *mcc = MIPS_CPU_GET_CLASS(dev);

    cpu_reset(CPU(cpu));
    qemu_init_vcpu(&cpu->env);

    mcc->parent_realize(dev, errp);
}

static void mips_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    MIPSCPU *cpu = MIPS_CPU(obj);
    CPUMIPSState *env = &cpu->env;

    cs->env_ptr = env;
    cpu_exec_init(env);

    if (tcg_enabled()) {
        mips_tcg_init();
    }
}

static void mips_cpu_class_init(ObjectClass *c, void *data)
{
    MIPSCPUClass *mcc = MIPS_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    mcc->parent_realize = dc->realize;
    dc->realize = mips_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = mips_cpu_reset;

    cc->do_interrupt = mips_cpu_do_interrupt;
}

static const TypeInfo mips_cpu_type_info = {
    .name = TYPE_MIPS_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(MIPSCPU),
    .instance_init = mips_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(MIPSCPUClass),
    .class_init = mips_cpu_class_init,
};

static void mips_cpu_register_types(void)
{
    type_register_static(&mips_cpu_type_info);
}

type_init(mips_cpu_register_types)
