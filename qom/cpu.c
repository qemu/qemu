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

#include "qemu-common.h"
#include "qom/cpu.h"
#include "sysemu/kvm.h"
#include "qemu/notify.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"

bool cpu_exists(int64_t id)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CPUClass *cc = CPU_GET_CLASS(cpu);

        if (cc->get_arch_id(cpu) == id) {
            return true;
        }
    }
    return false;
}

CPUState *cpu_generic_init(const char *typename, const char *cpu_model)
{
    char *str, *name, *featurestr;
    CPUState *cpu;
    ObjectClass *oc;
    CPUClass *cc;
    Error *err = NULL;

    str = g_strdup(cpu_model);
    name = strtok(str, ",");

    oc = cpu_class_by_name(typename, name);
    if (oc == NULL) {
        g_free(str);
        return NULL;
    }

    cpu = CPU(object_new(object_class_get_name(oc)));
    cc = CPU_GET_CLASS(cpu);

    featurestr = strtok(NULL, ",");
    cc->parse_features(cpu, featurestr, &err);
    g_free(str);
    if (err != NULL) {
        goto out;
    }

    object_property_set_bool(OBJECT(cpu), true, "realized", &err);

out:
    if (err != NULL) {
        error_report("%s", error_get_pretty(err));
        error_free(err);
        object_unref(OBJECT(cpu));
        return NULL;
    }

    return cpu;
}

bool cpu_paging_enabled(const CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return cc->get_paging_enabled(cpu);
}

static bool cpu_common_get_paging_enabled(const CPUState *cpu)
{
    return false;
}

void cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                            Error **errp)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return cc->get_memory_mapping(cpu, list, errp);
}

static void cpu_common_get_memory_mapping(CPUState *cpu,
                                          MemoryMappingList *list,
                                          Error **errp)
{
    error_setg(errp, "Obtaining memory mappings is unsupported on this CPU.");
}

/* CPU hot-plug notifiers */
static NotifierList cpu_added_notifiers =
    NOTIFIER_LIST_INITIALIZER(cpu_add_notifiers);

void qemu_register_cpu_added_notifier(Notifier *notifier)
{
    notifier_list_add(&cpu_added_notifiers, notifier);
}

void cpu_reset_interrupt(CPUState *cpu, int mask)
{
    cpu->interrupt_request &= ~mask;
}

void cpu_exit(CPUState *cpu)
{
    cpu->exit_request = 1;
    cpu->tcg_exit_req = 1;
}

int cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return (*cc->write_elf32_qemunote)(f, cpu, opaque);
}

static int cpu_common_write_elf32_qemunote(WriteCoreDumpFunction f,
                                           CPUState *cpu, void *opaque)
{
    return -1;
}

int cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return (*cc->write_elf32_note)(f, cpu, cpuid, opaque);
}

static int cpu_common_write_elf32_note(WriteCoreDumpFunction f,
                                       CPUState *cpu, int cpuid,
                                       void *opaque)
{
    return -1;
}

int cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return (*cc->write_elf64_qemunote)(f, cpu, opaque);
}

static int cpu_common_write_elf64_qemunote(WriteCoreDumpFunction f,
                                           CPUState *cpu, void *opaque)
{
    return -1;
}

int cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return (*cc->write_elf64_note)(f, cpu, cpuid, opaque);
}

static int cpu_common_write_elf64_note(WriteCoreDumpFunction f,
                                       CPUState *cpu, int cpuid,
                                       void *opaque)
{
    return -1;
}


static int cpu_common_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg)
{
    return 0;
}

static int cpu_common_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg)
{
    return 0;
}

bool target_words_bigendian(void);
static bool cpu_common_virtio_is_big_endian(CPUState *cpu)
{
    return target_words_bigendian();
}

static void cpu_common_noop(CPUState *cpu)
{
}

static bool cpu_common_exec_interrupt(CPUState *cpu, int int_req)
{
    return false;
}

void cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                    int flags)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->dump_state) {
        cpu_synchronize_state(cpu);
        cc->dump_state(cpu, f, cpu_fprintf, flags);
    }
}

