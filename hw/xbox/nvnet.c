/*
 * QEMU nForce Ethernet Controller implementation
 *
 * Copyright (c) 2013 espes
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
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"


#define IOPORT_SIZE 0x8
#define MMIO_SIZE 0x400


//#define DEBUG
#ifdef DEBUG
# define NVNET_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define NVNET_DPRINTF(format, ...)       do { } while (0)
#endif

typedef struct NVNetState {
    PCIDevice dev;
    MemoryRegion mmio, io;
} NVNetState;

#define NVNET_DEVICE(obj) \
    OBJECT_CHECK(NVNetState, (obj), "nvnet")


static uint64_t nvnet_mmio_read(void *opaque,
                                hwaddr addr, unsigned int size)
{
    NVNET_DPRINTF("nvnet MMIO: read [0x%llx]\n", addr);
    return 0;
}
static void nvnet_mmio_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    NVNET_DPRINTF("nvnet MMIO: [0x%llx] = 0x%llx\n", addr, val);
}
static const MemoryRegionOps nvnet_mmio_ops = {
    .read = nvnet_mmio_read,
    .write = nvnet_mmio_write,
};


static uint64_t nvnet_io_read(void *opaque,
                              hwaddr addr, unsigned int size)
{
    NVNET_DPRINTF("nvnet IO: read [0x%llx]\n", addr);
    return 0;
}
static void nvnet_io_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    NVNET_DPRINTF("nvnet IO: [0x%llx] = 0x%llx\n", addr, val);
}
static const MemoryRegionOps nvnet_io_ops = {
	.read = nvnet_io_read,
	.write = nvnet_io_write,
};

static int nvnet_initfn(PCIDevice *dev)
{
    NVNetState *d = NVNET_DEVICE(dev);

    memory_region_init_io(&d->mmio, OBJECT(dev),
                          &nvnet_mmio_ops, d, "nvnet-mmio", MMIO_SIZE);
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    memory_region_init_io(&d->io, OBJECT(dev), 
                          &nvnet_io_ops, d, "nvnet-io", IOPORT_SIZE);
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->io);

    return 0;
}

static void nvnet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_NVENET_1;
    k->revision = 210;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    k->init = nvnet_initfn;

    dc->desc = "nForce Ethernet Controller";
}

static const TypeInfo nvnet_info = {
    .name          = "nvnet",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NVNetState),
    .class_init    = nvnet_class_init,
};

static void nvnet_register(void)
{
    type_register_static(&nvnet_info);
}
type_init(nvnet_register);