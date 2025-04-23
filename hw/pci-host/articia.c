/*
 * Mai Logic Articia S emulation
 *
 * Copyright (c) 2023 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "hw/irq.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/intc/i8259.h"
#include "hw/pci-host/articia.h"

/*
 * This is a minimal emulation of this chip as used in AmigaOne board.
 * Most features are missing but those are not needed by firmware and guests.
 */

OBJECT_DECLARE_SIMPLE_TYPE(ArticiaState, ARTICIA)

OBJECT_DECLARE_SIMPLE_TYPE(ArticiaHostState, ARTICIA_PCI_HOST)
struct ArticiaHostState {
    PCIDevice parent_obj;

    ArticiaState *as;
};

/* TYPE_ARTICIA */

struct ArticiaState {
    PCIHostState parent_obj;

    qemu_irq irq[PCI_NUM_PINS];
    MemoryRegion io;
    MemoryRegion mem;
    MemoryRegion reg;

    bitbang_i2c_interface smbus;
    uint32_t gpio; /* bits 0-7 in, 8-15 out, 16-23 direction (0 in, 1 out) */
    hwaddr gpio_base;
    MemoryRegion gpio_reg;
};

static uint64_t articia_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    ArticiaState *s = opaque;

    return (s->gpio >> (addr * 8)) & 0xff;
}

static void articia_gpio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    ArticiaState *s = opaque;
    uint32_t sh = addr * 8;

    if (addr == 0) {
        /* in bits read only? */
        return;
    }

    if ((s->gpio & (0xff << sh)) != (val & 0xff) << sh) {
        s->gpio &= ~(0xff << sh | 0xff);
        s->gpio |= (val & 0xff) << sh;
        s->gpio |= bitbang_i2c_set(&s->smbus, BITBANG_I2C_SDA,
                                   s->gpio & BIT(16) ?
                                   !!(s->gpio & BIT(8)) : 1);
        if ((s->gpio & BIT(17))) {
            s->gpio &= ~BIT(0);
            s->gpio |= bitbang_i2c_set(&s->smbus, BITBANG_I2C_SCL,
                                       !!(s->gpio & BIT(9)));
        }
    }
}

static const MemoryRegionOps articia_gpio_ops = {
    .read = articia_gpio_read,
    .write = articia_gpio_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t articia_reg_read(void *opaque, hwaddr addr, unsigned int size)
{
    ArticiaState *s = opaque;
    uint64_t ret = UINT_MAX;

    switch (addr) {
    case 0xc00cf8:
        ret = pci_host_conf_le_ops.read(PCI_HOST_BRIDGE(s), 0, size);
        break;
    case 0xe00cfc ... 0xe00cff:
        ret = pci_host_data_le_ops.read(PCI_HOST_BRIDGE(s), addr - 0xe00cfc, size);
        break;
    case 0xf00000:
        ret = pic_read_irq(isa_pic);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented register read 0x%"
                      HWADDR_PRIx " %d\n", __func__, addr, size);
        break;
    }
    return ret;
}

static void articia_reg_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned int size)
{
    ArticiaState *s = opaque;

    switch (addr) {
    case 0xc00cf8:
        pci_host_conf_le_ops.write(PCI_HOST_BRIDGE(s), 0, val, size);
        break;
    case 0xe00cfc ... 0xe00cff:
        pci_host_data_le_ops.write(PCI_HOST_BRIDGE(s), addr, val, size);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented register write 0x%"
                      HWADDR_PRIx " %d <- %"PRIx64"\n", __func__, addr, size, val);
        break;
    }
}

