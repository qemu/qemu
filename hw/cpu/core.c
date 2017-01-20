/*
 * CPU core abstract device
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "hw/cpu/core.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "sysemu/cpus.h"

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
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    core->nr_threads = value;
}

static void cpu_core_instance_init(Object *obj)
{
    CPUCore *core = CPU_CORE(obj);

    object_property_add(obj, "core-id", "int", core_prop_get_core_id,
                        core_prop_set_core_id, NULL, NULL, NULL);
    object_property_add(obj, "nr-threads", "int", core_prop_get_nr_threads,
                        core_prop_set_nr_threads, NULL, NULL, NULL);
    core->nr_threads = smp_threads;
}

static void cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
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
