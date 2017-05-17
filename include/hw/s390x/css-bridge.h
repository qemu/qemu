/*
 * virtual css bridge definition
 *
 * Copyright 2012,2016 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_CSS_BRIDGE_H
#define HW_S390X_CSS_BRIDGE_H
#include "qom/object.h"
#include "hw/qdev-core.h"

/* virtual css bridge */
typedef struct VirtualCssBridge {
    SysBusDevice sysbus_dev;
    bool css_dev_path;
} VirtualCssBridge;

#define TYPE_VIRTUAL_CSS_BRIDGE "virtual-css-bridge"
#define VIRTUAL_CSS_BRIDGE(obj) \
    OBJECT_CHECK(VirtualCssBridge, (obj), TYPE_VIRTUAL_CSS_BRIDGE)

/* virtual css bus type */
typedef struct VirtualCssBus {
    BusState parent_obj;
    bool squash_mcss;
} VirtualCssBus;

#define TYPE_VIRTUAL_CSS_BUS "virtual-css-bus"
#define VIRTUAL_CSS_BUS(obj) \
     OBJECT_CHECK(VirtualCssBus, (obj), TYPE_VIRTUAL_CSS_BUS)
VirtualCssBus *virtual_css_bus_init(void);

#endif
