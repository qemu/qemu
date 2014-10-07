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

#include <assert.h>

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/ide.h"
#include "hw/pci/pci.h"
#include "hw/irq.h"
#include "hw/xen/xen_common.h"
#include "hw/xen/xen_backend.h"
#include "trace.h"
#include "exec/address-spaces.h"
#include "sysemu/block-backend.h"

#include <xenguest.h>

//#define DEBUG_PLATFORM

#ifdef DEBUG_PLATFORM
#define DPRINTF(fmt, ...) do { \
    fprintf(stderr, "xen_platform: " fmt, ## __VA_ARGS__); \
} while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define PFFLAG_ROM_LOCK 1 /* Sets whether ROM memory area is RW or RO */

typedef struct PCIXenPlatformState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion fixed_io;
    MemoryRegion bar;
    MemoryRegion mmio_bar;
    uint8_t flags; /* used only for version_id == 2 */
    int drivers_blacklisted;
    uint16_t driver_product_version;

    /* Log from guest drivers */
    char log_buffer[4096];
    int log_buffer_off;
} PCIXenPlatformState;

#define TYPE_XEN_PLATFORM "xen-platform"
#define XEN_PLATFORM(obj) \
    OBJECT_CHECK(PCIXenPlatformState, (obj), TYPE_XEN_PLATFORM)

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

/* Xen Platform, Fixed IOPort */
#define UNPLUG_ALL_IDE_DISKS 1
#define UNPLUG_ALL_NICS 2
#define UNPLUG_AUX_IDE_DISKS 4

static void unplug_nic(PCIBus *b, PCIDevice *d, void *o)
{
    /* We have to ignore passthrough devices */
    if (pci_get_word(d->config + PCI_CLASS_DEVICE) ==
            PCI_CLASS_NETWORK_ETHERNET
            && strcmp(d->name, "xen-pci-passthrough") != 0) {
        object_unparent(OBJECT(d));
    }
}

static void pci_unplug_nics(PCIBus *bus)
{
    pci_for_each_device(bus, 0, unplug_nic, NULL);
}

static void unplug_disks(PCIBus *b, PCIDevice *d, void *o)
{
    /* We have to ignore passthrough devices */
    if (pci_get_word(d->config + PCI_CLASS_DEVICE) ==
            PCI_CLASS_STORAGE_IDE
            && strcmp(d->name, "xen-pci-passthrough") != 0) {
        pci_piix3_xen_ide_unplug(DEVICE(d));
    }
}

static void pci_unplug_disks(PCIBus *bus)
{
    pci_for_each_device(bus, 0, unplug_disks, NULL);
}

static void platform_fixed_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    PCIXenPlatformState *s = opaque;

    switch (addr) {
    case 0: {
        PCIDevice *pci_dev = PCI_DEVICE(s);
        /* Unplug devices.  Value is a bitmask of which devices to
           unplug, with bit 0 the IDE devices, bit 1 the network
           devices, and bit 2 the non-primary-master IDE devices. */
        if (val & UNPLUG_ALL_IDE_DISKS) {
            DPRINTF("unplug disks\n");
            blk_drain_all();
            blk_flush_all();
            pci_unplug_disks(pci_dev->bus);
        }
        if (val & UNPLUG_ALL_NICS) {
            DPRINTF("unplug nics\n");
            pci_unplug_nics(pci_dev->bus);
        }
        if (val & UNPLUG_AUX_IDE_DISKS) {
            DPRINTF("unplug auxiliary disks not supported\n");
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
    case 0: /* Platform flags */ {
        hvmmem_type_t mem_type = (val & PFFLAG_ROM_LOCK) ?
            HVMMEM_ram_ro : HVMMEM_ram_rw;
        if (xc_hvm_set_mem_type(xen_xc, xen_domid, mem_type, 0xc0, 0x40)) {
            DPRINTF("unable to change ro/rw state of ROM memory area!\n");
        } else {
            s->flags = val & PFFLAG_ROM_LOCK;
            DPRINTF("changed ro/rw state of ROM memory area. now is %s state.\n",
                    (mem_type == HVMMEM_ram_ro ? "ro":"rw"));
        }
        break;
    }
    case 2:
        log_writeb(s, val);
        break;
    }
}

static uint32_t platform_fixed_ioport_readw(void *opaque, uint32_t addr)
{
    PCIXenPlatformState *s = opaque;

    switch (addr) {
    case 0:
        if (s->drivers_blacklisted) {
            /* The drivers will recognise this magic number and refuse
             * to do anything. */
            return 0xd249;
        } else {
            /* Magic value so that you can identify the interface. */
            return 0x49d2;
        }
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

    switch (addr) {
    case 0: /* Platform flags */
        platform_fixed_ioport_writeb(opaque, 0, (uint32_t)val);
        break;
    case 8:
        log_writeb(s, (uint32_t)val);
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
            "0x" TARGET_FMT_plx " in xen platform mmio space\n", addr);

    return 0;
}

static void platform_mmio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    DPRINTF("Warning: attempted write of 0x%"PRIx64" to physical "
            "address 0x" TARGET_FMT_plx " in xen platform mmio space\n",
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

static int xen_platform_initfn(PCIDevice *dev)
{
    PCIXenPlatformState *d = XEN_PLATFORM(dev);
    uint8_t *pci_conf;

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

    return 0;
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

    k->init = xen_platform_initfn;
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
};

static void xen_platform_register_types(void)
{
    type_register_static(&xen_platform_info);
}

type_init(xen_platform_register_types)
