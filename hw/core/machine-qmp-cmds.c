/*
 * QMP commands related to machines and CPUs
 *
 * Copyright (C) 2014 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/main-loop.h"
#include "qom/qom-qobject.h"
#include "sysemu/hostmem.h"
#include "sysemu/hw_accel.h"
#include "sysemu/numa.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"

CpuInfoList *qmp_query_cpus(Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    CpuInfoList *head = NULL, *cur_item = NULL;
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CpuInfoList *info;
#if defined(TARGET_I386)
        X86CPU *x86_cpu = X86_CPU(cpu);
        CPUX86State *env = &x86_cpu->env;
#elif defined(TARGET_PPC)
        PowerPCCPU *ppc_cpu = POWERPC_CPU(cpu);
        CPUPPCState *env = &ppc_cpu->env;
#elif defined(TARGET_SPARC)
        SPARCCPU *sparc_cpu = SPARC_CPU(cpu);
        CPUSPARCState *env = &sparc_cpu->env;
#elif defined(TARGET_RISCV)
        RISCVCPU *riscv_cpu = RISCV_CPU(cpu);
        CPURISCVState *env = &riscv_cpu->env;
#elif defined(TARGET_MIPS)
        MIPSCPU *mips_cpu = MIPS_CPU(cpu);
        CPUMIPSState *env = &mips_cpu->env;
#elif defined(TARGET_TRICORE)
        TriCoreCPU *tricore_cpu = TRICORE_CPU(cpu);
        CPUTriCoreState *env = &tricore_cpu->env;
#elif defined(TARGET_S390X)
        S390CPU *s390_cpu = S390_CPU(cpu);
        CPUS390XState *env = &s390_cpu->env;
#endif

        cpu_synchronize_state(cpu);

        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->CPU = cpu->cpu_index;
        info->value->current = (cpu == first_cpu);
        info->value->halted = cpu->halted;
        info->value->qom_path = object_get_canonical_path(OBJECT(cpu));
        info->value->thread_id = cpu->thread_id;
#if defined(TARGET_I386)
        info->value->arch = CPU_INFO_ARCH_X86;
        info->value->u.x86.pc = env->eip + env->segs[R_CS].base;
#elif defined(TARGET_PPC)
        info->value->arch = CPU_INFO_ARCH_PPC;
        info->value->u.ppc.nip = env->nip;
#elif defined(TARGET_SPARC)
        info->value->arch = CPU_INFO_ARCH_SPARC;
        info->value->u.q_sparc.pc = env->pc;
        info->value->u.q_sparc.npc = env->npc;
#elif defined(TARGET_MIPS)
        info->value->arch = CPU_INFO_ARCH_MIPS;
        info->value->u.q_mips.PC = env->active_tc.PC;
#elif defined(TARGET_TRICORE)
        info->value->arch = CPU_INFO_ARCH_TRICORE;
        info->value->u.tricore.PC = env->PC;
#elif defined(TARGET_S390X)
        info->value->arch = CPU_INFO_ARCH_S390;
        info->value->u.s390.cpu_state = env->cpu_state;
#elif defined(TARGET_RISCV)
        info->value->arch = CPU_INFO_ARCH_RISCV;
        info->value->u.riscv.pc = env->pc;
#else
        info->value->arch = CPU_INFO_ARCH_OTHER;
#endif
        info->value->has_props = !!mc->cpu_index_to_instance_props;
        if (info->value->has_props) {
            CpuInstanceProperties *props;
            props = g_malloc0(sizeof(*props));
            *props = mc->cpu_index_to_instance_props(ms, cpu->cpu_index);
            info->value->props = props;
        }

        /* XXX: waiting for the qapi to support GSList */
        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
}

static CpuInfoArch sysemu_target_to_cpuinfo_arch(SysEmuTarget target)
{
    /*
     * The @SysEmuTarget -> @CpuInfoArch mapping below is based on the
     * TARGET_ARCH -> TARGET_BASE_ARCH mapping in the "configure" script.
     */
    switch (target) {
    case SYS_EMU_TARGET_I386:
    case SYS_EMU_TARGET_X86_64:
        return CPU_INFO_ARCH_X86;

    case SYS_EMU_TARGET_PPC:
    case SYS_EMU_TARGET_PPC64:
        return CPU_INFO_ARCH_PPC;

    case SYS_EMU_TARGET_SPARC:
    case SYS_EMU_TARGET_SPARC64:
        return CPU_INFO_ARCH_SPARC;

    case SYS_EMU_TARGET_MIPS:
    case SYS_EMU_TARGET_MIPSEL:
    case SYS_EMU_TARGET_MIPS64:
    case SYS_EMU_TARGET_MIPS64EL:
        return CPU_INFO_ARCH_MIPS;

    case SYS_EMU_TARGET_TRICORE:
        return CPU_INFO_ARCH_TRICORE;

    case SYS_EMU_TARGET_S390X:
        return CPU_INFO_ARCH_S390;

    case SYS_EMU_TARGET_RISCV32:
    case SYS_EMU_TARGET_RISCV64:
        return CPU_INFO_ARCH_RISCV;

    default:
        return CPU_INFO_ARCH_OTHER;
    }
}

