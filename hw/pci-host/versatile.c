/*
 * ARM Versatile/PB PCI host controller
 *
 * Copyright (c) 2006-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "exec/address-spaces.h"

/* Old and buggy versions of QEMU used the wrong mapping from
 * PCI IRQs to system interrupt lines. Unfortunately the Linux
 * kernel also had the corresponding bug in setting up interrupts
 * (so older kernels work on QEMU and not on real hardware).
 * We automatically detect these broken kernels and flip back
 * to the broken irq mapping by spotting guest writes to the
 * PCI_INTERRUPT_LINE register to see where the guest thinks
 * interrupts are going to be routed. So we start in state
 * ASSUME_OK on reset, and transition to either BROKEN or
 * FORCE_OK at the first write to an INTERRUPT_LINE register for
 * a slot where broken and correct interrupt mapping would differ.
 * Once in either BROKEN or FORCE_OK we never transition again;
 * this allows a newer kernel to use the INTERRUPT_LINE
 * registers arbitrarily once it has indicated that it isn't
 * broken in its init code somewhere.
 */
enum {
    PCI_VPB_IRQMAP_ASSUME_OK,
    PCI_VPB_IRQMAP_BROKEN,
    PCI_VPB_IRQMAP_FORCE_OK,
};

typedef struct {
    PCIHostState parent_obj;

    qemu_irq irq[4];
    MemoryRegion controlregs;
    MemoryRegion mem_config;
    MemoryRegion mem_config2;
    MemoryRegion pci_io_space;
    MemoryRegion pci_io_window;
    PCIBus pci_bus;
    PCIDevice pci_dev;

    /* Constant for life of device: */
    int realview;

    /* Variable state: */
    uint32_t imap[3];
    uint32_t smap[3];
    uint32_t selfid;
    uint32_t flags;
    uint8_t irq_mapping;
} PCIVPBState;

static const VMStateDescription pci_vpb_vmstate = {
    .name = "versatile-pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(imap, PCIVPBState, 3),
        VMSTATE_UINT32_ARRAY(smap, PCIVPBState, 3),
        VMSTATE_UINT32(selfid, PCIVPBState),
        VMSTATE_UINT32(flags, PCIVPBState),
        VMSTATE_UINT8(irq_mapping, PCIVPBState),
        VMSTATE_END_OF_LIST()
    }
};

#define TYPE_VERSATILE_PCI "versatile_pci"
#define PCI_VPB(obj) \
    OBJECT_CHECK(PCIVPBState, (obj), TYPE_VERSATILE_PCI)

#define TYPE_VERSATILE_PCI_HOST "versatile_pci_host"
#define PCI_VPB_HOST(obj) \
    OBJECT_CHECK(PCIDevice, (obj), TYPE_VERSATILE_PCIHOST)

typedef enum {
    PCI_IMAP0 = 0x0,
    PCI_IMAP1 = 0x4,
    PCI_IMAP2 = 0x8,
    PCI_SELFID = 0xc,
    PCI_FLAGS = 0x10,
    PCI_SMAP0 = 0x14,
    PCI_SMAP1 = 0x18,
    PCI_SMAP2 = 0x1c,
} PCIVPBControlRegs;

static void pci_vpb_reg_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PCIVPBState *s = opaque;

    switch (addr) {
    case PCI_IMAP0:
    case PCI_IMAP1:
    case PCI_IMAP2:
    {
        int win = (addr - PCI_IMAP0) >> 2;
        s->imap[win] = val;
        break;
    }
    case PCI_SELFID:
        s->selfid = val;
        break;
    case PCI_FLAGS:
        s->flags = val;
        break;
    case PCI_SMAP0:
    case PCI_SMAP1:
    case PCI_SMAP2:
    {
        int win = (addr - PCI_SMAP0) >> 2;
        s->smap[win] = val;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pci_vpb_reg_write: Bad offset %x\n", (int)addr);
        break;
    }
}

