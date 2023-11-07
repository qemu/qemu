/*
 * XEN platform pci device, formerly known as the event channel device
 *
 * Copyright (c) 2003-2004 Intel Corp.
 * Copyright (c) 2006 XenSource
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/ide/pci.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "trace.h"
#include "sysemu/xen.h"
#include "sysemu/block-backend.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qom/object.h"

#ifdef CONFIG_XEN
#include "hw/xen/xen_native.h"
#endif

/* The rule is that xen_native.h must come first */
#include "hw/xen/xen.h"

//#define DEBUG_PLATFORM

#ifdef DEBUG_PLATFORM
#define DPRINTF(fmt, ...) do { \
    fprintf(stderr, "xen_platform: " fmt, ## __VA_ARGS__); \
} while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define PFFLAG_ROM_LOCK 1 /* Sets whether ROM memory area is RW or RO */

struct PCIXenPlatformState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion fixed_io;
    MemoryRegion bar;
    MemoryRegion mmio_bar;
    uint8_t flags; /* used only for version_id == 2 */
    uint16_t driver_product_version;

    /* Log from guest drivers */
    char log_buffer[4096];
    int log_buffer_off;
};

#define TYPE_XEN_PLATFORM "xen-platform"
OBJECT_DECLARE_SIMPLE_TYPE(PCIXenPlatformState, XEN_PLATFORM)

#define XEN_PLATFORM_IOPORT 0x10

/* Send bytes to syslog */
static void log_writeb(PCIXenPlatformState *s, char val)
{
    if (val == '\n' || s->log_buffer_off == sizeof(s->log_buffer) - 1) {
        /* Flush buffer */
        s->log_buffer[s->log_buffer_off] = 0;
        trace_xen_platform_log(s->log_buffer);
        s->log_buffer_off = 0;
    } else {
        s->log_buffer[s->log_buffer_off++] = val;
    }
}

/*
 * Unplug device flags.
 *
 * The logic got a little confused at some point in the past but this is
 * what they do now.
 *
 * bit 0: Unplug all IDE and SCSI disks.
 * bit 1: Unplug all NICs.
 * bit 2: Unplug IDE disks except primary master. This is overridden if
 *        bit 0 is also present in the mask.
 * bit 3: Unplug all NVMe disks.
 *
 */
#define _UNPLUG_IDE_SCSI_DISKS 0
#define UNPLUG_IDE_SCSI_DISKS (1u << _UNPLUG_IDE_SCSI_DISKS)

#define _UNPLUG_ALL_NICS 1
#define UNPLUG_ALL_NICS (1u << _UNPLUG_ALL_NICS)

#define _UNPLUG_AUX_IDE_DISKS 2
#define UNPLUG_AUX_IDE_DISKS (1u << _UNPLUG_AUX_IDE_DISKS)

#define _UNPLUG_NVME_DISKS 3
#define UNPLUG_NVME_DISKS (1u << _UNPLUG_NVME_DISKS)

static bool pci_device_is_passthrough(PCIDevice *d)
{
    if (!strcmp(d->name, "xen-pci-passthrough")) {
        return true;
    }

    if (xen_mode == XEN_EMULATE && !strcmp(d->name, "vfio-pci")) {
        return true;
    }

    return false;
}

static void unplug_nic(PCIBus *b, PCIDevice *d, void *o)
{
    /* We have to ignore passthrough devices */
    if (pci_get_word(d->config + PCI_CLASS_DEVICE) ==
            PCI_CLASS_NETWORK_ETHERNET
            && !pci_device_is_passthrough(d)) {
        object_unparent(OBJECT(d));
    }
}

/* Remove the peer of the NIC device. Normally, this would be a tap device. */
static void del_nic_peer(NICState *nic, void *opaque)
{
    NetClientState *nc = qemu_get_queue(nic);
    ObjectClass *klass = module_object_class_by_name(nc->model);

    /* Only delete peers of PCI NICs that we're about to delete */
    if (!klass || !object_class_dynamic_cast(klass, TYPE_PCI_DEVICE)) {
        return;
    }

    if (nc->peer)
        qemu_del_net_client(nc->peer);
}

