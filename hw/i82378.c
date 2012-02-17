/*
 * QEMU Intel i82378 emulation (PCI to ISA bridge)
 *
 * Copyright (c) 2010-2011 Hervé Poussineau
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

#include "pci.h"
#include "pc.h"
#include "i8254.h"
#include "pcspk.h"

//#define DEBUG_I82378

#ifdef DEBUG_I82378
#define DPRINTF(fmt, ...) \
do { fprintf(stderr, "i82378: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
do {} while (0)
#endif

#define BADF(fmt, ...) \
do { fprintf(stderr, "i82378 ERROR: " fmt , ## __VA_ARGS__); } while (0)

typedef struct I82378State {
    qemu_irq out[2];
    qemu_irq *i8259;
    MemoryRegion io;
    MemoryRegion mem;
} I82378State;

typedef struct PCIi82378State {
    PCIDevice pci_dev;
    uint32_t isa_io_base;
    uint32_t isa_mem_base;
    I82378State state;
} PCIi82378State;

static const VMStateDescription vmstate_pci_i82378 = {
    .name = "pci-i82378",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pci_dev, PCIi82378State),
        VMSTATE_END_OF_LIST()
    },
};

static void i82378_io_write(void *opaque, target_phys_addr_t addr,
                            uint64_t value, unsigned int size)
{
    switch (size) {
    case 1:
        DPRINTF("%s: " TARGET_FMT_plx "=%02" PRIx64 "\n", __func__,
                addr, value);
        cpu_outb(addr, value);
        break;
    case 2:
        DPRINTF("%s: " TARGET_FMT_plx "=%04" PRIx64 "\n", __func__,
                addr, value);
        cpu_outw(addr, value);
        break;
    case 4:
        DPRINTF("%s: " TARGET_FMT_plx "=%08" PRIx64 "\n", __func__,
                addr, value);
        cpu_outl(addr, value);
        break;
    default:
        abort();
    }
}

static uint64_t i82378_io_read(void *opaque, target_phys_addr_t addr,
                               unsigned int size)
{
    DPRINTF("%s: " TARGET_FMT_plx "\n", __func__, addr);
    switch (size) {
    case 1:
        return cpu_inb(addr);
    case 2:
        return cpu_inw(addr);
    case 4:
        return cpu_inl(addr);
    default:
        abort();
    }
}

static const MemoryRegionOps i82378_io_ops = {
    .read = i82378_io_read,
    .write = i82378_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void i82378_mem_write(void *opaque, target_phys_addr_t addr,
                             uint64_t value, unsigned int size)
{
    switch (size) {
    case 1:
        DPRINTF("%s: " TARGET_FMT_plx "=%02" PRIx64 "\n", __func__,
                addr, value);
        cpu_outb(addr, value);
        break;
    case 2:
        DPRINTF("%s: " TARGET_FMT_plx "=%04" PRIx64 "\n", __func__,
                addr, value);
        cpu_outw(addr, value);
        break;
    case 4:
        DPRINTF("%s: " TARGET_FMT_plx "=%08" PRIx64 "\n", __func__,
                addr, value);
        cpu_outl(addr, value);
        break;
    default:
        abort();
    }
}

static uint64_t i82378_mem_read(void *opaque, target_phys_addr_t addr,
                                unsigned int size)
{
    DPRINTF("%s: " TARGET_FMT_plx "\n", __func__, addr);
    switch (size) {
    case 1:
        return cpu_inb(addr);
    case 2:
        return cpu_inw(addr);
    case 4:
        return cpu_inl(addr);
    default:
        abort();
    }
}

static const MemoryRegionOps i82378_mem_ops = {
    .read = i82378_mem_read,
    .write = i82378_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void i82378_request_out0_irq(void *opaque, int irq, int level)
{
    I82378State *s = opaque;
    qemu_set_irq(s->out[0], level);
}

static void i82378_request_pic_irq(void *opaque, int irq, int level)
{
    DeviceState *dev = opaque;
    PCIDevice *pci = DO_UPCAST(PCIDevice, qdev, dev);
    PCIi82378State *s = DO_UPCAST(PCIi82378State, pci_dev, pci);

    qemu_set_irq(s->state.i8259[irq], level);
}

static void i82378_init(DeviceState *dev, I82378State *s)
{
    ISABus *isabus = DO_UPCAST(ISABus, qbus, qdev_get_child_bus(dev, "isa.0"));
    ISADevice *pit;
    qemu_irq *out0_irq;

    /* This device has:
       2 82C59 (irq)
       1 82C54 (pit)
       2 82C37 (dma)
       NMI
       Utility Bus Support Registers

       All devices accept byte access only, except timer
     */

    qdev_init_gpio_out(dev, s->out, 2);
    qdev_init_gpio_in(dev, i82378_request_pic_irq, 16);

    /* Workaround the fact that i8259 is not qdev'ified... */
    out0_irq = qemu_allocate_irqs(i82378_request_out0_irq, s, 1);

    /* 2 82C59 (irq) */
    s->i8259 = i8259_init(isabus, *out0_irq);
    isa_bus_irqs(isabus, s->i8259);

    /* 1 82C54 (pit) */
    pit = pit_init(isabus, 0x40, 0, NULL);

    /* speaker */
    pcspk_init(isabus, pit);

    /* 2 82C37 (dma) */
    DMA_init(1, &s->out[1]);
    isa_create_simple(isabus, "i82374");

    /* timer */
    isa_create_simple(isabus, "mc146818rtc");
}

static int pci_i82378_init(PCIDevice *dev)
{
    PCIi82378State *pci = DO_UPCAST(PCIi82378State, pci_dev, dev);
    I82378State *s = &pci->state;
    uint8_t *pci_conf;

    pci_conf = dev->config;
    pci_set_word(pci_conf + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_set_word(pci_conf + PCI_STATUS,
                 PCI_STATUS_DEVSEL_MEDIUM);

    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin 0 */

    memory_region_init_io(&s->io, &i82378_io_ops, s, "i82378-io", 0x00010000);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->io);

    memory_region_init_io(&s->mem, &i82378_mem_ops, s, "i82378-mem", 0x01000000);
    memory_region_set_coalescing(&s->mem);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mem);

    /* Make I/O address read only */
    pci_set_word(dev->wmask + PCI_COMMAND, PCI_COMMAND_SPECIAL);
    pci_set_long(dev->wmask + PCI_BASE_ADDRESS_0, 0);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, pci->isa_io_base);

    isa_mem_base = pci->isa_mem_base;
    isa_bus_new(&dev->qdev, pci_address_space_io(dev));

    i82378_init(&dev->qdev, s);

    return 0;
}

static Property i82378_properties[] = {
    DEFINE_PROP_HEX32("iobase", PCIi82378State, isa_io_base, 0x80000000),
    DEFINE_PROP_HEX32("membase", PCIi82378State, isa_mem_base, 0xc0000000),
    DEFINE_PROP_END_OF_LIST()
};

static void pci_i82378_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init = pci_i82378_init;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82378;
    k->revision = 0x03;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    k->subsystem_vendor_id = 0x0;
    k->subsystem_id = 0x0;
    dc->vmsd = &vmstate_pci_i82378;
    dc->props = i82378_properties;
}

static TypeInfo pci_i82378_info = {
    .name = "i82378",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIi82378State),
    .class_init = pci_i82378_class_init,
};

static void i82378_register_types(void)
{
    type_register_static(&pci_i82378_info);
}

type_init(i82378_register_types)
