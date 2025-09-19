/*
 * Copyright © 2020, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qom/object_interfaces.h"
#include "io/channel.h"
#include "hw/qdev-core.h"
#include "hw/remote/machine.h"
#include "io/channel-util.h"
#include "qapi/error.h"
#include "system/system.h"
#include "hw/pci/pci.h"
#include "qemu/sockets.h"
#include "monitor/monitor.h"

#define TYPE_REMOTE_OBJECT "x-remote-object"
OBJECT_DECLARE_TYPE(RemoteObject, RemoteObjectClass, REMOTE_OBJECT)

struct RemoteObjectClass {
    ObjectClass parent_class;

    unsigned int nr_devs;
    unsigned int max_devs;
};

struct RemoteObject {
    /* private */
    Object parent;

    Notifier machine_done;

    int32_t fd;
    char *devid;

    QIOChannel *ioc;

    DeviceState *dev;
    DeviceListener listener;
};

static void remote_object_set_fd(Object *obj, const char *str, Error **errp)
{
    ERRP_GUARD();
    RemoteObject *o = REMOTE_OBJECT(obj);
    int fd = -1;

    fd = monitor_fd_param(monitor_cur(), str, errp);
    if (fd == -1) {
        error_prepend(errp, "Could not parse remote object fd %s:", str);
        return;
    }

    if (!fd_is_socket(fd)) {
        error_setg(errp, "File descriptor '%s' is not a socket", str);
        close(fd);
        return;
    }

    o->fd = fd;
}

static void remote_object_set_devid(Object *obj, const char *str, Error **errp)
{
    RemoteObject *o = REMOTE_OBJECT(obj);

    g_free(o->devid);

    o->devid = g_strdup(str);
}

static void remote_object_unrealize_listener(DeviceListener *listener,
                                             DeviceState *dev)
{
    RemoteObject *o = container_of(listener, RemoteObject, listener);

    if (o->dev == dev) {
        object_unref(OBJECT(o));
    }
}

static void remote_object_machine_done(Notifier *notifier, void *data)
{
    RemoteObject *o = container_of(notifier, RemoteObject, machine_done);
    DeviceState *dev = NULL;
    QIOChannel *ioc = NULL;
    Coroutine *co = NULL;
    RemoteCommDev *comdev = NULL;
    Error *err = NULL;

    dev = qdev_find_recursive(sysbus_get_default(), o->devid);
    if (!dev || !object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_report("%s is not a PCI device", o->devid);
        return;
    }

    ioc = qio_channel_new_fd(o->fd, &err);
    if (!ioc) {
        error_report_err(err);
        return;
    }
    if (!qio_channel_set_blocking(ioc, false, &err)) {
        error_report_err(err);
        object_unref(OBJECT(ioc));
        return;
    }

    o->dev = dev;

    o->listener.unrealize = remote_object_unrealize_listener;
    device_listener_register(&o->listener);

    /* co-routine should free this. */
    comdev = g_new0(RemoteCommDev, 1);
    *comdev = (RemoteCommDev) {
        .ioc = ioc,
        .dev = PCI_DEVICE(dev),
    };

    co = qemu_coroutine_create(mpqemu_remote_msg_loop_co, comdev);
    qemu_coroutine_enter(co);
}

static void remote_object_init(Object *obj)
{
    RemoteObjectClass *k = REMOTE_OBJECT_GET_CLASS(obj);
    RemoteObject *o = REMOTE_OBJECT(obj);

    if (k->nr_devs >= k->max_devs) {
        error_report("Reached maximum number of devices: %u", k->max_devs);
        return;
    }

    o->ioc = NULL;
    o->fd = -1;
    o->devid = NULL;

    k->nr_devs++;

    o->machine_done.notify = remote_object_machine_done;
    qemu_add_machine_init_done_notifier(&o->machine_done);
}

static void remote_object_finalize(Object *obj)
{
    RemoteObjectClass *k = REMOTE_OBJECT_GET_CLASS(obj);
    RemoteObject *o = REMOTE_OBJECT(obj);

    device_listener_unregister(&o->listener);

    if (o->ioc) {
        qio_channel_shutdown(o->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        qio_channel_close(o->ioc, NULL);
    }

    object_unref(OBJECT(o->ioc));

    k->nr_devs--;
    g_free(o->devid);
}

static void remote_object_class_init(ObjectClass *klass, const void *data)
{
    RemoteObjectClass *k = REMOTE_OBJECT_CLASS(klass);

    /*
     * Limit number of supported devices to 1. This is done to avoid devices
     * from one VM accessing the RAM of another VM. This is done until we
     * start using separate address spaces for individual devices.
     */
    k->max_devs = 1;
    k->nr_devs = 0;

    object_class_property_add_str(klass, "fd", NULL, remote_object_set_fd);
    object_class_property_add_str(klass, "devid", NULL,
                                  remote_object_set_devid);
}

static const TypeInfo remote_object_info = {
    .name = TYPE_REMOTE_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RemoteObject),
    .instance_init = remote_object_init,
    .instance_finalize = remote_object_finalize,
    .class_size = sizeof(RemoteObjectClass),
    .class_init = remote_object_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&remote_object_info);
}

type_init(register_types);
