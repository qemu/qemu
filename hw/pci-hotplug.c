/*
 * QEMU PCI hotplug support
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw.h"
#include "boards.h"
#include "pci.h"
#include "net.h"
#include "sysemu.h"
#include "pc.h"
#include "monitor.h"
#include "block_int.h"
#include "virtio-blk.h"

#if defined(TARGET_I386) || defined(TARGET_X86_64)
static PCIDevice *qemu_pci_hot_add_nic(PCIBus *pci_bus, const char *opts)
{
    int ret;

    ret = net_client_init ("nic", opts);
    if (ret < 0 || !nd_table[ret].model)
        return NULL;
    return pci_nic_init (pci_bus, &nd_table[ret], -1, "rtl8139");
}

void drive_hot_add(Monitor *mon, const char *pci_addr, const char *opts)
{
    int dom, pci_bus;
    unsigned slot;
    int drive_idx, type, bus;
    int success = 0;
    PCIDevice *dev;

    if (pci_read_devaddr(pci_addr, &dom, &pci_bus, &slot)) {
        monitor_printf(mon, "Invalid pci address\n");
        return;
    }

    dev = pci_find_device(pci_bus, slot, 0);
    if (!dev) {
        monitor_printf(mon, "no pci device with address %s\n", pci_addr);
        return;
    }

    drive_idx = add_init_drive(opts);
    if (drive_idx < 0)
        return;
    type = drives_table[drive_idx].type;
    bus = drive_get_max_bus (type);

    switch (type) {
    case IF_SCSI:
        success = 1;
        lsi_scsi_attach (dev, drives_table[drive_idx].bdrv,
                         drives_table[drive_idx].unit);
        break;
    default:
        monitor_printf(mon, "Can't hot-add drive to type %d\n", type);
    }

    if (success)
        monitor_printf(mon, "OK bus %d, unit %d\n",
                       drives_table[drive_idx].bus,
                       drives_table[drive_idx].unit);
    return;
}

static PCIDevice *qemu_pci_hot_add_storage(Monitor *mon, PCIBus *pci_bus,
                                           const char *opts)
{
    void *opaque = NULL;
    int type = -1, drive_idx = -1;
    char buf[128];

    if (get_param_value(buf, sizeof(buf), "if", opts)) {
        if (!strcmp(buf, "scsi"))
            type = IF_SCSI;
        else if (!strcmp(buf, "virtio")) {
            type = IF_VIRTIO;
        } else {
            monitor_printf(mon, "type %s not a hotpluggable PCI device.\n", buf);
            goto out;
        }
    } else {
        monitor_printf(mon, "no if= specified\n");
        goto out;
    }

    if (get_param_value(buf, sizeof(buf), "file", opts)) {
        drive_idx = add_init_drive(opts);
        if (drive_idx < 0)
            goto out;
    } else if (type == IF_VIRTIO) {
        monitor_printf(mon, "virtio requires a backing file/device.\n");
        goto out;
    }

    switch (type) {
    case IF_SCSI:
        opaque = lsi_scsi_init (pci_bus, -1);
        if (opaque && drive_idx >= 0)
            lsi_scsi_attach (opaque, drives_table[drive_idx].bdrv,
                             drives_table[drive_idx].unit);
        break;
    case IF_VIRTIO:
        opaque = virtio_blk_init (pci_bus, drives_table[drive_idx].bdrv);
        break;
    }

out:
    return opaque;
}

void pci_device_hot_add(Monitor *mon, const char *pci_addr, const char *type,
                        const char *opts)
{
    PCIDevice *dev = NULL;
    PCIBus *pci_bus;
    int dom, bus;
    unsigned slot;

    if (pci_assign_devaddr(pci_addr, &dom, &bus, &slot)) {
        monitor_printf(mon, "Invalid pci address\n");
        return;
    }

    pci_bus = pci_find_bus(bus);
    if (!pci_bus) {
        monitor_printf(mon, "Can't find pci_bus %d\n", bus);
        return;
    }

    if (strcmp(type, "nic") == 0)
        dev = qemu_pci_hot_add_nic(pci_bus, opts);
    else if (strcmp(type, "storage") == 0)
        dev = qemu_pci_hot_add_storage(mon, pci_bus, opts);
    else
        monitor_printf(mon, "invalid type: %s\n", type);

    if (dev) {
        qemu_system_device_hot_add(bus, PCI_SLOT(dev->devfn), 1);
        monitor_printf(mon, "OK domain %d, bus %d, slot %d, function %d\n",
                       0, pci_bus_num(dev->bus), PCI_SLOT(dev->devfn),
                       PCI_FUNC(dev->devfn));
    } else
        monitor_printf(mon, "failed to add %s\n", opts);
}
#endif

void pci_device_hot_remove(Monitor *mon, const char *pci_addr)
{
    PCIDevice *d;
    int dom, bus;
    unsigned slot;

    if (pci_read_devaddr(pci_addr, &dom, &bus, &slot)) {
        monitor_printf(mon, "Invalid pci address\n");
        return;
    }

    d = pci_find_device(bus, slot, 0);
    if (!d) {
        monitor_printf(mon, "slot %d empty\n", slot);
        return;
    }

    qemu_system_device_hot_add(bus, slot, 0);
}

static int pci_match_fn(void *dev_private, void *arg)
{
    PCIDevice *dev = dev_private;
    PCIDevice *match = arg;

    return (dev == match);
}

/*
 * OS has executed _EJ0 method, we now can remove the device
 */
void pci_device_hot_remove_success(int pcibus, int slot)
{
    PCIDevice *d = pci_find_device(pcibus, slot, 0);
    int class_code;

    if (!d) {
        monitor_printf(cur_mon, "invalid slot %d\n", slot);
        return;
    }

    class_code = d->config_read(d, PCI_CLASS_DEVICE+1, 1);

    switch(class_code) {
    case PCI_BASE_CLASS_STORAGE:
        destroy_bdrvs(pci_match_fn, d);
        break;
    case PCI_BASE_CLASS_NETWORK:
        destroy_nic(pci_match_fn, d);
        break;
    }

    pci_unregister_device(d);
}

