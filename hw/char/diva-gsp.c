/*
 * HP Diva GSP controller
 *
 * The Diva PCI boards are Remote Management cards for PA-RISC machines.
 * They come with built-in 16550A multi UARTs for serial consoles
 * and a mailbox-like memory area for hardware auto-reboot functionality.
 * GSP stands for "Guardian Service Processor". Later products were marketed
 * "Management Processor" (MP).
 *
 * Diva cards are multifunctional cards. The first part, the aux port,
 * is on physical machines not useable but we still try to mimic it here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Helge Deller <deller@gmx.de>
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/char/serial.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"

#define PCI_DEVICE_ID_HP_DIVA           0x1048
/* various DIVA GSP cards: */
#define PCI_DEVICE_ID_HP_DIVA_TOSCA1    0x1049
#define PCI_DEVICE_ID_HP_DIVA_TOSCA2    0x104A
#define PCI_DEVICE_ID_HP_DIVA_MAESTRO   0x104B
#define PCI_DEVICE_ID_HP_REO_IOC        0x10f1
#define PCI_DEVICE_ID_HP_DIVA_HALFDOME  0x1223
#define PCI_DEVICE_ID_HP_DIVA_KEYSTONE  0x1226
#define PCI_DEVICE_ID_HP_DIVA_POWERBAR  0x1227
#define PCI_DEVICE_ID_HP_DIVA_EVEREST   0x1282
#define PCI_DEVICE_ID_HP_DIVA_AUX       0x1290
#define PCI_DEVICE_ID_HP_DIVA_RMP3      0x1301
#define PCI_DEVICE_ID_HP_DIVA_HURRICANE 0x132a


#define PCI_SERIAL_MAX_PORTS 4

typedef struct PCIDivaSerialState {
    PCIDevice    dev;
    MemoryRegion membar;        /* for serial ports */
    MemoryRegion mailboxbar;    /* for hardware mailbox */
    uint32_t     subvendor;
    uint32_t     ports;
    char         *name[PCI_SERIAL_MAX_PORTS];
    SerialState  state[PCI_SERIAL_MAX_PORTS];
    uint32_t     level[PCI_SERIAL_MAX_PORTS];
    qemu_irq     *irqs;
    uint8_t      prog_if;
    bool         disable;
} PCIDivaSerialState;

static void diva_pci_exit(PCIDevice *dev)
{
    PCIDivaSerialState *pci = DO_UPCAST(PCIDivaSerialState, dev, dev);
    SerialState *s;
    int i;

    for (i = 0; i < pci->ports; i++) {
        s = pci->state + i;
        qdev_unrealize(DEVICE(s));
        memory_region_del_subregion(&pci->membar, &s->io);
        g_free(pci->name[i]);
    }
    qemu_free_irqs(pci->irqs, pci->ports);
}

static void multi_serial_irq_mux(void *opaque, int n, int level)
{
    PCIDivaSerialState *pci = opaque;
    int i, pending = 0;

    pci->level[n] = level;
    for (i = 0; i < pci->ports; i++) {
        if (pci->level[i]) {
            pending = 1;
        }
    }
    pci_set_irq(&pci->dev, pending);
}

struct diva_info {
    unsigned int nports:4; /* number of serial ports */
    unsigned int omask:12; /* offset mask: BIT(1) -> offset 8 */
};

static struct diva_info diva_get_diva_info(PCIDeviceClass *pc)
{
    switch (pc->subsystem_id) {
    case PCI_DEVICE_ID_HP_DIVA_POWERBAR:
    case PCI_DEVICE_ID_HP_DIVA_HURRICANE:
        return (struct diva_info) { .nports = 1,
                        .omask = BIT(0) };
    case PCI_DEVICE_ID_HP_DIVA_TOSCA2:
        return (struct diva_info) { .nports = 2,
                        .omask = BIT(0) | BIT(1) };
    case PCI_DEVICE_ID_HP_DIVA_TOSCA1:
    case PCI_DEVICE_ID_HP_DIVA_HALFDOME:
    case PCI_DEVICE_ID_HP_DIVA_KEYSTONE:
        return (struct diva_info) { .nports = 3,
                        .omask = BIT(0) | BIT(1) | BIT(2) };
    case PCI_DEVICE_ID_HP_DIVA_EVEREST: /* e.g. in rp3410 */
        return (struct diva_info) { .nports = 3,
                        .omask = BIT(0) | BIT(2) | BIT(7) };
    case PCI_DEVICE_ID_HP_DIVA_MAESTRO:
        return (struct diva_info) { .nports = 4,
                        .omask = BIT(0) | BIT(1) | BIT(2) | BIT(7) };
    }
    g_assert_not_reached();
}


