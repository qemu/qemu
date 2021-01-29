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
#include "hw/remote/mpqemu-link.h"
#include "qemu/error-report.h"

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

static void config_op_send(PCIProxyDev *pdev, uint32_t addr, uint32_t *val,
                           int len, unsigned int op)
{
    MPQemuMsg msg = { 0 };
    uint64_t ret = -EINVAL;
    Error *local_err = NULL;

    msg.cmd = op;
    msg.data.pci_conf_data.addr = addr;
    msg.data.pci_conf_data.val = (op == MPQEMU_CMD_PCI_CFGWRITE) ? *val : 0;
    msg.data.pci_conf_data.len = len;
    msg.size = sizeof(PciConfDataMsg);

    ret = mpqemu_msg_send_and_await_reply(&msg, pdev, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    if (ret == UINT64_MAX) {
        error_report("Failed to perform PCI config %s operation",
                     (op == MPQEMU_CMD_PCI_CFGREAD) ? "READ" : "WRITE");
    }

    if (op == MPQEMU_CMD_PCI_CFGREAD) {
        *val = (uint32_t)ret;
    }
}

static uint32_t pci_proxy_read_config(PCIDevice *d, uint32_t addr, int len)
{
    uint32_t val;

    config_op_send(PCI_PROXY_DEV(d), addr, &val, len, MPQEMU_CMD_PCI_CFGREAD);

    return val;
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                   int len)
{
    /*
     * Some of the functions access the copy of remote device's PCI config
     * space which is cached in the proxy device. Therefore, maintain
     * it updated.
     */
    pci_default_write_config(d, addr, val, len);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, len, MPQEMU_CMD_PCI_CFGWRITE);
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
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;

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

static void send_bar_access_msg(PCIProxyDev *pdev, MemoryRegion *mr,
                                bool write, hwaddr addr, uint64_t *val,
                                unsigned size, bool memory)
{
    MPQemuMsg msg = { 0 };
    long ret = -EINVAL;
    Error *local_err = NULL;

    msg.size = sizeof(BarAccessMsg);
    msg.data.bar_access.addr = mr->addr + addr;
    msg.data.bar_access.size = size;
    msg.data.bar_access.memory = memory;

    if (write) {
        msg.cmd = MPQEMU_CMD_BAR_WRITE;
        msg.data.bar_access.val = *val;
    } else {
        msg.cmd = MPQEMU_CMD_BAR_READ;
    }

    ret = mpqemu_msg_send_and_await_reply(&msg, pdev, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    if (!write) {
        *val = ret;
    }
}

static void proxy_bar_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;

    send_bar_access_msg(pmr->dev, &pmr->mr, true, addr, &val, size,
                        pmr->memory);
}

static uint64_t proxy_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;
    uint64_t val;

    send_bar_access_msg(pmr->dev, &pmr->mr, false, addr, &val, size,
                        pmr->memory);

    return val;
}

const MemoryRegionOps proxy_mr_ops = {
    .read = proxy_bar_read,
    .write = proxy_bar_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};
