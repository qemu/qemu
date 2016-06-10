/*
 * sPAPR CPU core device.
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_SPAPR_CPU_CORE_H
#define HW_SPAPR_CPU_CORE_H

#include "hw/qdev.h"
#include "hw/cpu/core.h"
#include "target-ppc/cpu-qom.h"

#define TYPE_SPAPR_CPU_CORE "spapr-cpu-core"
#define SPAPR_CPU_CORE(obj) \
    OBJECT_CHECK(sPAPRCPUCore, (obj), TYPE_SPAPR_CPU_CORE)

typedef struct sPAPRCPUCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    void *threads;
    ObjectClass *cpu_class;
} sPAPRCPUCore;

void spapr_core_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                         Error **errp);
char *spapr_get_cpu_core_type(const char *model);
void spapr_core_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                     Error **errp);
void spapr_core_unplug(HotplugHandler *hotplug_dev, DeviceState *dev,
                       Error **errp);
#endif
