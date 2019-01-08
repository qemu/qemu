/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BUS_H
#define HW_XEN_BUS_H

#include "hw/xen/xen_common.h"
#include "hw/sysbus.h"

typedef struct XenDevice {
    DeviceState qdev;
    domid_t frontend_id;
    char *name;
    char *backend_path, *frontend_path;
    enum xenbus_state backend_state, frontend_state;
    Notifier exit;
} XenDevice;

typedef char *(*XenDeviceGetName)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceRealize)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceUnrealize)(XenDevice *xendev, Error **errp);

typedef struct XenDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *backend;
    const char *device;
    XenDeviceGetName get_name;
    XenDeviceRealize realize;
    XenDeviceUnrealize unrealize;
} XenDeviceClass;

#define TYPE_XEN_DEVICE "xen-device"
#define XEN_DEVICE(obj) \
     OBJECT_CHECK(XenDevice, (obj), TYPE_XEN_DEVICE)
#define XEN_DEVICE_CLASS(class) \
     OBJECT_CLASS_CHECK(XenDeviceClass, (class), TYPE_XEN_DEVICE)
#define XEN_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XenDeviceClass, (obj), TYPE_XEN_DEVICE)

typedef struct XenBus {
    BusState qbus;
    domid_t backend_id;
    struct xs_handle *xsh;
} XenBus;

typedef struct XenBusClass {
    /*< private >*/
    BusClass parent_class;
} XenBusClass;

#define TYPE_XEN_BUS "xen-bus"
#define XEN_BUS(obj) \
    OBJECT_CHECK(XenBus, (obj), TYPE_XEN_BUS)
#define XEN_BUS_CLASS(class) \
    OBJECT_CLASS_CHECK(XenBusClass, (class), TYPE_XEN_BUS)
#define XEN_BUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(XenBusClass, (obj), TYPE_XEN_BUS)

void xen_bus_init(void);

#endif /* HW_XEN_BUS_H */
