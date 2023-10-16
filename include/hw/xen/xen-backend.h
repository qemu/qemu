/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BACKEND_H
#define HW_XEN_BACKEND_H

#include "hw/xen/xen-bus.h"

typedef struct XenBackendInstance XenBackendInstance;

typedef void (*XenBackendDeviceCreate)(XenBackendInstance *backend,
                                       QDict *opts, Error **errp);
typedef void (*XenBackendDeviceDestroy)(XenBackendInstance *backend,
                                        Error **errp);

typedef struct XenBackendInfo {
    const char *type;
    XenBackendDeviceCreate create;
    XenBackendDeviceDestroy destroy;
} XenBackendInfo;

XenBus *xen_backend_get_bus(XenBackendInstance *backend);
const char *xen_backend_get_name(XenBackendInstance *backend);

void xen_backend_set_device(XenBackendInstance *backend,
                            XenDevice *xendevice);
XenDevice *xen_backend_get_device(XenBackendInstance *backend);

void xen_backend_register(const XenBackendInfo *info);
const char **xen_backend_get_types(unsigned int *nr);

bool xen_backend_exists(const char *type, const char *name);
void xen_backend_device_create(XenBus *xenbus, const char *type,
                               const char *name, QDict *opts, Error **errp);
bool xen_backend_try_device_destroy(XenDevice *xendev, Error **errp);

#endif /* HW_XEN_BACKEND_H */
