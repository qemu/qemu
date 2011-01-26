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
#include "pc.h"
#include "monitor.h"
#include "scsi.h"
#include "virtio-blk.h"
#include "qemu-config.h"
#include "blockdev.h"

#if defined(TARGET_I386)
static PCIDevice *qemu_pci_hot_add_nic(Monitor *mon,
                                       const char *devaddr,
                                       const char *opts_str)
{
    QemuOpts *opts;
    PCIBus *bus;
    int ret, devfn;

    bus = pci_get_bus_devfn(&devfn, devaddr);
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

    ret = net_client_init(mon, opts, 0);
    if (ret < 0)
        return NULL;
    if (nd_table[ret].devaddr) {
        monitor_printf(mon, "Parameter addr not supported\n");
        return NULL;
    }
    return pci_nic_init(&nd_table[ret], "rtl8139", devaddr);
}

static int scsi_hot_add(Monitor *mon, DeviceState *adapter,
                        DriveInfo *dinfo, int printinfo)
{
    SCSIBus *scsibus;
    SCSIDevice *scsidev;

    scsibus = DO_UPCAST(SCSIBus, qbus, QLIST_FIRST(&adapter->child_bus));
    if (!scsibus || strcmp(scsibus->qbus.info->name, "SCSI") != 0) {
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
    scsidev = scsi_bus_legacy_add_drive(scsibus, dinfo->bdrv, dinfo->unit, false);
    if (!scsidev) {
        return -1;
    }
    dinfo->unit = scsidev->id;

    if (printinfo)
        monitor_printf(mon, "OK bus %d, unit %d\n",
                       scsibus->busnr, scsidev->id);
    return 0;
}

void drive_hot_add(Monitor *mon, const QDict *qdict)
{
    int dom, pci_bus;
    unsigned slot;
    int type;
    PCIDevice *dev;
    DriveInfo *dinfo = NULL;
    const char *pci_addr = qdict_get_str(qdict, "pci_addr");
    const char *opts = qdict_get_str(qdict, "opts");

    dinfo = add_init_drive(opts);
    if (!dinfo)
        goto err;
    if (dinfo->devaddr) {
        monitor_printf(mon, "Parameter addr not supported\n");
        goto err;
    }
    type = dinfo->type;

    switch (type) {
    case IF_SCSI:
        if (pci_read_devaddr(mon, pci_addr, &dom, &pci_bus, &slot)) {
            goto err;
        }
        dev = pci_find_device(pci_find_root_bus(dom), pci_bus, slot, 0);
        if (!dev) {
            monitor_printf(mon, "no pci device with address %s\n", pci_addr);
            goto err;
        }
        if (scsi_hot_add(mon, &dev->qdev, dinfo, 1) != 0) {
            goto err;
        }
        break;
    case IF_NONE:
        monitor_printf(mon, "OK\n");
        break;
    default:
        monitor_printf(mon, "Can't hot-add drive to type %d\n", type);
        goto err;
    }
    return;

err:
    if (dinfo)
        drive_put_ref(dinfo);
    return;
}

static PCIDevice *qemu_pci_hot_add_storage(Monitor *mon,
                                           const char *devaddr,
                                           const char *opts)
{
    PCIDevice *dev;
    DriveInfo *dinfo = NULL;
    int type = -1;
    char buf[128];
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

    bus = pci_get_bus_devfn(&devfn, devaddr);
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
                qdev_unplug(&dev->qdev);
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
            qdev_free(&dev->qdev);
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
        monitor_printf(mon, "OK domain %d, bus %d, slot %d, function %d\n",
                       pci_find_domain(dev->bus),
                       pci_bus_num(dev->bus), PCI_SLOT(dev->devfn),
                       PCI_FUNC(dev->devfn));
    } else
        monitor_printf(mon, "failed to add %s\n", opts);
}
#endif

static int pci_device_hot_remove(Monitor *mon, const char *pci_addr)
{
    PCIDevice *d;
    int dom, bus;
    unsigned slot;

    if (pci_read_devaddr(mon, pci_addr, &dom, &bus, &slot)) {
        return -1;
    }

    d = pci_find_device(pci_find_root_bus(dom), bus, slot, 0);
    if (!d) {
        monitor_printf(mon, "slot %d empty\n", slot);
        return -1;
    }
    return qdev_unplug(&d->qdev);
}

void do_pci_device_hot_remove(Monitor *mon, const QDict *qdict)
{
    pci_device_hot_remove(mon, qdict_get_str(qdict, "pci_addr"));
}
