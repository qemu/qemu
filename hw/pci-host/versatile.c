/*
 * ARM Versatile/PB PCI host controller
 *
 * Copyright (c) 2006-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

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
 *
 * Unfortunately we have to cope with multiple different
 * variants on the broken kernel behaviour:
 *  phase I (before kernel commit 1bc39ac5d) kernels assume old
 *   QEMU behaviour, so they use IRQ 27 for all slots
 *  phase II (1bc39ac5d and later, but before e3e92a7be6) kernels
 *   swizzle IRQs between slots, but do it wrongly, so they
 *   work only for every fourth PCI card, and only if (like old
 *   QEMU) the PCI host device is at slot 0 rather than where
 *   the h/w actually puts it
 *  phase III (e3e92a7be6 and later) kernels still swizzle IRQs between
 *   slots wrongly, but add a fixed offset of 64 to everything
 *   they write to PCI_INTERRUPT_LINE.
 *
 * We live in hope of a mythical phase IV kernel which might
 * actually behave in ways that work on the hardware. Such a
 * kernel should probably start off by writing some value neither
 * 27 nor 91 to slot zero's PCI_INTERRUPT_LINE register to
 * disable the autodetection. After that it can do what it likes.
 *
 * Slot % 4 | hw | I  | II | III
 * -------------------------------
 *   0      | 29 | 27 | 27 | 91
 *   1      | 30 | 27 | 28 | 92
 *   2      | 27 | 27 | 29 | 93
 *   3      | 28 | 27 | 30 | 94
 *
 * Since our autodetection is not perfect we also provide a
 * property so the user can make us start in BROKEN or FORCE_OK
 * on reset if they know they have a bad or good kernel.
 */
enum {
    PCI_VPB_IRQMAP_ASSUME_OK,
    PCI_VPB_IRQMAP_BROKEN,
    PCI_VPB_IRQMAP_FORCE_OK,
};

struct PCIVPBState {
    PCIHostState parent_obj;

    qemu_irq irq[4];
    MemoryRegion controlregs;
    MemoryRegion mem_config;
    MemoryRegion mem_config2;
    /* Containers representing the PCI address spaces */
    MemoryRegion pci_io_space;
    MemoryRegion pci_mem_space;
    /* Alias regions into PCI address spaces which we expose as sysbus regions.
     * The offsets into pci_mem_space are controlled by the imap registers.
     */
    MemoryRegion pci_io_window;
    MemoryRegion pci_mem_window[3];
    PCIBus pci_bus;
    PCIDevice pci_dev;

    /* Constant for life of device: */
    int realview;
    uint32_t mem_win_size[3];
    uint8_t irq_mapping_prop;

    /* Variable state: */
    uint32_t imap[3];
    uint32_t smap[3];
    uint32_t selfid;
    uint32_t flags;
    uint8_t irq_mapping;
};
typedef struct PCIVPBState PCIVPBState;

static void pci_vpb_update_window(PCIVPBState *s, int i)
{
    /* Adjust the offset of the alias region we use for
     * the memory window i to account for a change in the
     * value of the corresponding IMAP register.
     * Note that the semantics of the IMAP register differ
     * for realview and versatile variants of the controller.
     */
    hwaddr offset;
    if (s->realview) {
        /* Top bits of register (masked according to window size) provide
         * top bits of PCI address.
         */
        offset = s->imap[i] & ~(s->mem_win_size[i] - 1);
    } else {
        /* Bottom 4 bits of register provide top 4 bits of PCI address */
        offset = s->imap[i] << 28;
    }
    memory_region_set_alias_offset(&s->pci_mem_window[i], offset);
}

static void pci_vpb_update_all_windows(PCIVPBState *s)
{
    /* Update all alias windows based on the current register state */
    int i;

    for (i = 0; i < 3; i++) {
        pci_vpb_update_window(s, i);
    }
}

static int pci_vpb_post_load(void *opaque, int version_id)
{
    PCIVPBState *s = opaque;
    pci_vpb_update_all_windows(s);
    return 0;
}

static const VMStateDescription pci_vpb_vmstate = {
    .name = "versatile-pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pci_vpb_post_load,
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
DECLARE_INSTANCE_CHECKER(PCIVPBState, PCI_VPB,
                         TYPE_VERSATILE_PCI)

#define TYPE_VERSATILE_PCI_HOST "versatile_pci_host"
DECLARE_INSTANCE_CHECKER(PCIDevice, PCI_VPB_HOST,
                         TYPE_VERSATILE_PCI_HOST)

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
        pci_vpb_update_window(s, win);
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

static int pci_vpb_broken_irq(int slot, int irq)
{
    /* Determine whether this IRQ value for this slot represents a
     * known broken Linux kernel behaviour for this slot.
     * Return one of the PCI_VPB_IRQMAP_ constants:
     *   BROKEN : if this definitely looks like a broken kernel
     *   FORCE_OK : if this definitely looks good
     *   ASSUME_OK : if we can't tell
     */
    slot %= PCI_NUM_PINS;

    if (irq == 27) {
        if (slot == 2) {
            /* Might be a Phase I kernel, or might be a fixed kernel,
             * since slot 2 is where we expect this IRQ.
             */
            return PCI_VPB_IRQMAP_ASSUME_OK;
        }
        /* Phase I kernel */
        return PCI_VPB_IRQMAP_BROKEN;
    }
    if (irq == slot + 27) {
        /* Phase II kernel */
        return PCI_VPB_IRQMAP_BROKEN;
    }
    if (irq == slot + 27 + 64) {
        /* Phase III kernel */
        return PCI_VPB_IRQMAP_BROKEN;
    }
    /* Anything else must be a fixed kernel, possibly using an
     * arbitrary irq map.
     */
    return PCI_VPB_IRQMAP_FORCE_OK;
}

static void pci_vpb_config_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PCIVPBState *s = opaque;
    if (!s->realview && (addr & 0xff) == PCI_INTERRUPT_LINE
        && s->irq_mapping == PCI_VPB_IRQMAP_ASSUME_OK) {
        uint8_t devfn = addr >> 8;
        s->irq_mapping = pci_vpb_broken_irq(PCI_SLOT(devfn), val);
    }
    pci_data_write(&s->pci_bus, addr, val, size);
}

