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

#include "qemu/osdep.h"
#include "hw/qdev.h"

#define TYPE_CPU_CORE "cpu-core"

#define CPU_CORE(obj) \
    OBJECT_CHECK(CPUCore, (obj), TYPE_CPU_CORE)

typedef struct CPUCore {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    int core_id;
    int nr_threads;
} CPUCore;

#define CPU_CORE_PROP_CORE_ID "core-id"

#endif
