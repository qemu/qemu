/*
 * QEMU Grackle PCI host (heathrow OldWorld PowerMac)
 *
 * Copyright (c) 2006-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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

#include "sysbus.h"
#include "ppc_mac.h"
#include "pci.h"
#include "pci_host.h"

/* debug Grackle */
//#define DEBUG_GRACKLE

#ifdef DEBUG_GRACKLE
#define GRACKLE_DPRINTF(fmt, ...)                               \
    do { printf("GRACKLE: " fmt , ## __VA_ARGS__); } while (0)
#else
#define GRACKLE_DPRINTF(fmt, ...)
#endif

typedef struct GrackleState {
    SysBusDevice busdev;
    PCIHostState host_state;
} GrackleState;

/* Don't know if this matches real hardware, but it agrees with OHW.  */
static int pci_grackle_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_grackle_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    GRACKLE_DPRINTF("set_irq num %d level %d\n", irq_num, level);
    qemu_set_irq(pic[irq_num + 0x15], level);
}

static void pci_grackle_reset(void *opaque)
{
}

PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io)
{
    DeviceState *dev;
    SysBusDevice *s;
    GrackleState *d;

    dev = qdev_create(NULL, "grackle");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    d = FROM_SYSBUS(GrackleState, s);
    d->host_state.bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_grackle_set_irq,
                                         pci_grackle_map_irq,
                                         pic,
                                         address_space_mem,
                                         address_space_io,
                                         0, 4);

    pci_create_simple(d->host_state.bus, 0, "grackle");

    sysbus_mmio_map(s, 0, base);
    sysbus_mmio_map(s, 1, base + 0x00200000);

    return d->host_state.bus;
}

static int pci_grackle_init_device(SysBusDevice *dev)
{
    GrackleState *s;
    int pci_mem_config, pci_mem_data;

    s = FROM_SYSBUS(GrackleState, dev);

    pci_mem_config = pci_host_conf_register_mmio(&s->host_state,
                                                 DEVICE_LITTLE_ENDIAN);
    pci_mem_data = pci_host_data_register_mmio(&s->host_state,
                                               DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);

    qemu_register_reset(pci_grackle_reset, &s->host_state);
    return 0;
}

static int grackle_pci_host_init(PCIDevice *d)
{
    d->config[0x09] = 0x01;
    return 0;
}

static PCIDeviceInfo grackle_pci_host_info = {
    .qdev.name = "grackle",
    .qdev.size = sizeof(PCIDevice),
    .init      = grackle_pci_host_init,
    .vendor_id = PCI_VENDOR_ID_MOTOROLA,
    .device_id = PCI_DEVICE_ID_MOTOROLA_MPC106,
    .revision  = 0x00,
    .class_id  = PCI_CLASS_BRIDGE_HOST,
};

static void grackle_register_devices(void)
{
    sysbus_register_dev("grackle", sizeof(GrackleState),
                        pci_grackle_init_device);
    pci_qdev_register(&grackle_pci_host_info);
}

device_init(grackle_register_devices)
