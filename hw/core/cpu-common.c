/*
 * QEMU CPU model
 *
 * Copyright (c) 2012-2014 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/cpu.h"
#include "sysemu/hw_accel.h"
#include "qemu/notify.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "exec/log.h"
#include "exec/cpu-common.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "sysemu/tcg.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "trace/trace-root.h"
#include "qemu/plugin.h"

CPUState *cpu_by_arch_id(int64_t id)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CPUClass *cc = CPU_GET_CLASS(cpu);

        if (cc->get_arch_id(cpu) == id) {
            return cpu;
        }
    }
    return NULL;
}

bool cpu_exists(int64_t id)
{
    return !!cpu_by_arch_id(id);
}

CPUState *cpu_create(const char *typename)
{
    Error *err = NULL;
    CPUState *cpu = CPU(object_new(typename));
    if (!qdev_realize(DEVICE(cpu), NULL, &err)) {
        error_report_err(err);
        object_unref(OBJECT(cpu));
        exit(EXIT_FAILURE);
    }
    return cpu;
}

/* Resetting the IRQ comes from across the code base so we take the
 * BQL here if we need to.  cpu_interrupt assumes it is held.*/
void cpu_reset_interrupt(CPUState *cpu, int mask)
{
    bool need_lock = !qemu_mutex_iothread_locked();

    if (need_lock) {
        qemu_mutex_lock_iothread();
    }
    cpu->interrupt_request &= ~mask;
    if (need_lock) {
        qemu_mutex_unlock_iothread();
    }
}

void cpu_exit(CPUState *cpu)
{
    qatomic_set(&cpu->exit_request, 1);
    /* Ensure cpu_exec will see the exit request after TCG has exited.  */
    smp_wmb();
    qatomic_set(&cpu->icount_decr_ptr->u16.high, -1);
}

static int cpu_common_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg)
{
    return 0;
}

static int cpu_common_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg)
{
    return 0;
}

void cpu_dump_state(CPUState *cpu, FILE *f, int flags)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->dump_state) {
        cpu_synchronize_state(cpu);
        cc->dump_state(cpu, f, flags);
    }
}

void cpu_reset(CPUState *cpu)
{
    device_cold_reset(DEVICE(cpu));

    trace_guest_cpu_reset(cpu);
}

static void cpu_common_reset(DeviceState *dev)
{
    CPUState *cpu = CPU(dev);
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", cpu->cpu_index);
        log_cpu_state(cpu, cc->reset_dump_flags);
    }

    cpu->interrupt_request = 0;
    cpu->halted = cpu->start_powered_off;
    cpu->mem_io_pc = 0;
    cpu->icount_extra = 0;
    qatomic_set(&cpu->icount_decr_ptr->u32, 0);
    cpu->can_do_io = 1;
    cpu->exception_index = -1;
    cpu->crash_occurred = false;
    cpu->cflags_next_tb = -1;

    if (tcg_enabled()) {
        cpu_tb_jmp_cache_clear(cpu);

        tcg_flush_softmmu_tlb(cpu);
    }
}

static bool cpu_common_has_work(CPUState *cs)
{
    return false;
}

ObjectClass *cpu_class_by_name(const char *typename, const char *cpu_model)
{
    CPUClass *cc = CPU_CLASS(object_class_by_name(typename));

    assert(cpu_model && cc->class_by_name);
    return cc->class_by_name(cpu_model);
}

static void cpu_common_parse_features(const char *typename, char *features,
                                      Error **errp)
{
    char *val;
    static bool cpu_globals_initialized;
    /* Single "key=value" string being parsed */
    char *featurestr = features ? strtok(features, ",") : NULL;

    /* should be called only once, catch invalid users */
    assert(!cpu_globals_initialized);
    cpu_globals_initialized = true;

    while (featurestr) {
        val = strchr(featurestr, '=');
        if (val) {
            GlobalProperty *prop = g_new0(typeof(*prop), 1);
            *val = 0;
            val++;
            prop->driver = typename;
            prop->property = g_strdup(featurestr);
            prop->value = g_strdup(val);
            qdev_prop_register_global(prop);
        } else {
            error_setg(errp, "Expected key=value format, found %s.",
                       featurestr);
            return;
        }
        featurestr = strtok(NULL, ",");
    }
}

