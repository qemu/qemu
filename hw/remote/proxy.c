/*
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/proxy.h"
#include "hw/pci/pci.h"
#include "qapi/error.h"
#include "io/channel-util.h"
#include "hw/qdev-properties.h"
#include "monitor/monitor.h"
#include "migration/blocker.h"
#include "qemu/sockets.h"

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    ERRP_GUARD();
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    int fd;

    if (!dev->fd) {
        error_setg(errp, "fd parameter not specified for %s",
                   DEVICE(device)->id);
        return;
    }

    fd = monitor_fd_param(monitor_cur(), dev->fd, errp);
    if (fd == -1) {
        error_prepend(errp, "proxy: unable to parse fd %s: ", dev->fd);
        return;
    }

    if (!fd_is_socket(fd)) {
        error_setg(errp, "proxy: fd %d is not a socket", fd);
        close(fd);
        return;
    }

    dev->ioc = qio_channel_new_fd(fd, errp);

    error_setg(&dev->migration_blocker, "%s does not support migration",
               TYPE_PCI_PROXY_DEV);
    migrate_add_blocker(dev->migration_blocker, errp);

    qemu_mutex_init(&dev->io_mutex);
    qio_channel_set_blocking(dev->ioc, true, NULL);
}

static void pci_proxy_dev_exit(PCIDevice *pdev)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(pdev);

    if (dev->ioc) {
        qio_channel_close(dev->ioc, NULL);
    }

    migrate_del_blocker(dev->migration_blocker);

    error_free(dev->migration_blocker);
}

static Property proxy_properties[] = {
    DEFINE_PROP_STRING("fd", PCIProxyDev, fd),
    DEFINE_PROP_END_OF_LIST(),
};

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->exit = pci_proxy_dev_exit;
    device_class_set_props(dc, proxy_properties);
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
    .class_init    = pci_proxy_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_proxy_dev_register_types(void)
{
    type_register_static(&pci_proxy_dev_type_info);
}

type_init(pci_proxy_dev_register_types)
