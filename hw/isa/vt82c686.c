/*
 * VT82C686B south bridge support
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2009 chenming (chenming@rdc.faw.com.cn)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/isa/vt82c686.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/isa/isa.h"
#include "hw/isa/superio.h"
#include "migration/vmstate.h"
#include "hw/isa/apm.h"
#include "hw/acpi/acpi.h"
#include "hw/i2c/pm_smbus.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "exec/address-spaces.h"
#include "trace.h"

typedef struct SuperIOConfig {
    uint8_t regs[0x100];
    uint8_t index;
    uint8_t data;
} SuperIOConfig;

struct VT82C686BISAState {
    PCIDevice dev;
    MemoryRegion superio;
    SuperIOConfig superio_cfg;
};

OBJECT_DECLARE_SIMPLE_TYPE(VT82C686BISAState, VT82C686B_ISA)

static void superio_cfg_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned size)
{
    SuperIOConfig *sc = opaque;

    if (addr == 0x3f0) { /* config index register */
        sc->index = data & 0xff;
    } else {
        bool can_write = true;
        /* 0x3f1, config data register */
        trace_via_superio_write(sc->index, data & 0xff);
        switch (sc->index) {
        case 0x00 ... 0xdf:
        case 0xe4:
        case 0xe5:
        case 0xe9 ... 0xed:
        case 0xf3:
        case 0xf5:
        case 0xf7:
        case 0xf9 ... 0xfb:
        case 0xfd ... 0xff:
            can_write = false;
            break;
        /* case 0xe6 ... 0xe8: Should set base port of parallel and serial */
        default:
            break;

        }
        if (can_write) {
            sc->regs[sc->index] = data & 0xff;
        }
    }
}

static uint64_t superio_cfg_read(void *opaque, hwaddr addr, unsigned size)
{
    SuperIOConfig *sc = opaque;
    uint8_t val = sc->regs[sc->index];

    trace_via_superio_read(sc->index, val);
    return val;
}

static const MemoryRegionOps superio_cfg_ops = {
    .read = superio_cfg_read,
    .write = superio_cfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void vt82c686b_isa_reset(DeviceState *dev)
{
    VT82C686BISAState *s = VT82C686B_ISA(dev);
    uint8_t *pci_conf = s->dev.config;

    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x000000c0);
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                 PCI_COMMAND_MASTER | PCI_COMMAND_SPECIAL);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM);

    pci_conf[0x48] = 0x01; /* Miscellaneous Control 3 */
    pci_conf[0x4a] = 0x04; /* IDE interrupt Routing */
    pci_conf[0x4f] = 0x03; /* DMA/Master Mem Access Control 3 */
    pci_conf[0x50] = 0x2d; /* PnP DMA Request Control */
    pci_conf[0x59] = 0x04;
    pci_conf[0x5a] = 0x04; /* KBC/RTC Control*/
    pci_conf[0x5f] = 0x04;
    pci_conf[0x77] = 0x10; /* GPIO Control 1/2/3/4 */

    s->superio_cfg.regs[0xe0] = 0x3c; /* Device ID */
    s->superio_cfg.regs[0xe2] = 0x03; /* Function select */
    s->superio_cfg.regs[0xe3] = 0xfc; /* Floppy ctrl base addr */
    s->superio_cfg.regs[0xe6] = 0xde; /* Parallel port base addr */
    s->superio_cfg.regs[0xe7] = 0xfe; /* Serial port 1 base addr */
    s->superio_cfg.regs[0xe8] = 0xbe; /* Serial port 2 base addr */
}

/* write config pci function0 registers. PCI-ISA bridge */
static void vt82c686b_write_config(PCIDevice *d, uint32_t addr,
                                   uint32_t val, int len)
{
    VT82C686BISAState *s = VT82C686B_ISA(d);

    trace_via_isa_write(addr, val, len);
    pci_default_write_config(d, addr, val, len);
    if (addr == 0x85) {  /* enable or disable super IO configure */
        memory_region_set_enabled(&s->superio, val & 0x2);
    }
}

struct VT686PMState {
    PCIDevice dev;
    MemoryRegion io;
    ACPIREGS ar;
    APMState apm;
    PMSMBus smb;
    uint32_t smb_io_base;
};

OBJECT_DECLARE_SIMPLE_TYPE(VT686PMState, VT82C686B_PM)

static void pm_update_sci(VT686PMState *s)
{
    int sci_level, pmsts;

    pmsts = acpi_pm1_evt_get_sts(&s->ar);
    sci_level = (((pmsts & s->ar.pm1.evt.en) &
                  (ACPI_BITMASK_RT_CLOCK_ENABLE |
                   ACPI_BITMASK_POWER_BUTTON_ENABLE |
                   ACPI_BITMASK_GLOBAL_LOCK_ENABLE |
                   ACPI_BITMASK_TIMER_ENABLE)) != 0);
    pci_set_irq(&s->dev, sci_level);
    /* schedule a timer interruption if needed */
    acpi_pm_tmr_update(&s->ar, (s->ar.pm1.evt.en & ACPI_BITMASK_TIMER_ENABLE) &&
                       !(pmsts & ACPI_BITMASK_TIMER_STATUS));
}

static void pm_tmr_timer(ACPIREGS *ar)
{
    VT686PMState *s = container_of(ar, VT686PMState, ar);
    pm_update_sci(s);
}

static void pm_io_space_update(VT686PMState *s)
{
    uint32_t pm_io_base;

    pm_io_base = pci_get_long(s->dev.config + 0x40);
    pm_io_base &= 0xffc0;

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->io, s->dev.config[0x80] & 1);
    memory_region_set_address(&s->io, pm_io_base);
    memory_region_transaction_commit();
}