static void pci_unplug_nics(PCIBus *bus)
{
    qemu_foreach_nic(del_nic_peer, NULL);
    pci_for_each_device(bus, 0, unplug_nic, NULL);
}

/*
 * The Xen HVM unplug protocol [1] specifies a mechanism to allow guests to
 * request unplug of 'aux' disks (which is stated to mean all IDE disks,
 * except the primary master).
 *
 * NOTE: The semantics of what happens if unplug of all disks and 'aux' disks
 *       is simultaneously requested is not clear. The implementation assumes
 *       that an 'all' request overrides an 'aux' request.
 *
 * [1] https://xenbits.xen.org/gitweb/?p=xen.git;a=blob;f=docs/misc/hvm-emulated-unplug.pandoc
 */
struct ide_unplug_state {
    bool aux;
    int nr_unplugged;
};

static int ide_dev_unplug(DeviceState *dev, void *_st)
{
    struct ide_unplug_state *st = _st;
    IDEDevice *idedev;
    IDEBus *idebus;
    BlockBackend *blk;
    int unit;

    idedev = IDE_DEVICE(object_dynamic_cast(OBJECT(dev), "ide-hd"));
    if (!idedev) {
        return 0;
    }

    idebus = IDE_BUS(qdev_get_parent_bus(dev));

    unit = (idedev == idebus->slave);
    assert(unit || idedev == idebus->master);

    if (st->aux && !unit && !strcmp(BUS(idebus)->name, "ide.0")) {
        return 0;
    }

    blk = idebus->ifs[unit].blk;
    if (blk) {
        blk_drain(blk);
        blk_flush(blk);

        blk_detach_dev(blk, DEVICE(idedev));
        idebus->ifs[unit].blk = NULL;
        idedev->conf.blk = NULL;
        monitor_remove_blk(blk);
        blk_unref(blk);
    }

    object_unparent(OBJECT(dev));
    st->nr_unplugged++;

    return 0;
}

static void pci_xen_ide_unplug(PCIDevice *d, bool aux)
{
    struct ide_unplug_state st = { aux, 0 };
    DeviceState *dev = DEVICE(d);

    qdev_walk_children(dev, NULL, NULL, ide_dev_unplug, NULL, &st);
    if (st.nr_unplugged) {
        pci_device_reset(d);
    }
}

static void unplug_disks(PCIBus *b, PCIDevice *d, void *opaque)
{
    uint32_t flags = *(uint32_t *)opaque;
    bool aux = (flags & UNPLUG_AUX_IDE_DISKS) &&
        !(flags & UNPLUG_IDE_SCSI_DISKS);

    /* We have to ignore passthrough devices */
    if (pci_device_is_passthrough(d))
        return;

    switch (pci_get_word(d->config + PCI_CLASS_DEVICE)) {
    case PCI_CLASS_STORAGE_IDE:
    case PCI_CLASS_STORAGE_SATA:
        pci_xen_ide_unplug(d, aux);
        break;

    case PCI_CLASS_STORAGE_SCSI:
        if (!aux) {
            object_unparent(OBJECT(d));
        }
        break;

    case PCI_CLASS_STORAGE_EXPRESS:
        if (flags & UNPLUG_NVME_DISKS) {
            object_unparent(OBJECT(d));
        }

    default:
        break;
    }
}

static void pci_unplug_disks(PCIBus *bus, uint32_t flags)
{
    pci_for_each_device(bus, 0, unplug_disks, &flags);
}

static void platform_fixed_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    PCIXenPlatformState *s = opaque;

    switch (addr) {
    case 0: {
        PCIDevice *pci_dev = PCI_DEVICE(s);
        /* Unplug devices. See comment above flag definitions */
        if (val & (UNPLUG_IDE_SCSI_DISKS | UNPLUG_AUX_IDE_DISKS |
                   UNPLUG_NVME_DISKS)) {
            DPRINTF("unplug disks\n");
            pci_unplug_disks(pci_get_bus(pci_dev), val);
        }
        if (val & UNPLUG_ALL_NICS) {
            DPRINTF("unplug nics\n");
            pci_unplug_nics(pci_get_bus(pci_dev));
        }
        break;
    }
    case 2:
        switch (val) {
        case 1:
            DPRINTF("Citrix Windows PV drivers loaded in guest\n");
            break;
        case 0:
            DPRINTF("Guest claimed to be running PV product 0?\n");
            break;
        default:
            DPRINTF("Unknown PV product %d loaded in guest\n", val);
            break;
        }
        s->driver_product_version = val;
        break;
    }
}