static const MemoryRegionOps articia_reg_ops = {
    .read = articia_reg_read,
    .write = articia_reg_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void articia_pcihost_set_irq(void *opaque, int n, int level)
{
    ArticiaState *s = opaque;
    qemu_set_irq(s->irq[n], level);
}

/*
 * AmigaOne SE PCI slot to IRQ routing
 *
 * repository: https://source.denx.de/u-boot/custodians/u-boot-avr32.git
 * refspec: v2010.06
 * file: board/MAI/AmigaOneG3SE/articiaS_pci.c
 */
static int amigaone_pcihost_bus0_map_irq(PCIDevice *pdev, int pin)
{
    int devfn_slot = PCI_SLOT(pdev->devfn);

    switch (devfn_slot) {
    case 6:  /* On board ethernet */
        return 3;
    case 7:  /* South bridge */
        return pin;
    default: /* PCI Slot 1 Devfn slot 8, Slot 2 Devfn 9, Slot 3 Devfn 10 */
        return pci_swizzle(devfn_slot, pin);
    }

}

static void articia_realize(DeviceState *dev, Error **errp)
{
    ArticiaState *s = ARTICIA(dev);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    PCIDevice *pdev;

    bitbang_i2c_init(&s->smbus, i2c_init_bus(dev, "smbus"));
    memory_region_init_io(&s->gpio_reg, OBJECT(s), &articia_gpio_ops, s,
                          TYPE_ARTICIA, 4);

    memory_region_init(&s->mem, OBJECT(dev), "pci-mem", UINT64_MAX);
    memory_region_init(&s->io, OBJECT(dev), "pci-io", 0xc00000);
    memory_region_init_io(&s->reg, OBJECT(s), &articia_reg_ops, s,
                          TYPE_ARTICIA, 0x1000000);
    memory_region_add_subregion_overlap(&s->reg, 0, &s->io, 1);

    /* devfn_min is 8 that matches first PCI slot in AmigaOne */
    h->bus = pci_register_root_bus(dev, NULL, articia_pcihost_set_irq,
                                   amigaone_pcihost_bus0_map_irq, dev, &s->mem,
                                   &s->io, PCI_DEVFN(8, 0), 4, TYPE_PCI_BUS);
    pdev = pci_create_simple_multifunction(h->bus, PCI_DEVFN(0, 0),
                                           TYPE_ARTICIA_PCI_HOST);
    ARTICIA_PCI_HOST(pdev)->as = s;
    pci_create_simple(h->bus, PCI_DEVFN(0, 1), TYPE_ARTICIA_PCI_BRIDGE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->reg);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mem);
    qdev_init_gpio_out(dev, s->irq, ARRAY_SIZE(s->irq));
}

static void articia_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = articia_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

/* TYPE_ARTICIA_PCI_HOST */

static void articia_pci_host_cfg_write(PCIDevice *d, uint32_t addr,
                                       uint32_t val, int len)
{
    ArticiaState *s = ARTICIA_PCI_HOST(d)->as;

    pci_default_write_config(d, addr, val, len);
    switch (addr) {
    case 0x40:
        s->gpio_base = val;
        break;
    case 0x44:
        if (val != 0x11) {
            /* FIXME what do the bits actually mean? */
            break;
        }
        if (memory_region_is_mapped(&s->gpio_reg)) {
            memory_region_del_subregion(&s->io, &s->gpio_reg);
        }
        memory_region_add_subregion(&s->io, s->gpio_base + 0x38, &s->gpio_reg);
        break;
    }
}

static void articia_pci_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = articia_pci_host_cfg_write;
    k->vendor_id = 0x10cc;
    k->device_id = 0x0660;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge,
     * not usable without the host-facing part
     */
    dc->user_creatable = false;
}

/* TYPE_ARTICIA_PCI_BRIDGE */

static void articia_pci_bridge_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = 0x10cc;
    k->device_id = 0x0661;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge,
     * not usable without the host-facing part
     */
    dc->user_creatable = false;
}

static const TypeInfo articia_types[] = {
    {
        .name          = TYPE_ARTICIA,
        .parent        = TYPE_PCI_HOST_BRIDGE,
        .instance_size = sizeof(ArticiaState),
        .class_init    = articia_class_init,
    },
    {
        .name          = TYPE_ARTICIA_PCI_HOST,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(ArticiaHostState),
        .class_init    = articia_pci_host_class_init,
        .interfaces = (const InterfaceInfo[]) {
              { INTERFACE_CONVENTIONAL_PCI_DEVICE },
              { },
        },
    },
    {
        .name          = TYPE_ARTICIA_PCI_BRIDGE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIDevice),
        .class_init    = articia_pci_bridge_class_init,
        .interfaces = (const InterfaceInfo[]) {
              { INTERFACE_CONVENTIONAL_PCI_DEVICE },
              { },
        },
    },
};

DEFINE_TYPES(articia_types)
