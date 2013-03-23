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


/* CPUClass::reset() */
static void lm32_cpu_reset(CPUState *s)
{
    LM32CPU *cpu = LM32_CPU(s);
    LM32CPUClass *lcc = LM32_CPU_GET_CLASS(cpu);
    CPULM32State *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    lcc->parent_reset(s);

    /* reset cpu state */
    memset(env, 0, offsetof(CPULM32State, breakpoints));

    tlb_flush(env, 1);
}

static void lm32_cpu_realizefn(DeviceState *dev, Error **errp)
{
    LM32CPU *cpu = LM32_CPU(dev);
    LM32CPUClass *lcc = LM32_CPU_GET_CLASS(dev);

    cpu_reset(CPU(cpu));

    qemu_init_vcpu(&cpu->env);

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
    cpu_class_set_vmsd(cc, &vmstate_lm32_cpu);
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
