/*
 * QEMU PREP PCI host
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2011-2013 Andreas Färber
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
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/intc/i8259.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/or-irq.h"
#include "elf.h"
#include "qom/object.h"

#define TYPE_RAVEN_PCI_DEVICE "raven"
#define TYPE_RAVEN_PCI_HOST_BRIDGE "raven-pcihost"

OBJECT_DECLARE_SIMPLE_TYPE(RavenPCIState, RAVEN_PCI_DEVICE)

struct RavenPCIState {
    PCIDevice dev;

    uint32_t elf_machine;
    char *bios_name;
    MemoryRegion bios;
};

typedef struct PRePPCIState PREPPCIState;
DECLARE_INSTANCE_CHECKER(PREPPCIState, RAVEN_PCI_HOST_BRIDGE,
                         TYPE_RAVEN_PCI_HOST_BRIDGE)

struct PRePPCIState {
    PCIHostState parent_obj;

    OrIRQState *or_irq;
    qemu_irq pci_irqs[PCI_NUM_PINS];
    PCIBus pci_bus;
    AddressSpace pci_io_as;
    MemoryRegion pci_io;
    MemoryRegion pci_io_non_contiguous;
    MemoryRegion pci_memory;
    MemoryRegion pci_intack;
    MemoryRegion bm;
    MemoryRegion bm_ram_alias;
    MemoryRegion bm_pci_memory_alias;
    AddressSpace bm_as;
    RavenPCIState pci_dev;

    int contiguous_map;
    bool is_legacy_prep;
};

#define BIOS_SIZE (1 * MiB)

#define PCI_IO_BASE_ADDR    0x80000000  /* Physical address on main bus */

static inline uint32_t raven_pci_io_config(hwaddr addr)
{
    int i;

    for (i = 0; i < 11; i++) {
        if ((addr & (1 << (11 + i))) != 0) {
            break;
        }
    }
    return (addr & 0x7ff) |  (i << 11);
}

static void raven_pci_io_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    PREPPCIState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    pci_data_write(phb->bus, raven_pci_io_config(addr), val, size);
}

static uint64_t raven_pci_io_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    PREPPCIState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    return pci_data_read(phb->bus, raven_pci_io_config(addr), size);
}

static const MemoryRegionOps raven_pci_io_ops = {
    .read = raven_pci_io_read,
    .write = raven_pci_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t raven_intack_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    return pic_read_irq(isa_pic);
}

static void raven_intack_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s not implemented\n", __func__);
}

static const MemoryRegionOps raven_intack_ops = {
    .read = raven_intack_read,
    .write = raven_intack_write,
    .valid = {
        .max_access_size = 1,
    },
};

static inline hwaddr raven_io_address(PREPPCIState *s,
                                      hwaddr addr)
{
    if (s->contiguous_map == 0) {
        /* 64 KB contiguous space for IOs */
        addr &= 0xFFFF;
    } else {
        /* 8 MB non-contiguous space for IOs */
        addr = (addr & 0x1F) | ((addr & 0x007FFF000) >> 7);
    }

    /* FIXME: handle endianness switch */

    return addr;
}

static uint64_t raven_io_read(void *opaque, hwaddr addr,
                              unsigned int size)
{
    PREPPCIState *s = opaque;
    uint8_t buf[4];

    addr = raven_io_address(s, addr);
    address_space_read(&s->pci_io_as, addr + PCI_IO_BASE_ADDR,
                       MEMTXATTRS_UNSPECIFIED, buf, size);

    if (size == 1) {
        return buf[0];
    } else if (size == 2) {
        return lduw_le_p(buf);
    } else if (size == 4) {
        return ldl_le_p(buf);
    } else {
        g_assert_not_reached();
    }
}

static void raven_io_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    PREPPCIState *s = opaque;
    uint8_t buf[4];

    addr = raven_io_address(s, addr);

    if (size == 1) {
        buf[0] = val;
    } else if (size == 2) {
        stw_le_p(buf, val);
    } else if (size == 4) {
        stl_le_p(buf, val);
    } else {
        g_assert_not_reached();
    }

    address_space_write(&s->pci_io_as, addr + PCI_IO_BASE_ADDR,
                        MEMTXATTRS_UNSPECIFIED, buf, size);
}

static const MemoryRegionOps raven_io_ops = {
    .read = raven_io_read,
    .write = raven_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size = 4,
    .impl.unaligned = true,
    .valid.unaligned = true,
};

static int raven_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 1;
}

