/*
 * CPU core abstract device
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/cpu/core.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "sysemu/cpus.h"
#include "hw/boards.h"

static void core_prop_get_core_id(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    CPUCore *core = CPU_CORE(obj);
    int64_t value = core->core_id;

    visit_type_int(v, name, &value, errp);
}

static void core_prop_set_core_id(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    CPUCore *core = CPU_CORE(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    if (value < 0) {
        error_setg(errp, "Invalid core id %"PRId64, value);
        return;
    }

    core->core_id = value;
}

static void core_prop_get_nr_threads(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    CPUCore *core = CPU_CORE(obj);
    int64_t value = core->nr_threads;

    visit_type_int(v, name, &value, errp);
}

static void core_prop_set_nr_threads(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    CPUCore *core = CPU_CORE(obj);
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    core->nr_threads = value;
}

static void cpu_core_instance_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    CPUCore *core = CPU_CORE(obj);

    core->nr_threads = ms->smp.threads;
}

static void cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
    object_class_property_add(oc, "core-id", "int", core_prop_get_core_id,
                              core_prop_set_core_id, NULL, NULL);
    object_class_property_add(oc, "nr-threads", "int", core_prop_get_nr_threads,
                              core_prop_set_nr_threads, NULL, NULL);
}

static const TypeInfo cpu_core_type_info = {
    .name = TYPE_CPU_CORE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .class_init = cpu_core_class_init,
    .instance_size = sizeof(CPUCore),
    .instance_init = cpu_core_instance_init,
};

static void cpu_core_register_types(void)
{
    type_register_static(&cpu_core_type_info);
}

type_init(cpu_core_register_types)
