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
/* #define DEBUG_TCE */

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

    if (dev->qirq) {
        uint32_t ints_prop[] = {cpu_to_be32(dev->vio_irq_num), 0};

        ret = fdt_setprop(fdt, node_off, "interrupts", ints_prop,
                          sizeof(ints_prop));
        if (ret < 0) {
            return ret;
        }
    }

    if (dev->rtce_window_size) {
        uint32_t dma_prop[] = {cpu_to_be32(dev->reg),
                               0, 0,
                               0, cpu_to_be32(dev->rtce_window_size)};

        ret = fdt_setprop_cell(fdt, node_off, "ibm,#dma-address-cells", 2);
        if (ret < 0) {
            return ret;
        }

        ret = fdt_setprop_cell(fdt, node_off, "ibm,#dma-size-cells", 2);
        if (ret < 0) {
            return ret;
        }

        ret = fdt_setprop(fdt, node_off, "ibm,my-dma-window", dma_prop,
                          sizeof(dma_prop));
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

/*
 * RTCE handling
 */

static void rtce_init(VIOsPAPRDevice *dev)
{
    size_t size = (dev->rtce_window_size >> SPAPR_VIO_TCE_PAGE_SHIFT)
        * sizeof(VIOsPAPR_RTCE);

    if (size) {
        dev->rtce_table = qemu_mallocz(size);
    }
}

static target_ulong h_put_tce(CPUState *env, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong liobn = args[0];
    target_ulong ioba = args[1];
    target_ulong tce = args[2];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, liobn);
    VIOsPAPR_RTCE *rtce;

    if (!dev) {
        hcall_dprintf("spapr_vio_put_tce on non-existent LIOBN "
                      TARGET_FMT_lx "\n", liobn);
        return H_PARAMETER;
    }

    ioba &= ~(SPAPR_VIO_TCE_PAGE_SIZE - 1);

#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_vio_put_tce on %s  ioba 0x" TARGET_FMT_lx
            "  TCE 0x" TARGET_FMT_lx "\n", dev->qdev.id, ioba, tce);
#endif

    if (ioba >= dev->rtce_window_size) {
        hcall_dprintf("spapr_vio_put_tce on out-of-boards IOBA 0x"
                      TARGET_FMT_lx "\n", ioba);
        return H_PARAMETER;
    }

    rtce = dev->rtce_table + (ioba >> SPAPR_VIO_TCE_PAGE_SHIFT);
    rtce->tce = tce;

    return H_SUCCESS;
}

int spapr_vio_check_tces(VIOsPAPRDevice *dev, target_ulong ioba,
                         target_ulong len, enum VIOsPAPR_TCEAccess access)
{
    int start, end, i;

    start = ioba >> SPAPR_VIO_TCE_PAGE_SHIFT;
    end = (ioba + len - 1) >> SPAPR_VIO_TCE_PAGE_SHIFT;

    for (i = start; i <= end; i++) {
        if ((dev->rtce_table[i].tce & access) != access) {
#ifdef DEBUG_TCE
            fprintf(stderr, "FAIL on %d\n", i);
#endif
            return -1;
        }
    }

    return 0;
}

int spapr_tce_dma_write(VIOsPAPRDevice *dev, uint64_t taddr, const void *buf,
                        uint32_t size)
{
#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_dma_write taddr=0x%llx size=0x%x\n",
            (unsigned long long)taddr, size);
#endif

    while (size) {
        uint64_t tce;
        uint32_t lsize;
        uint64_t txaddr;

        /* Check if we are in bound */
        if (taddr >= dev->rtce_window_size) {
#ifdef DEBUG_TCE
            fprintf(stderr, "spapr_tce_dma_write out of bounds\n");
#endif
            return H_DEST_PARM;
        }
        tce = dev->rtce_table[taddr >> SPAPR_VIO_TCE_PAGE_SHIFT].tce;

        /* How much til end of page ? */
        lsize = MIN(size, ((~taddr) & SPAPR_VIO_TCE_PAGE_MASK) + 1);

        /* Check TCE */
        if (!(tce & 2)) {
            return H_DEST_PARM;
        }

        /* Translate */
        txaddr = (tce & ~SPAPR_VIO_TCE_PAGE_MASK) |
            (taddr & SPAPR_VIO_TCE_PAGE_MASK);

#ifdef DEBUG_TCE
        fprintf(stderr, " -> write to txaddr=0x%llx, size=0x%x\n",
                (unsigned long long)txaddr, lsize);
#endif

        /* Do it */
        cpu_physical_memory_write(txaddr, buf, lsize);
        buf += lsize;
        taddr += lsize;
        size -= lsize;
    }
    return 0;
}