static uint64_t pci_vpb_reg_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    PCIVPBState *s = opaque;

    switch (addr) {
    case PCI_IMAP0:
    case PCI_IMAP1:
    case PCI_IMAP2:
    {
        int win = (addr - PCI_IMAP0) >> 2;
        return s->imap[win];
    }
    case PCI_SELFID:
        return s->selfid;
    case PCI_FLAGS:
        return s->flags;
    case PCI_SMAP0:
    case PCI_SMAP1:
    case PCI_SMAP2:
    {
        int win = (addr - PCI_SMAP0) >> 2;
        return s->smap[win];
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pci_vpb_reg_read: Bad offset %x\n", (int)addr);
        return 0;
    }
}

static const MemoryRegionOps pci_vpb_reg_ops = {
    .read = pci_vpb_reg_read,
    .write = pci_vpb_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static inline uint32_t vpb_pci_config_addr(hwaddr addr)
{
    return addr & 0xffffff;
}

static void pci_vpb_config_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PCIVPBState *s = opaque;
    if (!s->realview && (addr & 0xff) == PCI_INTERRUPT_LINE
        && s->irq_mapping == PCI_VPB_IRQMAP_ASSUME_OK) {
        uint8_t devfn = addr >> 8;
        if ((PCI_SLOT(devfn) % PCI_NUM_PINS) != 2) {
            if (val == 27) {
                s->irq_mapping = PCI_VPB_IRQMAP_BROKEN;
            } else {
                s->irq_mapping = PCI_VPB_IRQMAP_FORCE_OK;
            }
        }
    }
    pci_data_write(&s->pci_bus, vpb_pci_config_addr(addr), val, size);
}

static uint64_t pci_vpb_config_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    PCIVPBState *s = opaque;
    uint32_t val;
    val = pci_data_read(&s->pci_bus, vpb_pci_config_addr(addr), size);
    return val;
}

static const MemoryRegionOps pci_vpb_config_ops = {
    .read = pci_vpb_config_read,
    .write = pci_vpb_config_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int pci_vpb_map_irq(PCIDevice *d, int irq_num)
{
    PCIVPBState *s = container_of(d->bus, PCIVPBState, pci_bus);

    if (s->irq_mapping == PCI_VPB_IRQMAP_BROKEN) {
        /* Legacy broken IRQ mapping for compatibility with old and
         * buggy Linux guests
         */
        return irq_num;
    }

    /* Slot to IRQ mapping for RealView Platform Baseboard 926 backplane
     *      name    slot    IntA    IntB    IntC    IntD
     *      A       31      IRQ28   IRQ29   IRQ30   IRQ27
     *      B       30      IRQ27   IRQ28   IRQ29   IRQ30
     *      C       29      IRQ30   IRQ27   IRQ28   IRQ29
     * Slot C is for the host bridge; A and B the peripherals.
     * Our output irqs 0..3 correspond to the baseboard's 27..30.
     *
     * This mapping function takes account of an oddity in the PB926
     * board wiring, where the FPGA's P_nINTA input is connected to
     * the INTB connection on the board PCI edge connector, P_nINTB
     * is connected to INTC, and so on, so everything is one number
     * further round from where you might expect.
     */
    return pci_swizzle_map_irq_fn(d, irq_num + 2);
}

static int pci_vpb_rv_map_irq(PCIDevice *d, int irq_num)
{
    /* Slot to IRQ mapping for RealView EB and PB1176 backplane
     *      name    slot    IntA    IntB    IntC    IntD
     *      A       31      IRQ50   IRQ51   IRQ48   IRQ49
     *      B       30      IRQ49   IRQ50   IRQ51   IRQ48
     *      C       29      IRQ48   IRQ49   IRQ50   IRQ51
     * Slot C is for the host bridge; A and B the peripherals.
     * Our output irqs 0..3 correspond to the baseboard's 48..51.
     *
     * The PB1176 and EB boards don't have the PB926 wiring oddity
     * described above; P_nINTA connects to INTA, P_nINTB to INTB
     * and so on, which is why this mapping function is different.
     */
    return pci_swizzle_map_irq_fn(d, irq_num + 3);
}

static void pci_vpb_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[irq_num], level);
}

static void pci_vpb_reset(DeviceState *d)
{
    PCIVPBState *s = PCI_VPB(d);

    s->imap[0] = 0;
    s->imap[1] = 0;
    s->imap[2] = 0;
    s->smap[0] = 0;
    s->smap[1] = 0;
    s->smap[2] = 0;
    s->selfid = 0;
    s->flags = 0;
    s->irq_mapping = PCI_VPB_IRQMAP_ASSUME_OK;
}