void cpu_dump_statistics(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->dump_statistics) {
        cc->dump_statistics(cpu, f, cpu_fprintf, flags);
    }
}

void cpu_reset(CPUState *cpu)
{
    CPUClass *klass = CPU_GET_CLASS(cpu);

    if (klass->reset != NULL) {
        (*klass->reset)(cpu);
    }
}

static void cpu_common_reset(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", cpu->cpu_index);
        log_cpu_state(cpu, cc->reset_dump_flags);
    }

    cpu->interrupt_request = 0;
    cpu->current_tb = NULL;
    cpu->halted = 0;
    cpu->mem_io_pc = 0;
    cpu->mem_io_vaddr = 0;
    cpu->icount_extra = 0;
    cpu->icount_decr.u32 = 0;
    cpu->can_do_io = 0;
    memset(cpu->tb_jmp_cache, 0, TB_JMP_CACHE_SIZE * sizeof(void *));
}

static bool cpu_common_has_work(CPUState *cs)
{
    return false;
}

ObjectClass *cpu_class_by_name(const char *typename, const char *cpu_model)
{
    CPUClass *cc = CPU_CLASS(object_class_by_name(typename));

    return cc->class_by_name(cpu_model);
}

static ObjectClass *cpu_common_class_by_name(const char *cpu_model)
{
    return NULL;
}

static void cpu_common_parse_features(CPUState *cpu, char *features,
                                      Error **errp)
{
    char *featurestr; /* Single "key=value" string being parsed */
    char *val;
    Error *err = NULL;

    featurestr = features ? strtok(features, ",") : NULL;

    while (featurestr) {
        val = strchr(featurestr, '=');
        if (val) {
            *val = 0;
            val++;
            object_property_parse(OBJECT(cpu), val, featurestr, &err);
            if (err) {
                error_propagate(errp, err);
                return;
            }
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

    if (dev->hotplugged) {
        cpu_synchronize_post_init(cpu);
        notifier_list_notify(&cpu_added_notifiers, dev);
        cpu_resume(cpu);
    }
}

static void cpu_common_initfn(Object *obj)
{
    CPUState *cpu = CPU(obj);
    CPUClass *cc = CPU_GET_CLASS(obj);

    cpu->gdb_num_regs = cpu->gdb_num_g_regs = cc->gdb_num_core_regs;
}

static int64_t cpu_common_get_arch_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

static void cpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClass *k = CPU_CLASS(klass);

    k->class_by_name = cpu_common_class_by_name;
    k->parse_features = cpu_common_parse_features;
    k->reset = cpu_common_reset;
    k->get_arch_id = cpu_common_get_arch_id;
    k->has_work = cpu_common_has_work;
    k->get_paging_enabled = cpu_common_get_paging_enabled;
    k->get_memory_mapping = cpu_common_get_memory_mapping;
    k->write_elf32_qemunote = cpu_common_write_elf32_qemunote;
    k->write_elf32_note = cpu_common_write_elf32_note;
    k->write_elf64_qemunote = cpu_common_write_elf64_qemunote;
    k->write_elf64_note = cpu_common_write_elf64_note;
    k->gdb_read_register = cpu_common_gdb_read_register;
    k->gdb_write_register = cpu_common_gdb_write_register;
    k->virtio_is_big_endian = cpu_common_virtio_is_big_endian;
    k->debug_excp_handler = cpu_common_noop;
    k->cpu_exec_enter = cpu_common_noop;
    k->cpu_exec_exit = cpu_common_noop;
    k->cpu_exec_interrupt = cpu_common_exec_interrupt;
    dc->realize = cpu_common_realizefn;
    /*
     * Reason: CPUs still need special care by board code: wiring up
     * IRQs, adding reset handlers, halting non-first CPUs, ...
     */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo cpu_type_info = {
    .name = TYPE_CPU,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CPUState),
    .instance_init = cpu_common_initfn,
    .abstract = true,
    .class_size = sizeof(CPUClass),
    .class_init = cpu_class_init,
};

static void cpu_register_types(void)
{
    type_register_static(&cpu_type_info);
}

type_init(cpu_register_types)