static void diva_pci_realize(PCIDevice *dev, Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    PCIDivaSerialState *pci = DO_UPCAST(PCIDivaSerialState, dev, dev);
    SerialState *s;
    struct diva_info di = diva_get_diva_info(pc);
    size_t i, offset = 0;
    size_t portmask = di.omask;

    pci->dev.config[PCI_CLASS_PROG] = pci->prog_if;
    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    memory_region_init(&pci->membar, OBJECT(pci), "serial_ports", 4096);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &pci->membar);
    pci->irqs = qemu_allocate_irqs(multi_serial_irq_mux, pci, di.nports);

    for (i = 0; i < di.nports; i++) {
        s = pci->state + i;
        if (!qdev_realize(DEVICE(s), NULL, errp)) {
            diva_pci_exit(dev);
            return;
        }
        s->irq = pci->irqs[i];
        pci->name[i] = g_strdup_printf("uart #%zu", i + 1);
        memory_region_init_io(&s->io, OBJECT(pci), &serial_io_ops, s,
                              pci->name[i], 8);

        /* calculate offset of given port based on bitmask */
        while ((portmask & BIT(0)) == 0) {
            offset += 8;
            portmask >>= 1;
        }
        memory_region_add_subregion(&pci->membar, offset, &s->io);
        offset += 8;
        portmask >>= 1;
        pci->ports++;
    }

    /* mailbox bar */
    memory_region_init(&pci->mailboxbar, OBJECT(pci), "mailbox", 128 * KiB);
    pci_register_bar(&pci->dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &pci->mailboxbar);
}

static const VMStateDescription vmstate_pci_diva = {
    .name = "pci-diva-serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PCIDivaSerialState),
        VMSTATE_STRUCT_ARRAY(state, PCIDivaSerialState, PCI_SERIAL_MAX_PORTS,
                             0, vmstate_serial, SerialState),
        VMSTATE_UINT32_ARRAY(level, PCIDivaSerialState, PCI_SERIAL_MAX_PORTS),
        VMSTATE_BOOL(disable, PCIDivaSerialState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property diva_serial_properties[] = {
    DEFINE_PROP_BOOL("disable",  PCIDivaSerialState, disable, false),
    DEFINE_PROP_CHR("chardev1",  PCIDivaSerialState, state[0].chr),
    DEFINE_PROP_CHR("chardev2",  PCIDivaSerialState, state[1].chr),
    DEFINE_PROP_CHR("chardev3",  PCIDivaSerialState, state[2].chr),
    DEFINE_PROP_CHR("chardev4",  PCIDivaSerialState, state[3].chr),
    DEFINE_PROP_UINT8("prog_if",  PCIDivaSerialState, prog_if, 0x02),
    DEFINE_PROP_UINT32("subvendor", PCIDivaSerialState, subvendor,
                                    PCI_DEVICE_ID_HP_DIVA_TOSCA1),
};

static void diva_serial_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->realize = diva_pci_realize;
    pc->exit = diva_pci_exit;
    pc->vendor_id = PCI_VENDOR_ID_HP;
    pc->device_id = PCI_DEVICE_ID_HP_DIVA;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_HP;
    pc->subsystem_id = PCI_DEVICE_ID_HP_DIVA_TOSCA1;
    pc->revision = 3;
    pc->class_id = PCI_CLASS_COMMUNICATION_SERIAL;
    dc->vmsd = &vmstate_pci_diva;
    device_class_set_props(dc, diva_serial_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void diva_serial_init(Object *o)
{
    PCIDevice *dev = PCI_DEVICE(o);
    PCIDivaSerialState *pms = DO_UPCAST(PCIDivaSerialState, dev, dev);
    struct diva_info di = diva_get_diva_info(PCI_DEVICE_GET_CLASS(dev));
    size_t i;

    for (i = 0; i < di.nports; i++) {
        object_initialize_child(o, "serial[*]", &pms->state[i], TYPE_SERIAL);
    }
}


/* Diva-aux is the driver for portion 0 of the multifunction PCI device */

struct DivaAuxState {
    PCIDevice dev;
    MemoryRegion mem;
    qemu_irq irq;
};

#define TYPE_DIVA_AUX "diva-aux"
OBJECT_DECLARE_SIMPLE_TYPE(DivaAuxState, DIVA_AUX)

static void diva_aux_realize(PCIDevice *dev, Error **errp)
{
    DivaAuxState *pci = DO_UPCAST(DivaAuxState, dev, dev);

    pci->dev.config[PCI_CLASS_PROG] = 0x02;
    pci->dev.config[PCI_INTERRUPT_PIN] = 0x01;
    pci->irq = pci_allocate_irq(&pci->dev);

    memory_region_init(&pci->mem, OBJECT(pci), "mem", 16);
    pci_register_bar(&pci->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &pci->mem);
}

static void diva_aux_exit(PCIDevice *dev)
{
    DivaAuxState *pci = DO_UPCAST(DivaAuxState, dev, dev);
    qemu_free_irq(pci->irq);
}

static void diva_aux_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->realize = diva_aux_realize;
    pc->exit = diva_aux_exit;
    pc->vendor_id = PCI_VENDOR_ID_HP;
    pc->device_id = PCI_DEVICE_ID_HP_DIVA_AUX;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_HP;
    pc->subsystem_id = 0x1291;
    pc->revision = 1;
    pc->class_id = PCI_CLASS_COMMUNICATION_MULTISERIAL;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->user_creatable = false;
}

static void diva_aux_init(Object *o)
{
}

static const TypeInfo diva_aux_info = {
    .name          = TYPE_DIVA_AUX,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(DivaAuxState),
    .instance_init = diva_aux_init,
    .class_init    = diva_aux_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};



static const TypeInfo diva_serial_pci_info = {
    .name          = "diva-gsp",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDivaSerialState),
    .instance_init = diva_serial_init,
    .class_init    = diva_serial_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void diva_pci_register_type(void)
{
    type_register_static(&diva_serial_pci_info);
    type_register_static(&diva_aux_info);
}

type_init(diva_pci_register_type)