static void pm_write_config(PCIDevice *d, uint32_t addr, uint32_t val, int len)
{
    trace_via_pm_write(addr, val, len);
    pci_default_write_config(d, addr, val, len);
}

static int vmstate_acpi_post_load(void *opaque, int version_id)
{
    VT686PMState *s = opaque;

    pm_io_space_update(s);
    return 0;
}

static const VMStateDescription vmstate_acpi = {
    .name = "vt82c686b_pm",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmstate_acpi_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, VT686PMState),
        VMSTATE_UINT16(ar.pm1.evt.sts, VT686PMState),
        VMSTATE_UINT16(ar.pm1.evt.en, VT686PMState),
        VMSTATE_UINT16(ar.pm1.cnt.cnt, VT686PMState),
        VMSTATE_STRUCT(apm, VT686PMState, 0, vmstate_apm, APMState),
        VMSTATE_TIMER_PTR(ar.tmr.timer, VT686PMState),
        VMSTATE_INT64(ar.tmr.overflow_time, VT686PMState),
        VMSTATE_END_OF_LIST()
    }
};

/* vt82c686 pm init */
static void vt82c686b_pm_realize(PCIDevice *dev, Error **errp)
{
    VT686PMState *s = VT82C686B_PM(dev);
    uint8_t *pci_conf;

    pci_conf = s->dev.config;
    pci_set_word(pci_conf + PCI_COMMAND, 0);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_FAST_BACK |
                 PCI_STATUS_DEVSEL_MEDIUM);

    /* 0x48-0x4B is Power Management I/O Base */
    pci_set_long(pci_conf + 0x48, 0x00000001);

    /* SMB ports:0xeee0~0xeeef */
    s->smb_io_base = ((s->smb_io_base & 0xfff0) + 0x0);
    pci_conf[0x90] = s->smb_io_base | 1;
    pci_conf[0x91] = s->smb_io_base >> 8;
    pci_conf[0xd2] = 0x90;
    pm_smbus_init(DEVICE(s), &s->smb, false);
    memory_region_add_subregion(get_system_io(), s->smb_io_base, &s->smb.io);

    apm_init(dev, &s->apm, NULL, s);

    memory_region_init(&s->io, OBJECT(dev), "vt82c686-pm", 64);
    memory_region_set_enabled(&s->io, false);
    memory_region_add_subregion(get_system_io(), 0, &s->io);

    acpi_pm_tmr_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_evt_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_cnt_init(&s->ar, &s->io, false, false, 2);
}

static Property via_pm_properties[] = {
    DEFINE_PROP_UINT32("smb_io_base", VT686PMState, smb_io_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void via_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = vt82c686b_pm_realize;
    k->config_write = pm_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_ACPI;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    k->revision = 0x40;
    dc->desc = "PM";
    dc->vmsd = &vmstate_acpi;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    device_class_set_props(dc, via_pm_properties);
}

static const TypeInfo via_pm_info = {
    .name          = TYPE_VT82C686B_PM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VT686PMState),
    .class_init    = via_pm_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const VMStateDescription vmstate_via = {
    .name = "vt82c686b",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, VT82C686BISAState),
        VMSTATE_END_OF_LIST()
    }
};

/* init the PCI-to-ISA bridge */
static void vt82c686b_realize(PCIDevice *d, Error **errp)
{
    VT82C686BISAState *s = VT82C686B_ISA(d);
    uint8_t *pci_conf;
    ISABus *isa_bus;
    uint8_t *wmask;
    int i;

    isa_bus = isa_bus_new(DEVICE(d), get_system_memory(),
                          pci_address_space_io(d), errp);
    if (!isa_bus) {
        return;
    }

    pci_conf = d->config;
    pci_config_set_prog_interface(pci_conf, 0x0);

    wmask = d->wmask;
    for (i = 0x00; i < 0xff; i++) {
        if (i <= 0x03 || (i >= 0x08 && i <= 0x3f)) {
            wmask[i] = 0x00;
        }
    }

    memory_region_init_io(&s->superio, OBJECT(d), &superio_cfg_ops,
                          &s->superio_cfg, "superio", 2);
    memory_region_set_enabled(&s->superio, false);
    /*
     * The floppy also uses 0x3f0 and 0x3f1.
     * But we do not emulate a floppy, so just set it here.
     */
    memory_region_add_subregion(isa_bus->address_space_io, 0x3f0,
                                &s->superio);
}

static void via_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = vt82c686b_realize;
    k->config_write = vt82c686b_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_ISA_BRIDGE;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    k->revision = 0x40;
    dc->reset = vt82c686b_isa_reset;
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_via;
    /*
     * Reason: part of VIA VT82C686 southbridge, needs to be wired up,
     * e.g. by mips_fuloong2e_init()
     */
    dc->user_creatable = false;
}

static const TypeInfo via_info = {
    .name          = TYPE_VT82C686B_ISA,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VT82C686BISAState),
    .class_init    = via_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void vt82c686b_superio_class_init(ObjectClass *klass, void *data)
{
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    sc->serial.count = 2;
    sc->parallel.count = 1;
    sc->ide.count = 0;
    sc->floppy.count = 1;
}

static const TypeInfo via_superio_info = {
    .name          = TYPE_VT82C686B_SUPERIO,
    .parent        = TYPE_ISA_SUPERIO,
    .instance_size = sizeof(ISASuperIODevice),
    .class_size    = sizeof(ISASuperIOClass),
    .class_init    = vt82c686b_superio_class_init,
};

static void vt82c686b_register_types(void)
{
    type_register_static(&via_pm_info);
    type_register_static(&via_superio_info);
    type_register_static(&via_info);
}

type_init(vt82c686b_register_types)