static void raven_set_irq(void *opaque, int irq_num, int level)
{
    PREPPCIState *s = opaque;

    qemu_set_irq(s->pci_irqs[irq_num], level);
}

static AddressSpace *raven_pcihost_set_iommu(PCIBus *bus, void *opaque,
                                             int devfn)
{
    PREPPCIState *s = opaque;

    return &s->bm_as;
}

static const PCIIOMMUOps raven_iommu_ops = {
    .get_address_space = raven_pcihost_set_iommu,
};

static void raven_change_gpio(void *opaque, int n, int level)
{
    PREPPCIState *s = opaque;

    s->contiguous_map = level;
}

static void raven_pcihost_realizefn(DeviceState *d, Error **errp)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    PREPPCIState *s = RAVEN_PCI_HOST_BRIDGE(dev);
    MemoryRegion *address_space_mem = get_system_memory();
    int i;

    if (s->is_legacy_prep) {
        for (i = 0; i < PCI_NUM_PINS; i++) {
            sysbus_init_irq(dev, &s->pci_irqs[i]);
        }
    } else {
        /* According to PReP specification section 6.1.6 "System Interrupt
         * Assignments", all PCI interrupts are routed via IRQ 15 */
        s->or_irq = OR_IRQ(object_new(TYPE_OR_IRQ));
        object_property_set_int(OBJECT(s->or_irq), "num-lines", PCI_NUM_PINS,
                                &error_fatal);
        qdev_realize(DEVICE(s->or_irq), NULL, &error_fatal);
        sysbus_init_irq(dev, &s->or_irq->out_irq);

        for (i = 0; i < PCI_NUM_PINS; i++) {
            s->pci_irqs[i] = qdev_get_gpio_in(DEVICE(s->or_irq), i);
        }
    }

    qdev_init_gpio_in(d, raven_change_gpio, 1);

    pci_bus_irqs(&s->pci_bus, raven_set_irq, s, PCI_NUM_PINS);
    pci_bus_map_irqs(&s->pci_bus, raven_map_irq);

    memory_region_init_io(&h->conf_mem, OBJECT(h), &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4);
    memory_region_add_subregion(&s->pci_io, 0xcf8, &h->conf_mem);

    memory_region_init_io(&h->data_mem, OBJECT(h), &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);
    memory_region_add_subregion(&s->pci_io, 0xcfc, &h->data_mem);

    memory_region_init_io(&h->mmcfg, OBJECT(s), &raven_pci_io_ops, s,
                          "pciio", 0x00400000);
    memory_region_add_subregion(address_space_mem, 0x80800000, &h->mmcfg);

    memory_region_init_io(&s->pci_intack, OBJECT(s), &raven_intack_ops, s,
                          "pci-intack", 1);
    memory_region_add_subregion(address_space_mem, 0xbffffff0, &s->pci_intack);

    /* TODO Remove once realize propagates to child devices. */
    qdev_realize(DEVICE(&s->pci_dev), BUS(&s->pci_bus), errp);
}

static void raven_pcihost_initfn(Object *obj)
{
    PCIHostState *h = PCI_HOST_BRIDGE(obj);
    PREPPCIState *s = RAVEN_PCI_HOST_BRIDGE(obj);
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *pci_dev;

    memory_region_init(&s->pci_io, obj, "pci-io", 0x3f800000);
    memory_region_init_io(&s->pci_io_non_contiguous, obj, &raven_io_ops, s,
                          "pci-io-non-contiguous", 0x00800000);
    memory_region_init(&s->pci_memory, obj, "pci-memory", 0x3f000000);
    address_space_init(&s->pci_io_as, &s->pci_io, "raven-io");

    /*
     * Raven's raven_io_ops use the address-space API to access pci-conf-idx
     * (which is also owned by the raven device). As such, mark the
     * pci_io_non_contiguous as re-entrancy safe.
     */
    s->pci_io_non_contiguous.disable_reentrancy_guard = true;

    /* CPU address space */
    memory_region_add_subregion(address_space_mem, PCI_IO_BASE_ADDR,
                                &s->pci_io);
    memory_region_add_subregion_overlap(address_space_mem, PCI_IO_BASE_ADDR,
                                        &s->pci_io_non_contiguous, 1);
    memory_region_add_subregion(address_space_mem, 0xc0000000, &s->pci_memory);
    pci_root_bus_init(&s->pci_bus, sizeof(s->pci_bus), DEVICE(obj), NULL,
                      &s->pci_memory, &s->pci_io, 0, TYPE_PCI_BUS);

    /* Bus master address space */
    memory_region_init(&s->bm, obj, "bm-raven", 4 * GiB);
    memory_region_init_alias(&s->bm_pci_memory_alias, obj, "bm-pci-memory",
                             &s->pci_memory, 0,
                             memory_region_size(&s->pci_memory));
    memory_region_init_alias(&s->bm_ram_alias, obj, "bm-system",
                             get_system_memory(), 0, 0x80000000);
    memory_region_add_subregion(&s->bm, 0         , &s->bm_pci_memory_alias);
    memory_region_add_subregion(&s->bm, 0x80000000, &s->bm_ram_alias);
    address_space_init(&s->bm_as, &s->bm, "raven-bm");
    pci_setup_iommu(&s->pci_bus, &raven_iommu_ops, s);

    h->bus = &s->pci_bus;

    object_initialize(&s->pci_dev, sizeof(s->pci_dev), TYPE_RAVEN_PCI_DEVICE);
    pci_dev = DEVICE(&s->pci_dev);
    object_property_set_int(OBJECT(&s->pci_dev), "addr", PCI_DEVFN(0, 0),
                            NULL);
    qdev_prop_set_bit(pci_dev, "multifunction", false);
}

