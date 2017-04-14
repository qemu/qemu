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
#include "target/ppc/cpu-qom.h"

#define TYPE_SPAPR_CPU_CORE "spapr-cpu-core"
#define SPAPR_CPU_CORE(obj) \
    OBJECT_CHECK(sPAPRCPUCore, (obj), TYPE_SPAPR_CPU_CORE)
#define SPAPR_CPU_CORE_CLASS(klass) \
    OBJECT_CLASS_CHECK(sPAPRCPUCoreClass, (klass), TYPE_SPAPR_CPU_CORE)
#define SPAPR_CPU_CORE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(sPAPRCPUCoreClass, (obj), TYPE_SPAPR_CPU_CORE)

typedef struct sPAPRCPUCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    void *threads;
} sPAPRCPUCore;

typedef struct sPAPRCPUCoreClass {
    DeviceClass parent_class;
    ObjectClass *cpu_class;
} sPAPRCPUCoreClass;

char *spapr_get_cpu_core_type(const char *model);
void spapr_cpu_core_class_init(ObjectClass *oc, void *data);
#endif