static void platform_fixed_ioport_writel(void *opaque, uint32_t addr,
                                         uint32_t val)
{
    switch (addr) {
    case 0:
        /* PV driver version */
        break;
    }
}

static void platform_fixed_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCIXenPlatformState *s = opaque;

    switch (addr) {
    case 0: /* Platform flags */
        if (xen_mode == XEN_EMULATE) {
            /* XX: Use i440gx/q35 PAM setup to do this? */
            s->flags = val & PFFLAG_ROM_LOCK;
#ifdef CONFIG_XEN
        } else {
            hvmmem_type_t mem_type = (val & PFFLAG_ROM_LOCK) ?
                HVMMEM_ram_ro : HVMMEM_ram_rw;

            if (xen_set_mem_type(xen_domid, mem_type, 0xc0, 0x40)) {
                DPRINTF("unable to change ro/rw state of ROM memory area!\n");
            } else {
                s->flags = val & PFFLAG_ROM_LOCK;
                DPRINTF("changed ro/rw state of ROM memory area. now is %s state.\n",
                        (mem_type == HVMMEM_ram_ro ? "ro" : "rw"));
            }
#endif
        }
        break;

    case 2:
        log_writeb(s, val);
        break;
    }
}

static uint32_t platform_fixed_ioport_readw(void *opaque, uint32_t addr)
{
    switch (addr) {
    case 0:
        /* Magic value so that you can identify the interface. */
        return 0x49d2;
    default:
        return 0xffff;
    }
}

static uint32_t platform_fixed_ioport_readb(void *opaque, uint32_t addr)
{
    PCIXenPlatformState *s = opaque;

    switch (addr) {
    case 0:
        /* Platform flags */
        return s->flags;
    case 2:
        /* Version number */
        return 1;
    default:
        return 0xff;
    }
}

static void platform_fixed_ioport_reset(void *opaque)
{
    PCIXenPlatformState *s = opaque;

    platform_fixed_ioport_writeb(s, 0, 0);
}

static uint64_t platform_fixed_ioport_read(void *opaque,
                                           hwaddr addr,
                                           unsigned size)
{
    switch (size) {
    case 1:
        return platform_fixed_ioport_readb(opaque, addr);
    case 2:
        return platform_fixed_ioport_readw(opaque, addr);
    default:
        return -1;
    }
}

static void platform_fixed_ioport_write(void *opaque, hwaddr addr,

                                        uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        platform_fixed_ioport_writeb(opaque, addr, val);
        break;
    case 2:
        platform_fixed_ioport_writew(opaque, addr, val);
        break;
    case 4:
        platform_fixed_ioport_writel(opaque, addr, val);
        break;
    }
}


