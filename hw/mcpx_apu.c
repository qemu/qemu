/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw.h"
#include "pc.h"
#include "pci/pci.h"

#include "mcpx_apu.h"


//#define DEBUG
#ifdef DEBUG
# define MCPX_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define MCPX_DPRINTF(format, ...)       do { } while (0)
#endif


typedef struct MCPXAPUState {
    PCIDevice dev;

    qemu_irq irq;

    MemoryRegion mmio;
    MemoryRegion vp;
} MCPXAPUState;


#define MCPX_APU_DEVICE(obj) \
    OBJECT_CHECK(MCPXAPUState, (obj), "mcpx-apu")


static uint64_t mcpx_apu_read(void *opaque,
                          hwaddr addr, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu: read [0x%llx]\n", addr);
    return 0;
}
static void mcpx_apu_write(void *opaque, hwaddr addr,
                       uint64_t val, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu: [0x%llx] = 0x%llx\n", addr, val);
}


/* Voice Processor */
static uint64_t mcpx_apu_vp_read(void *opaque,
                             hwaddr addr, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu VP: read [0x%llx]\n", addr);
    switch (addr) {
    case 0x10: /* instruction queue free space */
        return 0x80;
    default:
        break;
    }
    return 0;
}
static void mcpx_apu_vp_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu VP: [0x%llx] = 0x%llx\n", addr, val);
}


static const MemoryRegionOps mcpx_apu_mmio_ops = {
    .read = mcpx_apu_read,
    .write = mcpx_apu_write,
};
static const MemoryRegionOps mcpx_apu_vp_ops = {
    .read = mcpx_apu_vp_read,
    .write = mcpx_apu_vp_write,
};


static int mcpx_apu_initfn(PCIDevice *dev)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    memory_region_init_io(&d->mmio, &mcpx_apu_mmio_ops, d,
                          "mcpx-apu-mmio", 0x80000);

    memory_region_init_io(&d->vp, &mcpx_apu_vp_ops, d,
                          "mcpx-apu-vp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x20000, &d->vp);

    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    return 0;
}

static void mcpx_apu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_APU;
    k->revision = 210;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->init = mcpx_apu_initfn;

    dc->desc = "MCPX Audio Processing Unit";
}

static const TypeInfo mcpx_apu_info = {
    .name          = "mcpx-apu",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXAPUState),
    .class_init    = mcpx_apu_class_init,
};

static void mcpx_apu_register(void)
{
    type_register_static(&mcpx_apu_info);
}
type_init(mcpx_apu_register);



void mcpx_apu_init(PCIBus *bus, int devfn, qemu_irq irq)
{
    PCIDevice *dev;
    MCPXAPUState *d;
    dev = pci_create_simple(bus, devfn, "mcpx-apu");
    d = MCPX_APU_DEVICE(dev);
    d->irq = irq;
}