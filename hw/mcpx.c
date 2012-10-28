/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "hw.h"
#include "pc.h"
#include "pci.h"

#include "mcpx.h"


//#define DEBUG
#ifdef DEBUG
# define MCPX_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define MCPX_DPRINTF(format, ...)       do { } while (0)
#endif


typedef struct MCPXState {
    PCIDevice dev;

    qemu_irq irq;

    MemoryRegion mmio;
    MemoryRegion vp;
} MCPXState;


#define MCPX_DEVICE(obj) \
    OBJECT_CHECK(MCPXState, (obj), "mcpx")


static uint64_t mcpx_read(void *opaque,
                          hwaddr addr, unsigned int size)
{
    MCPX_DPRINTF("mcpx: read [0x%llx]\n", addr);
    return 0;
}
static void mcpx_write(void *opaque, hwaddr addr,
                       uint64_t val, unsigned int size)
{
    MCPX_DPRINTF("mcpx: [0x%llx] = 0x%llx\n", addr, val);
}


/* Voice Processor */
static uint64_t mcpx_vp_read(void *opaque,
                             hwaddr addr, unsigned int size)
{
    MCPX_DPRINTF("mcpx VP: read [0x%llx]\n", addr);
    switch (addr) {
    case 0x10: /* instruction queue free space */
        return 0x20;
    default:
        break;
    }
    return 0;
}
static void mcpx_vp_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned int size)
{
    MCPX_DPRINTF("mcpx VP: [0x%llx] = 0x%llx\n", addr, val);
}


static const MemoryRegionOps mcpx_mmio_ops = {
    .read = mcpx_read,
    .write = mcpx_write,
};
static const MemoryRegionOps mcpx_vp_ops = {
    .read = mcpx_vp_read,
    .write = mcpx_vp_write,
};


static int mcpx_initfn(PCIDevice *dev)
{
    MCPXState *d = MCPX_DEVICE(dev);

    memory_region_init_io(&d->mmio, &mcpx_mmio_ops, d,
                          "mcpx-mmio", 0x80000);

    memory_region_init_io(&d->vp, &mcpx_vp_ops, d,
                          "mcpx-vp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x20000, &d->vp);

    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    return 0;
}

static void mcpx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX;
    k->revision = 210;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->init = mcpx_initfn;

    dc->desc = "MCPX Audio Processing Unit";
}

static const TypeInfo mcpx_info = {
    .name          = "mcpx",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXState),
    .class_init    = mcpx_class_init,
};

static void mcpx_register(void)
{
    type_register_static(&mcpx_info);
}
type_init(mcpx_register);



void mcpx_init(PCIBus *bus, int devfn, qemu_irq irq)
{
    PCIDevice *dev;
    MCPXState *d;
    dev = pci_create_simple(bus, devfn, "mcpx");
    d = MCPX_DEVICE(dev);
    d->irq = irq;
}