/*
 * Deprecated PCI hotplug interface support
 * This covers the old pci_add / pci_del command, whereas the more general
 * device_add / device_del commands are now preferred.
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

#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/pci/pci.h"
#include "net/net.h"
#include "hw/i386/pc.h"
#include "monitor/monitor.h"
#include "hw/scsi/scsi.h"
#include "hw/virtio/virtio-blk.h"
#include "qemu/config-file.h"
#include "sysemu/blockdev.h"
#include "qapi/error.h"

static int pci_read_devaddr(Monitor *mon, const char *addr,
                            int *busp, unsigned *slotp)
{
    int dom;

    /* strip legacy tag */
    if (!strncmp(addr, "pci_addr=", 9)) {
        addr += 9;
    }
    if (pci_parse_devaddr(addr, &dom, busp, slotp, NULL)) {
        monitor_printf(mon, "Invalid pci address\n");
        return -1;
    }
    if (dom != 0) {
        monitor_printf(mon, "Multiple PCI domains not supported, use device_add\n");
        return -1;
    }
    return 0;
}

static PCIDevice *qemu_pci_hot_add_nic(Monitor *mon,
                                       const char *devaddr,
                                       const char *opts_str)
{
    Error *local_err = NULL;
    QemuOpts *opts;
    PCIBus *root = pci_find_primary_bus();
    PCIBus *bus;
    int ret, devfn;

    if (!root) {
        monitor_printf(mon, "no primary PCI bus (if there are multiple"
                       " PCI roots, you must use device_add instead)");
        return NULL;
    }

    bus = pci_get_bus_devfn(&devfn, root, devaddr);
    if (!bus) {
        monitor_printf(mon, "Invalid PCI device address %s\n", devaddr);
        return NULL;
    }
    if (!((BusState*)bus)->allow_hotplug) {
        monitor_printf(mon, "PCI bus doesn't support hotplug\n");
        return NULL;
    }

    opts = qemu_opts_parse(qemu_find_opts("net"), opts_str ? opts_str : "", 0);
    if (!opts) {
        return NULL;
    }

    qemu_opt_set(opts, "type", "nic");

    ret = net_client_init(opts, 0, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return NULL;
    }
    if (nd_table[ret].devaddr) {
        monitor_printf(mon, "Parameter addr not supported\n");
        return NULL;
    }
    return pci_nic_init(&nd_table[ret], root, "rtl8139", devaddr);
}

static int scsi_hot_add(Monitor *mon, DeviceState *adapter,
                        DriveInfo *dinfo, int printinfo)
{
    SCSIBus *scsibus;
    SCSIDevice *scsidev;

    scsibus = (SCSIBus *)
        object_dynamic_cast(OBJECT(QLIST_FIRST(&adapter->child_bus)),
                            TYPE_SCSI_BUS);
    if (!scsibus) {
	error_report("Device is not a SCSI adapter");
	return -1;
    }

    /*
     * drive_init() tries to find a default for dinfo->unit.  Doesn't
     * work at all for hotplug though as we assign the device to a
     * specific bus instead of the first bus with spare scsi ids.
     *
     * Ditch the calculated value and reload from option string (if
     * specified).
     */
    dinfo->unit = qemu_opt_get_number(dinfo->opts, "unit", -1);
    dinfo->bus = scsibus->busnr;
    scsidev = scsi_bus_legacy_add_drive(scsibus, dinfo->bdrv, dinfo->unit,
                                        false, -1, NULL, NULL);
    if (!scsidev) {
        return -1;
    }
    dinfo->unit = scsidev->id;

    if (printinfo)
        monitor_printf(mon, "OK bus %d, unit %d\n",
                       scsibus->busnr, scsidev->id);
    return 0;
}

int pci_drive_hot_add(Monitor *mon, const QDict *qdict, DriveInfo *dinfo)
{
    int pci_bus;
    unsigned slot;
    PCIBus *root = pci_find_primary_bus();
    PCIDevice *dev;
    const char *pci_addr = qdict_get_str(qdict, "pci_addr");

    switch (dinfo->type) {
    case IF_SCSI:
        if (!root) {
            monitor_printf(mon, "no primary PCI bus (if there are multiple"
                           " PCI roots, you must use device_add instead)");
            goto err;
        }
        if (pci_read_devaddr(mon, pci_addr, &pci_bus, &slot)) {
            goto err;
        }
        dev = pci_find_device(root, pci_bus, PCI_DEVFN(slot, 0));
        if (!dev) {
            monitor_printf(mon, "no pci device with address %s\n", pci_addr);
            goto err;
        }
        if (scsi_hot_add(mon, &dev->qdev, dinfo, 1) != 0) {
            goto err;
        }
        break;
    default:
        monitor_printf(mon, "Can't hot-add drive to type %d\n", dinfo->type);
        goto err;
    }

    return 0;
err:
    return -1;
}

