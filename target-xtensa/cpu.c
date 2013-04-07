/*
 * QEMU Xtensa CPU
 *
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"


/* CPUClass::reset() */
static void xtensa_cpu_reset(CPUState *s)
{
    XtensaCPU *cpu = XTENSA_CPU(s);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(cpu);
    CPUXtensaState *env = &cpu->env;

    xcc->parent_reset(s);

    env->exception_taken = 0;
    env->pc = env->config->exception_vector[EXC_RESET];
    env->sregs[LITBASE] &= ~1;
    env->sregs[PS] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_INTERRUPT) ? 0x1f : 0x10;
    env->sregs[VECBASE] = env->config->vecbase;
    env->sregs[IBREAKENABLE] = 0;
    env->sregs[CACHEATTR] = 0x22222222;
    env->sregs[ATOMCTL] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_ATOMCTL) ? 0x28 : 0x15;

    env->pending_irq_level = 0;
    reset_mmu(env);
}

static void xtensa_cpu_realizefn(DeviceState *dev, Error **errp)
{
    XtensaCPU *cpu = XTENSA_CPU(dev);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(dev);

    qemu_init_vcpu(&cpu->env);

    xcc->parent_realize(dev, errp);
}

static void xtensa_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    XtensaCPU *cpu = XTENSA_CPU(obj);
    CPUXtensaState *env = &cpu->env;
    static bool tcg_inited;

    cs->env_ptr = env;
    cpu_exec_init(env);

    if (tcg_enabled() && !tcg_inited) {
        tcg_inited = true;
        xtensa_translate_init();
        cpu_set_debug_excp_handler(xtensa_breakpoint_handler);
    }
}

static const VMStateDescription vmstate_xtensa_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static void xtensa_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    XtensaCPUClass *xcc = XTENSA_CPU_CLASS(cc);

    xcc->parent_realize = dc->realize;
    dc->realize = xtensa_cpu_realizefn;

    xcc->parent_reset = cc->reset;
    cc->reset = xtensa_cpu_reset;

    cc->do_interrupt = xtensa_cpu_do_interrupt;
    dc->vmsd = &vmstate_xtensa_cpu;
}

static const TypeInfo xtensa_cpu_type_info = {
    .name = TYPE_XTENSA_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(XtensaCPU),
    .instance_init = xtensa_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(XtensaCPUClass),
    .class_init = xtensa_cpu_class_init,
};

static void xtensa_cpu_register_types(void)
{
    type_register_static(&xtensa_cpu_type_info);
}

type_init(xtensa_cpu_register_types)
