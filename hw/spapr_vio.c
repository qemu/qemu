/*
 * QEMU sPAPR VIO code
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 * Based on the s390 virtio bus code:
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "sysemu.h"
#include "boards.h"
#include "monitor.h"
#include "loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "kvm.h"
#include "device_tree.h"

#include "hw/spapr.h"
#include "hw/spapr_vio.h"

#ifdef CONFIG_FDT
#include <libfdt.h>
#endif /* CONFIG_FDT */

/* #define DEBUG_SPAPR */

#ifdef DEBUG_SPAPR
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

static struct BusInfo spapr_vio_bus_info = {
    .name       = "spapr-vio",
    .size       = sizeof(VIOsPAPRBus),
};

VIOsPAPRDevice *spapr_vio_find_by_reg(VIOsPAPRBus *bus, uint32_t reg)
{
    DeviceState *qdev;
    VIOsPAPRDevice *dev = NULL;

    QLIST_FOREACH(qdev, &bus->bus.children, sibling) {
        dev = (VIOsPAPRDevice *)qdev;
        if (dev->reg == reg) {
            break;
        }
    }

    return dev;
}

#ifdef CONFIG_FDT
static int vio_make_devnode(VIOsPAPRDevice *dev,
                            void *fdt)
{
    VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)dev->qdev.info;
    int vdevice_off, node_off;
    int ret;

    vdevice_off = fdt_path_offset(fdt, "/vdevice");
    if (vdevice_off < 0) {
        return vdevice_off;
    }

    node_off = fdt_add_subnode(fdt, vdevice_off, dev->qdev.id);
    if (node_off < 0) {
        return node_off;
    }

    ret = fdt_setprop_cell(fdt, node_off, "reg", dev->reg);
    if (ret < 0) {
        return ret;
    }

    if (info->dt_type) {
        ret = fdt_setprop_string(fdt, node_off, "device_type",
                                 info->dt_type);
        if (ret < 0) {
            return ret;
        }
    }

    if (info->dt_compatible) {
        ret = fdt_setprop_string(fdt, node_off, "compatible",
                                 info->dt_compatible);
        if (ret < 0) {
            return ret;
        }
    }

    if (info->devnode) {
        ret = (info->devnode)(dev, fdt, node_off);
        if (ret < 0) {
            return ret;
        }
    }

    return node_off;
}
#endif /* CONFIG_FDT */

static int spapr_vio_busdev_init(DeviceState *qdev, DeviceInfo *qinfo)
{
    VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)qinfo;
    VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;
    char *id;

    if (asprintf(&id, "%s@%x", info->dt_name, dev->reg) < 0) {
        return -1;
    }

    dev->qdev.id = id;

    return info->init(dev);
}

void spapr_vio_bus_register_withprop(VIOsPAPRDeviceInfo *info)
{
    info->qdev.init = spapr_vio_busdev_init;
    info->qdev.bus_info = &spapr_vio_bus_info;

    assert(info->qdev.size >= sizeof(VIOsPAPRDevice));
    qdev_register(&info->qdev);
}

VIOsPAPRBus *spapr_vio_bus_init(void)
{
    VIOsPAPRBus *bus;
    BusState *qbus;
    DeviceState *dev;
    DeviceInfo *qinfo;

    /* Create bridge device */
    dev = qdev_create(NULL, "spapr-vio-bridge");
    qdev_init_nofail(dev);

    /* Create bus on bridge device */

    qbus = qbus_create(&spapr_vio_bus_info, dev, "spapr-vio");
    bus = DO_UPCAST(VIOsPAPRBus, bus, qbus);

    for (qinfo = device_info_list; qinfo; qinfo = qinfo->next) {
        VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)qinfo;

        if (qinfo->bus_info != &spapr_vio_bus_info) {
            continue;
        }

        if (info->hcalls) {
            info->hcalls(bus);
        }
    }

    return bus;
}

/* Represents sPAPR hcall VIO devices */

static int spapr_vio_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
}

static SysBusDeviceInfo spapr_vio_bridge_info = {
    .init = spapr_vio_bridge_init,
    .qdev.name  = "spapr-vio-bridge",
    .qdev.size  = sizeof(SysBusDevice),
    .qdev.no_user = 1,
};

static void spapr_vio_register_devices(void)
{
    sysbus_register_withprop(&spapr_vio_bridge_info);
}

device_init(spapr_vio_register_devices)

#ifdef CONFIG_FDT
int spapr_populate_vdevice(VIOsPAPRBus *bus, void *fdt)
{
    DeviceState *qdev;
    int ret = 0;

    QLIST_FOREACH(qdev, &bus->bus.children, sibling) {
        VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;

        ret = vio_make_devnode(dev, fdt);

        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}
#endif /* CONFIG_FDT */