int spapr_tce_dma_zero(VIOsPAPRDevice *dev, uint64_t taddr, uint32_t size)
{
    /* FIXME: allocating a temp buffer is nasty, but just stepping
     * through writing zeroes is awkward.  This will do for now. */
    uint8_t zeroes[size];

#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_dma_zero taddr=0x%llx size=0x%x\n",
            (unsigned long long)taddr, size);
#endif

    memset(zeroes, 0, size);
    return spapr_tce_dma_write(dev, taddr, zeroes, size);
}

void stb_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint8_t val)
{
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}

void sth_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint16_t val)
{
    val = tswap16(val);
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}


void stw_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint32_t val)
{
    val = tswap32(val);
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}

void stq_tce(VIOsPAPRDevice *dev, uint64_t taddr, uint64_t val)
{
    val = tswap64(val);
    spapr_tce_dma_write(dev, taddr, &val, sizeof(val));
}

int spapr_tce_dma_read(VIOsPAPRDevice *dev, uint64_t taddr, void *buf,
                       uint32_t size)
{
#ifdef DEBUG_TCE
    fprintf(stderr, "spapr_tce_dma_write taddr=0x%llx size=0x%x\n",
            (unsigned long long)taddr, size);
#endif

    while (size) {
        uint64_t tce;
        uint32_t lsize;
        uint64_t txaddr;

        /* Check if we are in bound */
        if (taddr >= dev->rtce_window_size) {
#ifdef DEBUG_TCE
            fprintf(stderr, "spapr_tce_dma_read out of bounds\n");
#endif
            return H_DEST_PARM;
        }
        tce = dev->rtce_table[taddr >> SPAPR_VIO_TCE_PAGE_SHIFT].tce;

        /* How much til end of page ? */
        lsize = MIN(size, ((~taddr) & SPAPR_VIO_TCE_PAGE_MASK) + 1);

        /* Check TCE */
        if (!(tce & 1)) {
            return H_DEST_PARM;
        }

        /* Translate */
        txaddr = (tce & ~SPAPR_VIO_TCE_PAGE_MASK) |
            (taddr & SPAPR_VIO_TCE_PAGE_MASK);

#ifdef DEBUG_TCE
        fprintf(stderr, " -> write to txaddr=0x%llx, size=0x%x\n",
                (unsigned long long)txaddr, lsize);
#endif
        /* Do it */
        cpu_physical_memory_read(txaddr, buf, lsize);
        buf += lsize;
        taddr += lsize;
        size -= lsize;
    }
    return H_SUCCESS;
}

uint64_t ldq_tce(VIOsPAPRDevice *dev, uint64_t taddr)
{
    uint64_t val;

    spapr_tce_dma_read(dev, taddr, &val, sizeof(val));
    return tswap64(val);
}

static int spapr_vio_busdev_init(DeviceState *qdev, DeviceInfo *qinfo)
{
    VIOsPAPRDeviceInfo *info = (VIOsPAPRDeviceInfo *)qinfo;
    VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;
    char *id;

    if (asprintf(&id, "%s@%x", info->dt_name, dev->reg) < 0) {
        return -1;
    }

    dev->qdev.id = id;

    rtce_init(dev);

    return info->init(dev);
}

void spapr_vio_bus_register_withprop(VIOsPAPRDeviceInfo *info)
{
    info->qdev.init = spapr_vio_busdev_init;
    info->qdev.bus_info = &spapr_vio_bus_info;

    assert(info->qdev.size >= sizeof(VIOsPAPRDevice));
    qdev_register(&info->qdev);
}

static target_ulong h_vio_signal(CPUState *env, sPAPREnvironment *spapr,
                                 target_ulong opcode,
                                 target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong mode = args[1];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRDeviceInfo *info;

    if (!dev) {
        return H_PARAMETER;
    }

    info = (VIOsPAPRDeviceInfo *)dev->qdev.info;

    if (mode & ~info->signal_mask) {
        return H_PARAMETER;
    }

    dev->signal_state = mode;

    return H_SUCCESS;
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

    /* hcall-vio */
    spapr_register_hypercall(H_VIO_SIGNAL, h_vio_signal);

    /* hcall-tce */
    spapr_register_hypercall(H_PUT_TCE, h_put_tce);

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