static void raven_realize(PCIDevice *d, Error **errp)
{
    RavenPCIState *s = RAVEN_PCI_DEVICE(d);
    char *filename;
    int bios_size = -1;

    d->config[PCI_CACHE_LINE_SIZE] = 0x08;
    d->config[PCI_LATENCY_TIMER] = 0x10;
    d->config[PCI_CAPABILITY_LIST] = 0x00;

    if (!memory_region_init_rom_nomigrate(&s->bios, OBJECT(s), "bios",
                                          BIOS_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), (uint32_t)(-BIOS_SIZE),
                                &s->bios);
    if (s->bios_name) {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, s->bios_name);
        if (filename) {
            if (s->elf_machine != EM_NONE) {
                bios_size = load_elf(filename, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL,
                                     ELFDATA2MSB, s->elf_machine, 0, 0);
            }
            if (bios_size < 0) {
                bios_size = get_image_size(filename);
                if (bios_size > 0 && bios_size <= BIOS_SIZE) {
                    hwaddr bios_addr;
                    bios_size = (bios_size + 0xfff) & ~0xfff;
                    bios_addr = (uint32_t)(-BIOS_SIZE);
                    bios_size = load_image_targphys(filename, bios_addr,
                                                    bios_size);
                }
            }
        }
        g_free(filename);
        if (bios_size < 0 || bios_size > BIOS_SIZE) {
            memory_region_del_subregion(get_system_memory(), &s->bios);
            error_setg(errp, "Could not load bios image '%s'", s->bios_name);
            return;
        }
    }

    vmstate_register_ram_global(&s->bios);
}

static const VMStateDescription vmstate_raven = {
    .name = "raven",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, RavenPCIState),
        VMSTATE_END_OF_LIST()
    },
};

static void raven_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = raven_realize;
    k->vendor_id = PCI_VENDOR_ID_MOTOROLA;
    k->device_id = PCI_DEVICE_ID_MOTOROLA_RAVEN;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    dc->desc = "PReP Host Bridge - Motorola Raven";
    dc->vmsd = &vmstate_raven;
    /*
     * Reason: PCI-facing part of the host bridge, not usable without
     * the host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo raven_info = {
    .name = TYPE_RAVEN_PCI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RavenPCIState),
    .class_init = raven_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const Property raven_pcihost_properties[] = {
    DEFINE_PROP_UINT32("elf-machine", PREPPCIState, pci_dev.elf_machine,
                       EM_NONE),
    DEFINE_PROP_STRING("bios-name", PREPPCIState, pci_dev.bios_name),
    /* Temporary workaround until legacy prep machine is removed */
    DEFINE_PROP_BOOL("is-legacy-prep", PREPPCIState, is_legacy_prep,
                     false),
};

static void raven_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->realize = raven_pcihost_realizefn;
    device_class_set_props(dc, raven_pcihost_properties);
    dc->fw_name = "pci";
}

static const TypeInfo raven_pcihost_info = {
    .name = TYPE_RAVEN_PCI_HOST_BRIDGE,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PREPPCIState),
    .instance_init = raven_pcihost_initfn,
    .class_init = raven_pcihost_class_init,
};

static void raven_register_types(void)
{
    type_register_static(&raven_pcihost_info);
    type_register_static(&raven_info);
}

type_init(raven_register_types)
