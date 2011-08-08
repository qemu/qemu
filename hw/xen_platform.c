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

#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "irq.h"
#include "xen_common.h"
#include "net.h"
#include "xen_backend.h"
#include "trace.h"
#include "exec-memory.h"

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
    PCIDevice  pci_dev;
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

static void unplug_nic(PCIBus *b, PCIDevice *d)
{
    if (pci_get_word(d->config + PCI_CLASS_DEVICE) ==
            PCI_CLASS_NETWORK_ETHERNET) {
        qdev_unplug(&(d->qdev));
    }
}

static void pci_unplug_nics(PCIBus *bus)
{
    pci_for_each_device(bus, 0, unplug_nic);
}

static void unplug_disks(PCIBus *b, PCIDevice *d)
{
    if (pci_get_word(d->config + PCI_CLASS_DEVICE) ==
            PCI_CLASS_STORAGE_IDE) {
        qdev_unplug(&(d->qdev));
    }
}

static void pci_unplug_disks(PCIBus *bus)
{
    pci_for_each_device(bus, 0, unplug_disks);
}

static void platform_fixed_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    PCIXenPlatformState *s = opaque;

    switch (addr - XEN_PLATFORM_IOPORT) {
    case 0:
        /* Unplug devices.  Value is a bitmask of which devices to
           unplug, with bit 0 the IDE devices, bit 1 the network
           devices, and bit 2 the non-primary-master IDE devices. */
        if (val & UNPLUG_ALL_IDE_DISKS) {
            DPRINTF("unplug disks\n");
            qemu_aio_flush();
            bdrv_flush_all();
            pci_unplug_disks(s->pci_dev.bus);
        }
        if (val & UNPLUG_ALL_NICS) {
            DPRINTF("unplug nics\n");
            pci_unplug_nics(s->pci_dev.bus);
        }
        if (val & UNPLUG_AUX_IDE_DISKS) {
            DPRINTF("unplug auxiliary disks not supported\n");
        }
        break;
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
    switch (addr - XEN_PLATFORM_IOPORT) {
    case 0:
        /* PV driver version */
        break;
    }
}

static void platform_fixed_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCIXenPlatformState *s = opaque;

    switch (addr - XEN_PLATFORM_IOPORT) {
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

    switch (addr - XEN_PLATFORM_IOPORT) {
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

    switch (addr - XEN_PLATFORM_IOPORT) {
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

    platform_fixed_ioport_writeb(s, XEN_PLATFORM_IOPORT, 0);
}

const MemoryRegionPortio xen_platform_ioport[] = {
    { 0, 16, 4, .write = platform_fixed_ioport_writel, },
    { 0, 16, 2, .write = platform_fixed_ioport_writew, },
    { 0, 16, 1, .write = platform_fixed_ioport_writeb, },
    { 0, 16, 2, .read = platform_fixed_ioport_readw, },
    { 0, 16, 1, .read = platform_fixed_ioport_readb, },
    PORTIO_END_OF_LIST()
};

static const MemoryRegionOps platform_fixed_io_ops = {
    .old_portio = xen_platform_ioport,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void platform_fixed_ioport_init(PCIXenPlatformState* s)
{
    memory_region_init_io(&s->fixed_io, &platform_fixed_io_ops, s,
                          "xen-fixed", 16);
    memory_region_add_subregion(get_system_io(), XEN_PLATFORM_IOPORT,
                                &s->fixed_io);
}

/* Xen Platform PCI Device */

static uint32_t xen_platform_ioport_readb(void *opaque, uint32_t addr)
{
    if (addr == 0) {
        return platform_fixed_ioport_readb(opaque, XEN_PLATFORM_IOPORT);
    } else {
        return ~0u;
    }
}

static void xen_platform_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCIXenPlatformState *s = opaque;

    switch (addr) {
    case 0: /* Platform flags */
        platform_fixed_ioport_writeb(opaque, XEN_PLATFORM_IOPORT, val);
        break;
    case 8:
        log_writeb(s, val);
        break;
    default:
        break;
    }
}

static MemoryRegionPortio xen_pci_portio[] = {
    { 0, 0x100, 1, .read = xen_platform_ioport_readb, },
    { 0, 0x100, 1, .write = xen_platform_ioport_writeb, },
    PORTIO_END_OF_LIST()
};

static const MemoryRegionOps xen_pci_io_ops = {
    .old_portio = xen_pci_portio,
};

static void platform_ioport_bar_setup(PCIXenPlatformState *d)
{
    memory_region_init_io(&d->bar, &xen_pci_io_ops, d, "xen-pci", 0x100);
}

static uint64_t platform_mmio_read(void *opaque, target_phys_addr_t addr,
                                   unsigned size)
{
    DPRINTF("Warning: attempted read from physical address "
            "0x" TARGET_FMT_plx " in xen platform mmio space\n", addr);

    return 0;
}

static void platform_mmio_write(void *opaque, target_phys_addr_t addr,
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
    memory_region_init_io(&d->mmio_bar, &platform_mmio_handler, d,
                          "xen-mmio", 0x1000000);
}

static int xen_platform_post_load(void *opaque, int version_id)
{
    PCIXenPlatformState *s = opaque;

    platform_fixed_ioport_writeb(s, XEN_PLATFORM_IOPORT, s->flags);

    return 0;
}

static const VMStateDescription vmstate_xen_platform = {
    .name = "platform",
    .version_id = 4,
    .minimum_version_id = 4,
    .minimum_version_id_old = 4,
    .post_load = xen_platform_post_load,
    .fields = (VMStateField []) {
        VMSTATE_PCI_DEVICE(pci_dev, PCIXenPlatformState),
        VMSTATE_UINT8(flags, PCIXenPlatformState),
        VMSTATE_END_OF_LIST()
    }
};

static int xen_platform_initfn(PCIDevice *dev)
{
    PCIXenPlatformState *d = DO_UPCAST(PCIXenPlatformState, pci_dev, dev);
    uint8_t *pci_conf;

    pci_conf = d->pci_dev.config;

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    pci_config_set_prog_interface(pci_conf, 0);

    pci_conf[PCI_INTERRUPT_PIN] = 1;

    platform_ioport_bar_setup(d);
    pci_register_bar(&d->pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->bar);

    /* reserve 16MB mmio address for share memory*/
    platform_mmio_setup(d);
    pci_register_bar(&d->pci_dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &d->mmio_bar);

    platform_fixed_ioport_init(d);

    return 0;
}

static void platform_reset(DeviceState *dev)
{
    PCIXenPlatformState *s = DO_UPCAST(PCIXenPlatformState, pci_dev.qdev, dev);

    platform_fixed_ioport_reset(s);
}

static PCIDeviceInfo xen_platform_info = {
    .init = xen_platform_initfn,
    .qdev.name = "xen-platform",
    .qdev.desc = "XEN platform pci device",
    .qdev.size = sizeof(PCIXenPlatformState),
    .qdev.vmsd = &vmstate_xen_platform,
    .qdev.reset = platform_reset,

    .vendor_id    =  PCI_VENDOR_ID_XEN,
    .device_id    = PCI_DEVICE_ID_XEN_PLATFORM,
    .class_id     = PCI_CLASS_OTHERS << 8 | 0x80,
    .subsystem_vendor_id = PCI_VENDOR_ID_XEN,
    .subsystem_id = PCI_DEVICE_ID_XEN_PLATFORM,
    .revision = 1,
};

static void xen_platform_register(void)
{
    pci_qdev_register(&xen_platform_info);
}

device_init(xen_platform_register);
