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
#include "hw/sysbus.h"

/* virtual css bridge */
struct VirtualCssBridge {
    SysBusDevice sysbus_dev;
    bool css_dev_path;
};
typedef struct VirtualCssBridge VirtualCssBridge;

#define TYPE_VIRTUAL_CSS_BRIDGE "virtual-css-bridge"
DECLARE_INSTANCE_CHECKER(VirtualCssBridge, VIRTUAL_CSS_BRIDGE,
                         TYPE_VIRTUAL_CSS_BRIDGE)

/* virtual css bus type */
struct VirtualCssBus {
    BusState parent_obj;
};
typedef struct VirtualCssBus VirtualCssBus;

#define TYPE_VIRTUAL_CSS_BUS "virtual-css-bus"
DECLARE_INSTANCE_CHECKER(VirtualCssBus, VIRTUAL_CSS_BUS,
                         TYPE_VIRTUAL_CSS_BUS)
VirtualCssBus *virtual_css_bus_init(void);

#endif
