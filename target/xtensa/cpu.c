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
#include "qapi/error.h"
#include "cpu.h"
#include "fpu/softfloat.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/qdev-clock.h"
#ifndef CONFIG_USER_ONLY
#include "system/memory.h"
#endif


static void xtensa_cpu_set_pc(CPUState *cs, vaddr value)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    cpu->env.pc = value;
}

static vaddr xtensa_cpu_get_pc(CPUState *cs)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    return cpu->env.pc;
}

static void xtensa_restore_state_to_opc(CPUState *cs,
                                        const TranslationBlock *tb,
                                        const uint64_t *data)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    cpu->env.pc = data[0];
}

#ifndef CONFIG_USER_ONLY
static bool xtensa_cpu_has_work(CPUState *cs)
{
    CPUXtensaState *env = cpu_env(cs);

    return !env->runstall && env->pending_irq_level;
}
#endif /* !CONFIG_USER_ONLY */

static int xtensa_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return xtensa_get_cring(cpu_env(cs));
}

#ifdef CONFIG_USER_ONLY
static bool abi_call0;

void xtensa_set_abi_call0(void)
{
    abi_call0 = true;
}

bool xtensa_abi_call0(void)
{
    return abi_call0;
}
#endif

static void xtensa_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(obj);
    CPUXtensaState *env = cpu_env(cs);
    bool dfpu = xtensa_option_enabled(env->config,
                                      XTENSA_OPTION_DFP_COPROCESSOR);

    if (xcc->parent_phases.hold) {
        xcc->parent_phases.hold(obj, type);
    }

    env->pc = env->config->exception_vector[EXC_RESET0 + env->static_vectors];
    env->sregs[LITBASE] &= ~1;
#ifndef CONFIG_USER_ONLY
    env->sregs[PS] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_INTERRUPT) ? 0x1f : 0x10;
    env->pending_irq_level = 0;
#else
    env->sregs[PS] = PS_UM | (3 << PS_RING_SHIFT);
    if (xtensa_option_enabled(env->config,
                              XTENSA_OPTION_WINDOWED_REGISTER) &&
        !xtensa_abi_call0()) {
        env->sregs[PS] |= PS_WOE;
    }
    env->sregs[CPENABLE] = 0xff;
#endif
    env->sregs[VECBASE] = env->config->vecbase;
    env->sregs[IBREAKENABLE] = 0;
    env->sregs[MEMCTL] = MEMCTL_IL0EN & env->config->memctl_mask;
    env->sregs[ATOMCTL] = xtensa_option_enabled(env->config,
            XTENSA_OPTION_ATOMCTL) ? 0x28 : 0x15;
    env->sregs[CONFIGID0] = env->config->configid[0];
    env->sregs[CONFIGID1] = env->config->configid[1];
    env->exclusive_addr = -1;

#ifndef CONFIG_USER_ONLY
    reset_mmu(env);
    cs->halted = env->runstall;
#endif
    /* For inf * 0 + NaN, return the input NaN */
    set_float_infzeronan_rule(float_infzeronan_dnan_never, &env->fp_status);
    set_no_signaling_nans(!dfpu, &env->fp_status);
    /* Default NaN value: sign bit clear, set frac msb */
    set_float_default_nan_pattern(0b01000000, &env->fp_status);
    xtensa_use_first_nan(env, !dfpu);
}

static ObjectClass *xtensa_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = g_strdup_printf(XTENSA_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    return oc;
}

static void xtensa_cpu_disas_set_info(CPUState *cs, disassemble_info *info)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);

    info->private_data = cpu->env.config->isa;
    info->print_insn = print_insn_xtensa;
    info->endian = TARGET_BIG_ENDIAN ? BFD_ENDIAN_BIG
                                     : BFD_ENDIAN_LITTLE;
}

static void xtensa_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

#ifndef CONFIG_USER_ONLY
    xtensa_irq_init(&XTENSA_CPU(dev)->env);
#endif

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    cs->gdb_num_regs = xcc->config->gdb_regmap.num_regs;

    qemu_init_vcpu(cs);

    xcc->parent_realize(dev, errp);
}

static void xtensa_cpu_initfn(Object *obj)
{
    XtensaCPU *cpu = XTENSA_CPU(obj);
    XtensaCPUClass *xcc = XTENSA_CPU_GET_CLASS(obj);
    CPUXtensaState *env = &cpu->env;

    env->config = xcc->config;

#ifndef CONFIG_USER_ONLY
    env->address_space_er = g_malloc(sizeof(*env->address_space_er));
    env->system_er = g_malloc(sizeof(*env->system_er));
    memory_region_init_io(env->system_er, obj, NULL, env, "er",
                          UINT64_C(0x100000000));
    address_space_init(env->address_space_er, env->system_er, "ER");

    cpu->clock = qdev_init_clock_in(DEVICE(obj), "clk-in", NULL, cpu, 0);
    clock_set_hz(cpu->clock, env->config->clock_freq_khz * 1000);
#endif
}

XtensaCPU *xtensa_cpu_create_with_clock(const char *cpu_type, Clock *cpu_refclk)
{
    DeviceState *cpu;

    cpu = qdev_new(cpu_type);
    qdev_connect_clock_in(cpu, "clk-in", cpu_refclk);
    qdev_realize(cpu, NULL, &error_abort);

    return XTENSA_CPU(cpu);
}

#ifndef CONFIG_USER_ONLY
static const VMStateDescription vmstate_xtensa_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps xtensa_sysemu_ops = {
    .has_work = xtensa_cpu_has_work,
    .get_phys_page_debug = xtensa_cpu_get_phys_page_debug,
};
#endif

#include "accel/tcg/cpu-ops.h"

static const TCGCPUOps xtensa_tcg_ops = {
    /* Xtensa processors have a weak memory model */
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = xtensa_translate_init,
    .translate_code = xtensa_translate_code,
    .debug_excp_handler = xtensa_breakpoint_handler,
    .restore_state_to_opc = xtensa_restore_state_to_opc,
    .mmu_index = xtensa_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = xtensa_cpu_tlb_fill,
    .cpu_exec_interrupt = xtensa_cpu_exec_interrupt,
    .cpu_exec_halt = xtensa_cpu_has_work,
    .do_interrupt = xtensa_cpu_do_interrupt,
    .do_transaction_failed = xtensa_cpu_do_transaction_failed,
    .do_unaligned_access = xtensa_cpu_do_unaligned_access,
    .debug_check_breakpoint = xtensa_debug_check_breakpoint,
#endif /* !CONFIG_USER_ONLY */
};

static void xtensa_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    XtensaCPUClass *xcc = XTENSA_CPU_CLASS(cc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, xtensa_cpu_realizefn,
                                    &xcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, xtensa_cpu_reset_hold, NULL,
                                       &xcc->parent_phases);

    cc->class_by_name = xtensa_cpu_class_by_name;
    cc->dump_state = xtensa_cpu_dump_state;
    cc->set_pc = xtensa_cpu_set_pc;
    cc->get_pc = xtensa_cpu_get_pc;
    cc->gdb_read_register = xtensa_cpu_gdb_read_register;
    cc->gdb_write_register = xtensa_cpu_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &xtensa_sysemu_ops;
    dc->vmsd = &vmstate_xtensa_cpu;
#endif
    cc->disas_set_info = xtensa_cpu_disas_set_info;
    cc->tcg_ops = &xtensa_tcg_ops;
}

static const TypeInfo xtensa_cpu_type_info = {
    .name = TYPE_XTENSA_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(XtensaCPU),
    .instance_align = __alignof(XtensaCPU),
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