static const MemoryRegionOps platform_fixed_io_ops = {
    .read = platform_fixed_ioport_read,
    .write = platform_fixed_ioport_write,
    .valid = {
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void platform_fixed_ioport_init(PCIXenPlatformState* s)
{
    memory_region_init_io(&s->fixed_io, OBJECT(s), &platform_fixed_io_ops, s,
                          "xen-fixed", 16);
    memory_region_add_subregion(get_system_io(), XEN_PLATFORM_IOPORT,
                                &s->fixed_io);
}

/* Xen Platform PCI Device */

static uint64_t xen_platform_ioport_readb(void *opaque, hwaddr addr,
                                          unsigned int size)
{
    if (addr == 0) {
        return platform_fixed_ioport_readb(opaque, 0);
    } else {
        return ~0u;
    }
}

static void xen_platform_ioport_writeb(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned int size)
{
    PCIXenPlatformState *s = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(s);

    switch (addr) {
    case 0: /* Platform flags */
        platform_fixed_ioport_writeb(opaque, 0, (uint32_t)val);
        break;
    case 4:
        if (val == 1) {
            /*
             * SUSE unplug for Xenlinux
             * xen-kmp used this since xen-3.0.4, instead the official protocol
             * from xen-3.3+ It did an unconditional "outl(1, (ioaddr + 4));"
             * Pre VMDP 1.7 used 4 and 8 depending on how VMDP was configured.
             * If VMDP was to control both disk and LAN it would use 4.
             * If it controlled just disk or just LAN, it would use 8 below.
             */
            pci_unplug_disks(pci_get_bus(pci_dev), UNPLUG_IDE_SCSI_DISKS);
            pci_unplug_nics(pci_get_bus(pci_dev));
        }
        break;
    case 8:
        switch (val) {
        case 1:
            pci_unplug_disks(pci_get_bus(pci_dev), UNPLUG_IDE_SCSI_DISKS);
            break;
        case 2:
            pci_unplug_nics(pci_get_bus(pci_dev));
            break;
        default:
            log_writeb(s, (uint32_t)val);
            break;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps xen_pci_io_ops = {
    .read  = xen_platform_ioport_readb,
    .write = xen_platform_ioport_writeb,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static void platform_ioport_bar_setup(PCIXenPlatformState *d)
{
    memory_region_init_io(&d->bar, OBJECT(d), &xen_pci_io_ops, d,
                          "xen-pci", 0x100);
}

static uint64_t platform_mmio_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    DPRINTF("Warning: attempted read from physical address "
            "0x" HWADDR_FMT_plx " in xen platform mmio space\n", addr);

    return 0;
}

static void platform_mmio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    DPRINTF("Warning: attempted write of 0x%"PRIx64" to physical "
            "address 0x" HWADDR_FMT_plx " in xen platform mmio space\n",
            val, addr);
}

static const MemoryRegionOps platform_mmio_handler = {
    .read = &platform_mmio_read,
    .write = &platform_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void platform_mmio_setup(PCIXenPlatformState *d)
{
    memory_region_init_io(&d->mmio_bar, OBJECT(d), &platform_mmio_handler, d,
                          "xen-mmio", 0x1000000);
}

static int xen_platform_post_load(void *opaque, int version_id)
{
    PCIXenPlatformState *s = opaque;

    platform_fixed_ioport_writeb(s, 0, s->flags);

    return 0;
}

static const VMStateDescription vmstate_xen_platform = {
    .name = "platform",
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = xen_platform_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIXenPlatformState),
        VMSTATE_UINT8(flags, PCIXenPlatformState),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_platform_realize(PCIDevice *dev, Error **errp)
{
    PCIXenPlatformState *d = XEN_PLATFORM(dev);
    uint8_t *pci_conf;

    /* Device will crash on reset if xen is not initialized */
    if (xen_mode == XEN_DISABLED) {
        error_setg(errp, "xen-platform device requires a Xen guest");
        return;
    }

    pci_conf = dev->config;

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    pci_config_set_prog_interface(pci_conf, 0);

    pci_conf[PCI_INTERRUPT_PIN] = 1;

    platform_ioport_bar_setup(d);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->bar);

    /* reserve 16MB mmio address for share memory*/
    platform_mmio_setup(d);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &d->mmio_bar);

    platform_fixed_ioport_init(d);
}

static void platform_reset(DeviceState *dev)
{
    PCIXenPlatformState *s = XEN_PLATFORM(dev);

    platform_fixed_ioport_reset(s);
}

static void xen_platform_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = xen_platform_realize;
    k->vendor_id = PCI_VENDOR_ID_XEN;
    k->device_id = PCI_DEVICE_ID_XEN_PLATFORM;
    k->class_id = PCI_CLASS_OTHERS << 8 | 0x80;
    k->subsystem_vendor_id = PCI_VENDOR_ID_XEN;
    k->subsystem_id = PCI_DEVICE_ID_XEN_PLATFORM;
    k->revision = 1;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "XEN platform pci device";
    dc->reset = platform_reset;
    dc->vmsd = &vmstate_xen_platform;
}

static const TypeInfo xen_platform_info = {
    .name          = TYPE_XEN_PLATFORM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIXenPlatformState),
    .class_init    = xen_platform_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void xen_platform_register_types(void)
{
    type_register_static(&xen_platform_info);
}

type_init(xen_platform_register_types)
