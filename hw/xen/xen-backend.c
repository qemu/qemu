/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/xen/xen-backend.h"
#include "hw/xen/xen-bus.h"

typedef struct XenBackendImpl {
    const char *type;
    XenBackendDeviceCreate create;
    XenBackendDeviceDestroy destroy;
} XenBackendImpl;

struct XenBackendInstance {
    QLIST_ENTRY(XenBackendInstance) entry;
    const XenBackendImpl *impl;
    XenBus *xenbus;
    char *name;
    XenDevice *xendev;
};

static GHashTable *xen_backend_table_get(void)
{
    static GHashTable *table;

    if (table == NULL) {
        table = g_hash_table_new(g_str_hash, g_str_equal);
    }

    return table;
}

static void xen_backend_table_add(XenBackendImpl *impl)
{
    g_hash_table_insert(xen_backend_table_get(), (void *)impl->type, impl);
}

static const char **xen_backend_table_keys(unsigned int *count)
{
    return (const char **)g_hash_table_get_keys_as_array(
        xen_backend_table_get(), count);
}

static const XenBackendImpl *xen_backend_table_lookup(const char *type)
{
    return g_hash_table_lookup(xen_backend_table_get(), type);
}

void xen_backend_register(const XenBackendInfo *info)
{
    XenBackendImpl *impl = g_new0(XenBackendImpl, 1);

    g_assert(info->type);

    if (xen_backend_table_lookup(info->type)) {
        error_report("attempt to register duplicate Xen backend type '%s'",
                     info->type);
        abort();
    }

    if (!info->create) {
        error_report("backend type '%s' has no creator", info->type);
        abort();
    }

    impl->type = info->type;
    impl->create = info->create;
    impl->destroy = info->destroy;

    xen_backend_table_add(impl);
}

const char **xen_backend_get_types(unsigned int *count)
{
    return xen_backend_table_keys(count);
}

static QLIST_HEAD(, XenBackendInstance) backend_list;

static void xen_backend_list_add(XenBackendInstance *backend)
{
    QLIST_INSERT_HEAD(&backend_list, backend, entry);
}

static XenBackendInstance *xen_backend_list_find(XenDevice *xendev)
{
    XenBackendInstance *backend;

    QLIST_FOREACH(backend, &backend_list, entry) {
        if (backend->xendev == xendev) {
            return backend;
        }
    }

    return NULL;
}

static void xen_backend_list_remove(XenBackendInstance *backend)
{
    QLIST_REMOVE(backend, entry);
}

void xen_backend_device_create(XenBus *xenbus, const char *type,
                               const char *name, QDict *opts, Error **errp)
{
    ERRP_GUARD();
    const XenBackendImpl *impl = xen_backend_table_lookup(type);
    XenBackendInstance *backend;

    if (!impl) {
        return;
    }

    backend = g_new0(XenBackendInstance, 1);
    backend->xenbus = xenbus;
    backend->name = g_strdup(name);

    impl->create(backend, opts, errp);
    if (*errp) {
        g_free(backend->name);
        g_free(backend);
        return;
    }

    backend->impl = impl;
    xen_backend_list_add(backend);
}

XenBus *xen_backend_get_bus(XenBackendInstance *backend)
{
    return backend->xenbus;
}

const char *xen_backend_get_name(XenBackendInstance *backend)
{
    return backend->name;
}

void xen_backend_set_device(XenBackendInstance *backend,
                            XenDevice *xendev)
{
    g_assert(!backend->xendev);
    backend->xendev = xendev;
}

XenDevice *xen_backend_get_device(XenBackendInstance *backend)
{
    return backend->xendev;
}


bool xen_backend_try_device_destroy(XenDevice *xendev, Error **errp)
{
    XenBackendInstance *backend = xen_backend_list_find(xendev);
    const XenBackendImpl *impl;

    if (!backend) {
        return false;
    }

    impl = backend->impl;
    impl->destroy(backend, errp);

    xen_backend_list_remove(backend);
    g_free(backend->name);
    g_free(backend);

    return true;
}