static void cpu_common_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cpu = CPU(dev);
    Object *machine = qdev_get_machine();

    /* qdev_get_machine() can return something that's not TYPE_MACHINE
     * if this is one of the user-only emulators; in that case there's
     * no need to check the ignore_memory_transaction_failures board flag.
     */
    if (object_dynamic_cast(machine, TYPE_MACHINE)) {
        ObjectClass *oc = object_get_class(machine);
        MachineClass *mc = MACHINE_CLASS(oc);

        if (mc) {
            cpu->ignore_memory_transaction_failures =
                mc->ignore_memory_transaction_failures;
        }
    }

    if (dev->hotplugged) {
        cpu_synchronize_post_init(cpu);
        cpu_resume(cpu);
    }

    /* NOTE: latest generic point where the cpu is fully realized */
    trace_init_vcpu(cpu);
}

static void cpu_common_unrealizefn(DeviceState *dev)
{
    CPUState *cpu = CPU(dev);

    /* NOTE: latest generic point before the cpu is fully unrealized */
    trace_fini_vcpu(cpu);
    cpu_exec_unrealizefn(cpu);
}

static void cpu_common_initfn(Object *obj)
{
    CPUState *cpu = CPU(obj);
    CPUClass *cc = CPU_GET_CLASS(obj);

    cpu->cpu_index = UNASSIGNED_CPU_INDEX;
    cpu->cluster_index = UNASSIGNED_CLUSTER_INDEX;
    cpu->gdb_num_regs = cpu->gdb_num_g_regs = cc->gdb_num_core_regs;
    /* *-user doesn't have configurable SMP topology */
    /* the default value is changed by qemu_init_vcpu() for softmmu */
    cpu->nr_cores = 1;
    cpu->nr_threads = 1;

    qemu_mutex_init(&cpu->work_mutex);
    QSIMPLEQ_INIT(&cpu->work_list);
    QTAILQ_INIT(&cpu->breakpoints);
    QTAILQ_INIT(&cpu->watchpoints);

    cpu_exec_initfn(cpu);
}

static void cpu_common_finalize(Object *obj)
{
    CPUState *cpu = CPU(obj);

    qemu_mutex_destroy(&cpu->work_mutex);
}

static int64_t cpu_common_get_arch_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

static Property cpu_common_props[] = {
#ifndef CONFIG_USER_ONLY
    /* Create a memory property for softmmu CPU object,
     * so users can wire up its memory. (This can't go in hw/core/cpu.c
     * because that file is compiled only once for both user-mode
     * and system builds.) The default if no link is set up is to use
     * the system address space.
     */
    DEFINE_PROP_LINK("memory", CPUState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
#endif
    DEFINE_PROP_BOOL("start-powered-off", CPUState, start_powered_off, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void cpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClass *k = CPU_CLASS(klass);

    k->parse_features = cpu_common_parse_features;
    k->get_arch_id = cpu_common_get_arch_id;
    k->has_work = cpu_common_has_work;
    k->gdb_read_register = cpu_common_gdb_read_register;
    k->gdb_write_register = cpu_common_gdb_write_register;
    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
    dc->realize = cpu_common_realizefn;
    dc->unrealize = cpu_common_unrealizefn;
    dc->reset = cpu_common_reset;
    device_class_set_props(dc, cpu_common_props);
    /*
     * Reason: CPUs still need special care by board code: wiring up
     * IRQs, adding reset handlers, halting non-first CPUs, ...
     */
    dc->user_creatable = false;
}

static const TypeInfo cpu_type_info = {
    .name = TYPE_CPU,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CPUState),
    .instance_init = cpu_common_initfn,
    .instance_finalize = cpu_common_finalize,
    .abstract = true,
    .class_size = sizeof(CPUClass),
    .class_init = cpu_class_init,
};

static void cpu_register_types(void)
{
    type_register_static(&cpu_type_info);
}

type_init(cpu_register_types)