static uint64_t pci_vpb_config_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    PCIVPBState *s = opaque;
    uint32_t val;
    val = pci_data_read(&s->pci_bus, addr, size);
    return val;
}

static const MemoryRegionOps pci_vpb_config_ops = {
    .read = pci_vpb_config_read,
    .write = pci_vpb_config_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int pci_vpb_map_irq(PCIDevice *d, int irq_num)
{
    PCIVPBState *s = container_of(pci_get_bus(d), PCIVPBState, pci_bus);

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
    s->irq_mapping = s->irq_mapping_prop;

    pci_vpb_update_all_windows(s);
}

static void pci_vpb_init(Object *obj)
{
    PCIVPBState *s = PCI_VPB(obj);

    /* Window sizes for VersatilePB; realview_pci's init will override */
    s->mem_win_size[0] = 0x0c000000;
    s->mem_win_size[1] = 0x10000000;
    s->mem_win_size[2] = 0x10000000;
}

static void pci_vpb_realize(DeviceState *dev, Error **errp)
{
    PCIVPBState *s = PCI_VPB(dev);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    pci_map_irq_fn mapfn;
    int i;

    memory_region_init(&s->pci_io_space, OBJECT(s), "pci_io", 4 * GiB);
    memory_region_init(&s->pci_mem_space, OBJECT(s), "pci_mem", 4 * GiB);

    pci_root_bus_init(&s->pci_bus, sizeof(s->pci_bus), dev, "pci",
                      &s->pci_mem_space, &s->pci_io_space,
                      PCI_DEVFN(11, 0), TYPE_PCI_BUS);
    h->bus = &s->pci_bus;

    object_initialize(&s->pci_dev, sizeof(s->pci_dev), TYPE_VERSATILE_PCI_HOST);

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
     * 4..6 : PCI memory windows
     */
    memory_region_init_io(&s->controlregs, OBJECT(s), &pci_vpb_reg_ops, s,
                          "pci-vpb-regs", 0x1000);
    sysbus_init_mmio(sbd, &s->controlregs);
    memory_region_init_io(&s->mem_config, OBJECT(s), &pci_vpb_config_ops, s,
                          "pci-vpb-selfconfig", 0x1000000);
    sysbus_init_mmio(sbd, &s->mem_config);
    memory_region_init_io(&s->mem_config2, OBJECT(s), &pci_vpb_config_ops, s,
                          "pci-vpb-config", 0x1000000);
    sysbus_init_mmio(sbd, &s->mem_config2);

    /* The window into I/O space is always into a fixed base address;
     * its size is the same for both realview and versatile.
     */
    memory_region_init_alias(&s->pci_io_window, OBJECT(s), "pci-vbp-io-window",
                             &s->pci_io_space, 0, 0x100000);

    sysbus_init_mmio(sbd, &s->pci_io_space);

    /* Create the alias regions corresponding to our three windows onto
     * PCI memory space. The sizes vary from board to board; the base
     * offsets are guest controllable via the IMAP registers.
     */
    for (i = 0; i < 3; i++) {
        memory_region_init_alias(&s->pci_mem_window[i], OBJECT(s), "pci-vbp-window",
                                 &s->pci_mem_space, 0, s->mem_win_size[i]);
        sysbus_init_mmio(sbd, &s->pci_mem_window[i]);
    }

    /* TODO Remove once realize propagates to child devices. */
    qdev_realize(DEVICE(&s->pci_dev), BUS(&s->pci_bus), errp);
}

static void versatile_pci_host_realize(PCIDevice *d, Error **errp)
{
    pci_set_word(d->config + PCI_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_byte(d->config + PCI_LATENCY_TIMER, 0x10);
}

static void versatile_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = versatile_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = PCI_DEVICE_ID_XILINX_XC2VP30;
    k->class_id = PCI_CLASS_PROCESSOR_CO;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo versatile_pci_host_info = {
    .name          = TYPE_VERSATILE_PCI_HOST,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = versatile_pci_host_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static Property pci_vpb_properties[] = {
    DEFINE_PROP_UINT8("broken-irq-mapping", PCIVPBState, irq_mapping_prop,
                      PCI_VPB_IRQMAP_ASSUME_OK),
    DEFINE_PROP_END_OF_LIST()
};

static void pci_vpb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pci_vpb_realize;
    dc->reset = pci_vpb_reset;
    dc->vmsd = &pci_vpb_vmstate;
    device_class_set_props(dc, pci_vpb_properties);
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
    /* The PCI window sizes are different on Realview boards */
    s->mem_win_size[0] = 0x01000000;
    s->mem_win_size[1] = 0x04000000;
    s->mem_win_size[2] = 0x08000000;
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
