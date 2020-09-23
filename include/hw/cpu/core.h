/*
 * CPU core abstract device
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_CPU_CORE_H
#define HW_CPU_CORE_H

#include "hw/qdev-core.h"
#include "qom/object.h"

#define TYPE_CPU_CORE "cpu-core"

OBJECT_DECLARE_SIMPLE_TYPE(CPUCore, CPU_CORE)

struct CPUCore {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    int core_id;
    int nr_threads;
};

/* Note: topology field names need to be kept in sync with
 * 'CpuInstanceProperties' */

#define CPU_CORE_PROP_CORE_ID "core-id"

#endif
