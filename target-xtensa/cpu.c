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

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"


static void xtensa_cpu_set_pc(CPUState *cs, vaddr value)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    cpu->env.pc = value;
}

static bool xtensa_cpu_has_work(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    return cpu->env.pending_irq_level;
}

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
    env->sregs[CONFIGID0] = env->config->configid[0];
    env->sregs[CONFIGID1] = env->config->configid[1];

    env->pending_irq_level = 0;
    reset_mmu(env);
}

static ObjectClass *xtensa_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_XTENSA_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc == NULL || !object_class_dynamic_cast(oc, TYPE_XTENSA_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }
    return oc;
}

static void xtensa_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(dev);

    cs->gdb_num_regs = xcc->config->gdb_regmap.num_regs;

    qemu_init_vcpu(cs);

    xcc->parent_realize(dev, errp);
}

static void xtensa_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    XtensaCPU *cpu = XTENSA_CPU(obj);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(obj);
    CPUXtensaState *env = &cpu->env;
    static bool tcg_inited;

    cs->env_ptr = env;
    env->config = xcc->config;
    cpu_exec_init(cs, &error_abort);

    if (tcg_enabled() && !tcg_inited) {
        tcg_inited = true;
        xtensa_translate_init();
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

    cc->class_by_name = xtensa_cpu_class_by_name;
    cc->has_work = xtensa_cpu_has_work;
    cc->do_interrupt = xtensa_cpu_do_interrupt;
    cc->cpu_exec_interrupt = xtensa_cpu_exec_interrupt;
    cc->dump_state = xtensa_cpu_dump_state;
    cc->set_pc = xtensa_cpu_set_pc;
    cc->gdb_read_register = xtensa_cpu_gdb_read_register;
    cc->gdb_write_register = xtensa_cpu_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;
#ifndef CONFIG_USER_ONLY
    cc->do_unaligned_access = xtensa_cpu_do_unaligned_access;
    cc->get_phys_page_debug = xtensa_cpu_get_phys_page_debug;
    cc->do_unassigned_access = xtensa_cpu_do_unassigned_access;
#endif
    cc->debug_excp_handler = xtensa_breakpoint_handler;
    dc->vmsd = &vmstate_xtensa_cpu;

    /*
     * Reason: xtensa_cpu_initfn() calls cpu_exec_init(), which saves
     * the object in cpus -> dangling pointer after final
     * object_unref().
     */
    dc->cannot_destroy_with_object_finalize_yet = true;
}

static const TypeInfo xtensa_cpu_type_info = {
    .name = TYPE_XTENSA_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(XtensaCPU),
    .instance_init = xtensa_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(XtensaCPUClass),
    .class_init = xtensa_cpu_class_init,
};

static void xtensa_cpu_register_types(void)
{
    type_register_static(&xtensa_cpu_type_info);
}

type_init(xtensa_cpu_register_types)
