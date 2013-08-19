/*
 * QEMU LatticeMico32 CPU
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


static void lm32_cpu_set_pc(CPUState *cs, vaddr value)
{
    LM32CPU *cpu = LM32_CPU(cs);

    cpu->env.pc = value;
}

/* CPUClass::reset() */
static void lm32_cpu_reset(CPUState *s)
{
    LM32CPU *cpu = LM32_CPU(s);
    LM32CPUClass *lcc = LM32_CPU_GET_CLASS(cpu);
    CPULM32State *env = &cpu->env;

    lcc->parent_reset(s);

    /* reset cpu state */
    memset(env, 0, offsetof(CPULM32State, breakpoints));

    tlb_flush(env, 1);
}

static void lm32_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    LM32CPUClass *lcc = LM32_CPU_GET_CLASS(dev);

    cpu_reset(cs);

    qemu_init_vcpu(cs);

    lcc->parent_realize(dev, errp);
}

static void lm32_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    LM32CPU *cpu = LM32_CPU(obj);
    CPULM32State *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(env);

    env->flags = 0;

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        lm32_translate_init();
    }
}

static void lm32_cpu_class_init(ObjectClass *oc, void *data)
{
    LM32CPUClass *lcc = LM32_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    lcc->parent_realize = dc->realize;
    dc->realize = lm32_cpu_realizefn;

    lcc->parent_reset = cc->reset;
    cc->reset = lm32_cpu_reset;

    cc->do_interrupt = lm32_cpu_do_interrupt;
    cc->dump_state = lm32_cpu_dump_state;
    cc->set_pc = lm32_cpu_set_pc;
    cc->gdb_read_register = lm32_cpu_gdb_read_register;
    cc->gdb_write_register = lm32_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->get_phys_page_debug = lm32_cpu_get_phys_page_debug;
    cc->vmsd = &vmstate_lm32_cpu;
#endif
    cc->gdb_num_core_regs = 32 + 7;
}

static const TypeInfo lm32_cpu_type_info = {
    .name = TYPE_LM32_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(LM32CPU),
    .instance_init = lm32_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(LM32CPUClass),
    .class_init = lm32_cpu_class_init,
};

static void lm32_cpu_register_types(void)
{
    type_register_static(&lm32_cpu_type_info);
}

type_init(lm32_cpu_register_types)