static void pci_vpb_init(Object *obj)
{
    PCIHostState *h = PCI_HOST_BRIDGE(obj);
    PCIVPBState *s = PCI_VPB(obj);

    memory_region_init(&s->pci_io_space, "pci_io", 1ULL << 32);

    pci_bus_new_inplace(&s->pci_bus, DEVICE(obj), "pci",
                        get_system_memory(), &s->pci_io_space,
                        PCI_DEVFN(11, 0), TYPE_PCI_BUS);
    h->bus = &s->pci_bus;

    object_initialize(&s->pci_dev, TYPE_VERSATILE_PCI_HOST);
    qdev_set_parent_bus(DEVICE(&s->pci_dev), BUS(&s->pci_bus));
    object_property_set_int(OBJECT(&s->pci_dev), PCI_DEVFN(29, 0), "addr",
                            NULL);
}

static void pci_vpb_realize(DeviceState *dev, Error **errp)
{
    PCIVPBState *s = PCI_VPB(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    pci_map_irq_fn mapfn;
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    if (s->realview) {
        mapfn = pci_vpb_rv_map_irq;
    } else {
        mapfn = pci_vpb_map_irq;
    }

    pci_bus_irqs(&s->pci_bus, pci_vpb_set_irq, mapfn, s->irq, 4);

    /* Our memory regions are:
     * 0 : our control registers
     * 1 : PCI self config window
     * 2 : PCI config window
     * 3 : PCI IO window
     */
    memory_region_init_io(&s->controlregs, &pci_vpb_reg_ops, s, "pci-vpb-regs",
                          0x1000);
    sysbus_init_mmio(sbd, &s->controlregs);
    memory_region_init_io(&s->mem_config, &pci_vpb_config_ops, s,
                          "pci-vpb-selfconfig", 0x1000000);
    sysbus_init_mmio(sbd, &s->mem_config);
    memory_region_init_io(&s->mem_config2, &pci_vpb_config_ops, s,
                          "pci-vpb-config", 0x1000000);
    sysbus_init_mmio(sbd, &s->mem_config2);

    /* The window into I/O space is always into a fixed base address;
     * its size is the same for both realview and versatile.
     */
    memory_region_init_alias(&s->pci_io_window, "pci-vbp-io-window",
                             &s->pci_io_space, 0, 0x100000);

    sysbus_init_mmio(sbd, &s->pci_io_space);

    /* TODO Remove once realize propagates to child devices. */
    object_property_set_bool(OBJECT(&s->pci_dev), true, "realized", errp);
}

static int versatile_pci_host_init(PCIDevice *d)
{
    pci_set_word(d->config + PCI_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_byte(d->config + PCI_LATENCY_TIMER, 0x10);
    return 0;
}

static void versatile_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = versatile_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = PCI_DEVICE_ID_XILINX_XC2VP30;
    k->class_id = PCI_CLASS_PROCESSOR_CO;
}

static const TypeInfo versatile_pci_host_info = {
    .name          = TYPE_VERSATILE_PCI_HOST,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = versatile_pci_host_class_init,
};

static void pci_vpb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pci_vpb_realize;
    dc->reset = pci_vpb_reset;
    dc->vmsd = &pci_vpb_vmstate;
}

static const TypeInfo pci_vpb_info = {
    .name          = TYPE_VERSATILE_PCI,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PCIVPBState),
    .instance_init = pci_vpb_init,
    .class_init    = pci_vpb_class_init,
};

static void pci_realview_init(Object *obj)
{
    PCIVPBState *s = PCI_VPB(obj);

    s->realview = 1;
}

static const TypeInfo pci_realview_info = {
    .name          = "realview_pci",
    .parent        = TYPE_VERSATILE_PCI,
    .instance_init = pci_realview_init,
};

static void versatile_pci_register_types(void)
{
    type_register_static(&pci_vpb_info);
    type_register_static(&pci_realview_info);
    type_register_static(&versatile_pci_host_info);
}

type_init(versatile_pci_register_types)