static void cpustate_to_cpuinfo_s390(CpuInfoS390 *info, const CPUState *cpu)
{
#ifdef TARGET_S390X
    S390CPU *s390_cpu = S390_CPU(cpu);
    CPUS390XState *env = &s390_cpu->env;

    info->cpu_state = env->cpu_state;
#else
    abort();
#endif
}

/*
 * fast means: we NEVER interrupt vCPU threads to retrieve
 * information from KVM.
 */
CpuInfoFastList *qmp_query_cpus_fast(Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    CpuInfoFastList *head = NULL, *cur_item = NULL;
    SysEmuTarget target = qapi_enum_parse(&SysEmuTarget_lookup, TARGET_NAME,
                                          -1, &error_abort);
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CpuInfoFastList *info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));

        info->value->cpu_index = cpu->cpu_index;
        info->value->qom_path = object_get_canonical_path(OBJECT(cpu));
        info->value->thread_id = cpu->thread_id;

        info->value->has_props = !!mc->cpu_index_to_instance_props;
        if (info->value->has_props) {
            CpuInstanceProperties *props;
            props = g_malloc0(sizeof(*props));
            *props = mc->cpu_index_to_instance_props(ms, cpu->cpu_index);
            info->value->props = props;
        }

        info->value->arch = sysemu_target_to_cpuinfo_arch(target);
        info->value->target = target;
        if (target == SYS_EMU_TARGET_S390X) {
            cpustate_to_cpuinfo_s390(&info->value->u.s390x, cpu);
        }

        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
}

MachineInfoList *qmp_query_machines(Error **errp)
{
    GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineInfoList *mach_list = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;
        MachineInfoList *entry;
        MachineInfo *info;

        info = g_malloc0(sizeof(*info));
        if (mc->is_default) {
            info->has_is_default = true;
            info->is_default = true;
        }

        if (mc->alias) {
            info->has_alias = true;
            info->alias = g_strdup(mc->alias);
        }

        info->name = g_strdup(mc->name);
        info->cpu_max = !mc->max_cpus ? 1 : mc->max_cpus;
        info->hotpluggable_cpus = mc->has_hotpluggable_cpus;
        info->numa_mem_supported = mc->numa_mem_supported;
        info->deprecated = !!mc->deprecation_reason;
        if (mc->default_cpu_type) {
            info->default_cpu_type = g_strdup(mc->default_cpu_type);
            info->has_default_cpu_type = true;
        }
        if (mc->default_ram_id) {
            info->default_ram_id = g_strdup(mc->default_ram_id);
            info->has_default_ram_id = true;
        }

        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = mach_list;
        mach_list = entry;
    }

    g_slist_free(machines);
    return mach_list;
}

CurrentMachineParams *qmp_query_current_machine(Error **errp)
{
    CurrentMachineParams *params = g_malloc0(sizeof(*params));
    params->wakeup_suspend_support = qemu_wakeup_suspend_enabled();

    return params;
}

TargetInfo *qmp_query_target(Error **errp)
{
    TargetInfo *info = g_malloc0(sizeof(*info));

    info->arch = qapi_enum_parse(&SysEmuTarget_lookup, TARGET_NAME, -1,
                                 &error_abort);

    return info;
}

HotpluggableCPUList *qmp_query_hotpluggable_cpus(Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    if (!mc->has_hotpluggable_cpus) {
        error_setg(errp, QERR_FEATURE_DISABLED, "query-hotpluggable-cpus");
        return NULL;
    }

    return machine_query_hotpluggable_cpus(ms);
}

void qmp_set_numa_node(NumaOptions *cmd, Error **errp)
{
    if (qdev_hotplug) {
         error_setg(errp, "The command is permitted only before the machine has been created");
         return;
    }

    set_numa_options(MACHINE(qdev_get_machine()), cmd, errp);
}

static int query_memdev(Object *obj, void *opaque)
{
    MemdevList **list = opaque;
    MemdevList *m = NULL;
    QObject *host_nodes;
    Visitor *v;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        m = g_malloc0(sizeof(*m));

        m->value = g_malloc0(sizeof(*m->value));

        m->value->id = g_strdup(object_get_canonical_path_component(obj));
        m->value->has_id = !!m->value->id;

        m->value->size = object_property_get_uint(obj, "size",
                                                  &error_abort);
        m->value->merge = object_property_get_bool(obj, "merge",
                                                   &error_abort);
        m->value->dump = object_property_get_bool(obj, "dump",
                                                  &error_abort);
        m->value->prealloc = object_property_get_bool(obj,
                                                      "prealloc",
                                                      &error_abort);
        m->value->policy = object_property_get_enum(obj,
                                                    "policy",
                                                    "HostMemPolicy",
                                                    &error_abort);
        host_nodes = object_property_get_qobject(obj,
                                                 "host-nodes",
                                                 &error_abort);
        v = qobject_input_visitor_new(host_nodes);
        visit_type_uint16List(v, NULL, &m->value->host_nodes, &error_abort);
        visit_free(v);
        qobject_unref(host_nodes);

        m->next = *list;
        *list = m;
    }

    return 0;
}

MemdevList *qmp_query_memdev(Error **errp)
{
    Object *obj = object_get_objects_root();
    MemdevList *list = NULL;

    object_child_foreach(obj, query_memdev, &list);
    return list;
}