static PCIDevice *qemu_pci_hot_add_storage(Monitor *mon,
                                           const char *devaddr,
                                           const char *opts)
{
    PCIDevice *dev;
    DriveInfo *dinfo = NULL;
    int type = -1;
    char buf[128];
    PCIBus *root = pci_find_primary_bus();
    PCIBus *bus;
    int devfn;

    if (get_param_value(buf, sizeof(buf), "if", opts)) {
        if (!strcmp(buf, "scsi"))
            type = IF_SCSI;
        else if (!strcmp(buf, "virtio")) {
            type = IF_VIRTIO;
        } else {
            monitor_printf(mon, "type %s not a hotpluggable PCI device.\n", buf);
            return NULL;
        }
    } else {
        monitor_printf(mon, "no if= specified\n");
        return NULL;
    }

    if (get_param_value(buf, sizeof(buf), "file", opts)) {
        dinfo = add_init_drive(opts);
        if (!dinfo)
            return NULL;
        if (dinfo->devaddr) {
            monitor_printf(mon, "Parameter addr not supported\n");
            return NULL;
        }
    } else {
        dinfo = NULL;
    }

    if (!root) {
        monitor_printf(mon, "no primary PCI bus (if there are multiple"
                       " PCI roots, you must use device_add instead)");
        return NULL;
    }
    bus = pci_get_bus_devfn(&devfn, root, devaddr);
    if (!bus) {
        monitor_printf(mon, "Invalid PCI device address %s\n", devaddr);
        return NULL;
    }
    if (!((BusState*)bus)->allow_hotplug) {
        monitor_printf(mon, "PCI bus doesn't support hotplug\n");
        return NULL;
    }

    switch (type) {
    case IF_SCSI:
        dev = pci_create(bus, devfn, "lsi53c895a");
        if (qdev_init(&dev->qdev) < 0)
            dev = NULL;
        if (dev && dinfo) {
            if (scsi_hot_add(mon, &dev->qdev, dinfo, 0) != 0) {
                qdev_unplug(&dev->qdev, NULL);
                dev = NULL;
            }
        }
        break;
    case IF_VIRTIO:
        if (!dinfo) {
            monitor_printf(mon, "virtio requires a backing file/device.\n");
            return NULL;
        }
        dev = pci_create(bus, devfn, "virtio-blk-pci");
        if (qdev_prop_set_drive(&dev->qdev, "drive", dinfo->bdrv) < 0) {
            object_unparent(OBJECT(dev));
            dev = NULL;
            break;
        }
        if (qdev_init(&dev->qdev) < 0)
            dev = NULL;
        break;
    default:
        dev = NULL;
    }
    return dev;
}

void pci_device_hot_add(Monitor *mon, const QDict *qdict)
{
    PCIDevice *dev = NULL;
    const char *pci_addr = qdict_get_str(qdict, "pci_addr");
    const char *type = qdict_get_str(qdict, "type");
    const char *opts = qdict_get_try_str(qdict, "opts");

    /* strip legacy tag */
    if (!strncmp(pci_addr, "pci_addr=", 9)) {
        pci_addr += 9;
    }

    if (!opts) {
        opts = "";
    }

    if (!strcmp(pci_addr, "auto"))
        pci_addr = NULL;

    if (strcmp(type, "nic") == 0) {
        dev = qemu_pci_hot_add_nic(mon, pci_addr, opts);
    } else if (strcmp(type, "storage") == 0) {
        dev = qemu_pci_hot_add_storage(mon, pci_addr, opts);
    } else {
        monitor_printf(mon, "invalid type: %s\n", type);
    }

    if (dev) {
        monitor_printf(mon, "OK root bus %s, bus %d, slot %d, function %d\n",
                       pci_root_bus_path(dev),
                       pci_bus_num(dev->bus), PCI_SLOT(dev->devfn),
                       PCI_FUNC(dev->devfn));
    } else
        monitor_printf(mon, "failed to add %s\n", opts);
}

static int pci_device_hot_remove(Monitor *mon, const char *pci_addr)
{
    PCIBus *root = pci_find_primary_bus();
    PCIDevice *d;
    int bus;
    unsigned slot;
    Error *local_err = NULL;

    if (!root) {
        monitor_printf(mon, "no primary PCI bus (if there are multiple"
                       " PCI roots, you must use device_del instead)");
        return -1;
    }

    if (pci_read_devaddr(mon, pci_addr, &bus, &slot)) {
        return -1;
    }

    d = pci_find_device(root, bus, PCI_DEVFN(slot, 0));
    if (!d) {
        monitor_printf(mon, "slot %d empty\n", slot);
        return -1;
    }

    qdev_unplug(&d->qdev, &local_err);
    if (local_err) {
        monitor_printf(mon, "%s\n", error_get_pretty(local_err));
        error_free(local_err);
        return -1;
    }

    return 0;
}

void do_pci_device_hot_remove(Monitor *mon, const QDict *qdict)
{
    pci_device_hot_remove(mon, qdict_get_str(qdict, "pci_addr"));
}
