/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BUS_H
#define HW_XEN_BUS_H

#include "hw/qdev-core.h"
#include "hw/xen/xen_backend_ops.h"
#include "qemu/notify.h"
#include "qemu/queue.h"
#include "qom/object.h"

typedef struct XenEventChannel XenEventChannel;

struct XenDevice {
    DeviceState qdev;
    domid_t frontend_id;
    char *name;
    struct qemu_xs_handle *xsh;
    char *backend_path, *frontend_path;
    enum xenbus_state backend_state, frontend_state;
    Notifier exit;
    struct qemu_xs_watch *backend_state_watch, *frontend_state_watch;
    bool backend_online;
    struct qemu_xs_watch *backend_online_watch;
    xengnttab_handle *xgth;
    bool inactive;
    QLIST_HEAD(, XenEventChannel) event_channels;
    QLIST_ENTRY(XenDevice) list;
};
typedef struct XenDevice XenDevice;

typedef char *(*XenDeviceGetFrontendPath)(XenDevice *xendev, Error **errp);
typedef char *(*XenDeviceGetName)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceRealize)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceFrontendChanged)(XenDevice *xendev,
                                         enum xenbus_state frontend_state,
                                         Error **errp);
typedef void (*XenDeviceUnrealize)(XenDevice *xendev);

struct XenDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *backend;
    const char *device;
    XenDeviceGetFrontendPath get_frontend_path;
    XenDeviceGetName get_name;
    XenDeviceRealize realize;
    XenDeviceFrontendChanged frontend_changed;
    XenDeviceUnrealize unrealize;
};

#define TYPE_XEN_DEVICE "xen-device"
OBJECT_DECLARE_TYPE(XenDevice, XenDeviceClass, XEN_DEVICE)

struct XenBus {
    BusState qbus;
    domid_t backend_id;
    struct qemu_xs_handle *xsh;
    unsigned int backend_types;
    struct qemu_xs_watch **backend_watch;
    QLIST_HEAD(, XenDevice) inactive_devices;
};

struct XenBusClass {
    /*< private >*/
    BusClass parent_class;
};

#define TYPE_XEN_BUS "xen-bus"
OBJECT_DECLARE_TYPE(XenBus, XenBusClass,
                    XEN_BUS)

void xen_bus_init(void);

void xen_device_backend_set_state(XenDevice *xendev,
                                  enum xenbus_state state);
enum xenbus_state xen_device_backend_get_state(XenDevice *xendev);

void xen_device_backend_printf(XenDevice *xendev, const char *key,
                               const char *fmt, ...)
    G_GNUC_PRINTF(3, 4);
void xen_device_frontend_printf(XenDevice *xendev, const char *key,
                                const char *fmt, ...)
    G_GNUC_PRINTF(3, 4);

int xen_device_frontend_scanf(XenDevice *xendev, const char *key,
                              const char *fmt, ...)
    G_GNUC_SCANF(3, 4);
char *xen_device_frontend_read(XenDevice *xendev, const char *key);

void xen_device_set_max_grant_refs(XenDevice *xendev, unsigned int nr_refs,
                                   Error **errp);
void *xen_device_map_grant_refs(XenDevice *xendev, uint32_t *refs,
                                unsigned int nr_refs, int prot,
                                Error **errp);
void xen_device_unmap_grant_refs(XenDevice *xendev, void *map, uint32_t *refs,
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
                                               unsigned int port,
                                               XenEventHandler handler,
                                               void *opaque, Error **errp);
void xen_device_set_event_channel_context(XenDevice *xendev,
                                          XenEventChannel *channel,
                                          AioContext *ctx,
                                          Error **errp);
void xen_device_notify_event_channel(XenDevice *xendev,
                                     XenEventChannel *channel,
                                     Error **errp);
void xen_device_unbind_event_channel(XenDevice *xendev,
                                     XenEventChannel *channel,
                                     Error **errp);
unsigned int xen_event_channel_get_local_port(XenEventChannel *channel);

#endif /* HW_XEN_BUS_H */
