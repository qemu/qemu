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
#include "qemu/notify.h"

typedef void (*XenWatchHandler)(void *opaque);

typedef struct XenWatch XenWatch;
typedef struct XenEventChannel XenEventChannel;

typedef struct XenDevice {
    DeviceState qdev;
    domid_t frontend_id;
    char *name;
    char *backend_path, *frontend_path;
    enum xenbus_state backend_state, frontend_state;
    Notifier exit;
    XenWatch *backend_state_watch, *frontend_state_watch;
    bool backend_online;
    XenWatch *backend_online_watch;
    xengnttab_handle *xgth;
    bool feature_grant_copy;
    QLIST_HEAD(, XenEventChannel) event_channels;
} XenDevice;

typedef char *(*XenDeviceGetName)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceRealize)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceFrontendChanged)(XenDevice *xendev,
                                         enum xenbus_state frontend_state,
                                         Error **errp);
typedef void (*XenDeviceUnrealize)(XenDevice *xendev, Error **errp);

typedef struct XenDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *backend;
    const char *device;
    XenDeviceGetName get_name;
    XenDeviceRealize realize;
    XenDeviceFrontendChanged frontend_changed;
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
    NotifierList watch_notifiers;
    XenWatch *backend_watch;
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

void xen_device_backend_set_state(XenDevice *xendev,
                                  enum xenbus_state state);
enum xenbus_state xen_device_backend_get_state(XenDevice *xendev);

void xen_device_backend_printf(XenDevice *xendev, const char *key,
                               const char *fmt, ...)
    GCC_FMT_ATTR(3, 4);
void xen_device_frontend_printf(XenDevice *xendev, const char *key,
                                const char *fmt, ...)
    GCC_FMT_ATTR(3, 4);

int xen_device_frontend_scanf(XenDevice *xendev, const char *key,
                              const char *fmt, ...);

void xen_device_set_max_grant_refs(XenDevice *xendev, unsigned int nr_refs,
                                   Error **errp);
void *xen_device_map_grant_refs(XenDevice *xendev, uint32_t *refs,
                                unsigned int nr_refs, int prot,
                                Error **errp);
void xen_device_unmap_grant_refs(XenDevice *xendev, void *map,
                                 unsigned int nr_refs, Error **errp);

typedef struct XenDeviceGrantCopySegment {
    union {
        void *virt;
        struct {
            uint32_t ref;
            off_t offset;
        } foreign;
    } source, dest;
    size_t len;
} XenDeviceGrantCopySegment;

void xen_device_copy_grant_refs(XenDevice *xendev, bool to_domain,
                                XenDeviceGrantCopySegment segs[],
                                unsigned int nr_segs, Error **errp);

typedef bool (*XenEventHandler)(void *opaque);

XenEventChannel *xen_device_bind_event_channel(XenDevice *xendev,
                                               AioContext *ctx,
                                               unsigned int port,
                                               XenEventHandler handler,
                                               void *opaque, Error **errp);
void xen_device_notify_event_channel(XenDevice *xendev,
                                     XenEventChannel *channel,
                                     Error **errp);
void xen_device_unbind_event_channel(XenDevice *xendev,
                                     XenEventChannel *channel,
                                     Error **errp);

#endif /* HW_XEN_BUS_H */
