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
 *
 * VT8231 south bridge support and general clean up to allow it
 * Copyright (c) 2018-2020 BALATON Zoltan
 */

#include "qemu/osdep.h"
#include "hw/isa/vt82c686.h"
#include "hw/block/fdc.h"
#include "hw/char/parallel-isa.h"
#include "hw/char/serial-isa.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/ide/pci.h"
#include "hw/isa/isa.h"
#include "hw/isa/superio.h"
#include "hw/intc/i8259.h"
#include "hw/irq.h"
#include "hw/dma/i8257.h"
#include "hw/usb/hcd-uhci.h"
#include "hw/timer/i8254.h"
#include "hw/rtc/mc146818rtc.h"
#include "migration/vmstate.h"
#include "hw/isa/apm.h"
#include "hw/acpi/acpi.h"
#include "hw/i2c/pm_smbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "qemu/timer.h"
#include "trace.h"

#define TYPE_VIA_PM "via-pm"
OBJECT_DECLARE_SIMPLE_TYPE(ViaPMState, VIA_PM)

struct ViaPMState {
    PCIDevice dev;
    MemoryRegion io;
    ACPIREGS ar;
    APMState apm;
    PMSMBus smb;
};

static void pm_io_space_update(ViaPMState *s)
{
    uint32_t pmbase = pci_get_long(s->dev.config + 0x48) & 0xff80UL;

    memory_region_transaction_begin();
    memory_region_set_address(&s->io, pmbase);
    memory_region_set_enabled(&s->io, s->dev.config[0x41] & BIT(7));
    memory_region_transaction_commit();
}

static void smb_io_space_update(ViaPMState *s)
{
    uint32_t smbase = pci_get_long(s->dev.config + 0x90) & 0xfff0UL;

    memory_region_transaction_begin();
    memory_region_set_address(&s->smb.io, smbase);
    memory_region_set_enabled(&s->smb.io, s->dev.config[0xd2] & BIT(0));
    memory_region_transaction_commit();
}

static int vmstate_acpi_post_load(void *opaque, int version_id)
{
    ViaPMState *s = opaque;

    pm_io_space_update(s);
    smb_io_space_update(s);
    return 0;
}

static const VMStateDescription vmstate_acpi = {
    .name = "vt82c686b_pm",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmstate_acpi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, ViaPMState),
        VMSTATE_UINT16(ar.pm1.evt.sts, ViaPMState),
        VMSTATE_UINT16(ar.pm1.evt.en, ViaPMState),
        VMSTATE_UINT16(ar.pm1.cnt.cnt, ViaPMState),
        VMSTATE_STRUCT(apm, ViaPMState, 0, vmstate_apm, APMState),
        VMSTATE_TIMER_PTR(ar.tmr.timer, ViaPMState),
        VMSTATE_INT64(ar.tmr.overflow_time, ViaPMState),
        VMSTATE_END_OF_LIST()
    }
};

static void pm_write_config(PCIDevice *d, uint32_t addr, uint32_t val, int len)
{
    ViaPMState *s = VIA_PM(d);

    trace_via_pm_write(addr, val, len);
    pci_default_write_config(d, addr, val, len);
    if (ranges_overlap(addr, len, 0x48, 4)) {
        uint32_t v = pci_get_long(s->dev.config + 0x48);
        pci_set_long(s->dev.config + 0x48, (v & 0xff80UL) | 1);
    }
    if (range_covers_byte(addr, len, 0x41)) {
        pm_io_space_update(s);
    }
    if (ranges_overlap(addr, len, 0x90, 4)) {
        uint32_t v = pci_get_long(s->dev.config + 0x90);
        pci_set_long(s->dev.config + 0x90, (v & 0xfff0UL) | 1);
    }
    if (range_covers_byte(addr, len, 0xd2)) {
        s->dev.config[0xd2] &= 0xf;
        smb_io_space_update(s);
    }
}

static void pm_io_write(void *op, hwaddr addr, uint64_t data, unsigned size)
{
    trace_via_pm_io_write(addr, data, size);
}

static uint64_t pm_io_read(void *op, hwaddr addr, unsigned size)
{
    trace_via_pm_io_read(addr, 0, size);
    return 0;
}

static const MemoryRegionOps pm_io_ops = {
    .read = pm_io_read,
    .write = pm_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pm_update_sci(ViaPMState *s)
{
    int sci_level, pmsts;

    pmsts = acpi_pm1_evt_get_sts(&s->ar);
    sci_level = (((pmsts & s->ar.pm1.evt.en) &
                  (ACPI_BITMASK_RT_CLOCK_ENABLE |
                   ACPI_BITMASK_POWER_BUTTON_ENABLE |
                   ACPI_BITMASK_GLOBAL_LOCK_ENABLE |
                   ACPI_BITMASK_TIMER_ENABLE)) != 0);
    if (pci_get_byte(s->dev.config + PCI_INTERRUPT_PIN)) {
        /*
         * FIXME:
         * Fix device model that realizes this PM device and remove
         * this work around.
         * The device model should wire SCI and setup
         * PCI_INTERRUPT_PIN properly.
         * If PIN# = 0(interrupt pin isn't used), don't raise SCI as
         * work around.
         */
        pci_set_irq(&s->dev, sci_level);
    }
    /* schedule a timer interruption if needed */
    acpi_pm_tmr_update(&s->ar, (s->ar.pm1.evt.en & ACPI_BITMASK_TIMER_ENABLE) &&
                       !(pmsts & ACPI_BITMASK_TIMER_STATUS));
}

static void pm_tmr_timer(ACPIREGS *ar)
{
    ViaPMState *s = container_of(ar, ViaPMState, ar);
    pm_update_sci(s);
}

static void via_pm_reset(DeviceState *d)
{
    ViaPMState *s = VIA_PM(d);

    memset(s->dev.config + PCI_CONFIG_HEADER_SIZE, 0,
           PCI_CONFIG_SPACE_SIZE - PCI_CONFIG_HEADER_SIZE);
    /* Power Management IO base */
    pci_set_long(s->dev.config + 0x48, 1);
    /* SMBus IO base */
    pci_set_long(s->dev.config + 0x90, 1);

    acpi_pm1_evt_reset(&s->ar);
    acpi_pm1_cnt_reset(&s->ar);
    acpi_pm_tmr_reset(&s->ar);
    pm_update_sci(s);

    pm_io_space_update(s);
    smb_io_space_update(s);
}

static void via_pm_realize(PCIDevice *dev, Error **errp)
{
    ViaPMState *s = VIA_PM(dev);

    pci_set_word(dev->config + PCI_STATUS, PCI_STATUS_FAST_BACK |
                 PCI_STATUS_DEVSEL_MEDIUM);

    pm_smbus_init(DEVICE(s), &s->smb, false);
    memory_region_add_subregion(pci_address_space_io(dev), 0, &s->smb.io);
    memory_region_set_enabled(&s->smb.io, false);

    apm_init(dev, &s->apm, NULL, s);

    memory_region_init_io(&s->io, OBJECT(dev), &pm_io_ops, s, "via-pm", 128);
    memory_region_add_subregion(pci_address_space_io(dev), 0, &s->io);
    memory_region_set_enabled(&s->io, false);

    acpi_pm_tmr_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_evt_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_cnt_init(&s->ar, &s->io, false, false, 2, false);
}

typedef struct via_pm_init_info {
    uint16_t device_id;
} ViaPMInitInfo;

static void via_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    const ViaPMInitInfo *info = data;

    k->realize = via_pm_realize;
    k->config_write = pm_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = info->device_id;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    k->revision = 0x40;
    device_class_set_legacy_reset(dc, via_pm_reset);
    /* Reason: part of VIA south bridge, does not exist stand alone */
    dc->user_creatable = false;
    dc->vmsd = &vmstate_acpi;
}

static const TypeInfo via_pm_info = {
    .name          = TYPE_VIA_PM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ViaPMState),
    .abstract      = true,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const ViaPMInitInfo vt82c686b_pm_init_info = {
    .device_id = PCI_DEVICE_ID_VIA_82C686B_PM,
};

#define TYPE_VT82C686B_PM "vt82c686b-pm"

static const TypeInfo vt82c686b_pm_info = {
    .name          = TYPE_VT82C686B_PM,
    .parent        = TYPE_VIA_PM,
    .class_init    = via_pm_class_init,
    .class_data    = (void *)&vt82c686b_pm_init_info,
};

static const ViaPMInitInfo vt8231_pm_init_info = {
    .device_id = PCI_DEVICE_ID_VIA_8231_PM,
};

#define TYPE_VT8231_PM "vt8231-pm"

static const TypeInfo vt8231_pm_info = {
    .name          = TYPE_VT8231_PM,
    .parent        = TYPE_VIA_PM,
    .class_init    = via_pm_class_init,
    .class_data    = (void *)&vt8231_pm_init_info,
};


#define TYPE_VIA_SUPERIO "via-superio"
OBJECT_DECLARE_SIMPLE_TYPE(ViaSuperIOState, VIA_SUPERIO)

struct ViaSuperIOState {
    ISASuperIODevice superio;
    uint8_t regs[0x100];
    const MemoryRegionOps *io_ops;
    MemoryRegion io;
};

static inline void via_superio_io_enable(ViaSuperIOState *s, bool enable)
{
    memory_region_set_enabled(&s->io, enable);
}

static void via_superio_realize(DeviceState *d, Error **errp)
{
    ViaSuperIOState *s = VIA_SUPERIO(d);
    ISASuperIOClass *ic = ISA_SUPERIO_GET_CLASS(s);
    Error *local_err = NULL;

    assert(s->io_ops);
    ic->parent_realize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_init_io(&s->io, OBJECT(d), s->io_ops, s, "via-superio", 2);
    memory_region_set_enabled(&s->io, false);
    /* The floppy also uses 0x3f0 and 0x3f1 but this seems to work anyway */
    memory_region_add_subregion(isa_address_space_io(ISA_DEVICE(s)), 0x3f0,
                                &s->io);
}

static uint64_t via_superio_cfg_read(void *opaque, hwaddr addr, unsigned size)
{
    ViaSuperIOState *sc = opaque;
    uint8_t idx = sc->regs[0];
    uint8_t val = sc->regs[idx];

    if (addr == 0) {
        return idx;
    }
    if (addr == 1 && idx == 0) {
        val = 0; /* reading reg 0 where we store index value */
    }
    trace_via_superio_read(idx, val);
    return val;
}

static void via_superio_devices_enable(ViaSuperIOState *s, uint8_t data)
{
    ISASuperIOClass *ic = ISA_SUPERIO_GET_CLASS(s);

    isa_parallel_set_enabled(s->superio.parallel[0], (data & 0x3) != 3);
    for (int i = 0; i < ic->serial.count; i++) {
        isa_serial_set_enabled(s->superio.serial[i], data & BIT(i + 2));
    }
    isa_fdc_set_enabled(s->superio.floppy, data & BIT(4));
}

static void via_superio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    device_class_set_parent_realize(dc, via_superio_realize,
                                    &sc->parent_realize);
}

static const TypeInfo via_superio_info = {
    .name          = TYPE_VIA_SUPERIO,
    .parent        = TYPE_ISA_SUPERIO,
    .instance_size = sizeof(ViaSuperIOState),
    .class_size    = sizeof(ISASuperIOClass),
    .class_init    = via_superio_class_init,
    .abstract      = true,
};

#define TYPE_VT82C686B_SUPERIO "vt82c686b-superio"

static void vt82c686b_superio_cfg_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    ViaSuperIOState *sc = opaque;
    uint8_t idx = sc->regs[0];

    if (addr == 0) { /* config index register */
        sc->regs[0] = data;
        return;
    }

    /* config data register */
    trace_via_superio_write(idx, data);
    switch (idx) {
    case 0x00 ... 0xdf:
    case 0xe4:
    case 0xe5:
    case 0xe9 ... 0xed:
    case 0xf3:
    case 0xf5:
    case 0xf7:
    case 0xf9 ... 0xfb:
    case 0xfd ... 0xff:
        /* ignore write to read only registers */
        return;
    case 0xe2:
        data &= 0x1f;
        via_superio_devices_enable(sc, data);
        break;
    case 0xe3:
        data &= 0xfc;
        isa_fdc_set_iobase(sc->superio.floppy, data << 2);
        break;
    case 0xe6:
        isa_parallel_set_iobase(sc->superio.parallel[0], data << 2);
        break;
    case 0xe7:
        data &= 0xfe;
        isa_serial_set_iobase(sc->superio.serial[0], data << 2);
        break;
    case 0xe8:
        data &= 0xfe;
        isa_serial_set_iobase(sc->superio.serial[1], data << 2);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "via_superio_cfg: unimplemented register 0x%x\n", idx);
        break;
    }
    sc->regs[idx] = data;
}

static const MemoryRegionOps vt82c686b_superio_cfg_ops = {
    .read = via_superio_cfg_read,
    .write = vt82c686b_superio_cfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void vt82c686b_superio_reset(DeviceState *dev)
{
    ViaSuperIOState *s = VIA_SUPERIO(dev);

    memset(s->regs, 0, sizeof(s->regs));
    /* Device ID */
    vt82c686b_superio_cfg_write(s, 0, 0xe0, 1);
    vt82c686b_superio_cfg_write(s, 1, 0x3c, 1);
    /*
     * Function select - only serial enabled
     * Fuloong 2e's rescue-yl prints to the serial console w/o enabling it. This
     * suggests that the serial ports are enabled by default, so override the
     * datasheet.
     */
    vt82c686b_superio_cfg_write(s, 0, 0xe2, 1);
    vt82c686b_superio_cfg_write(s, 1, 0x0f, 1);
    /* Floppy ctrl base addr 0x3f0-7 */
    vt82c686b_superio_cfg_write(s, 0, 0xe3, 1);
    vt82c686b_superio_cfg_write(s, 1, 0xfc, 1);
    /* Parallel port base addr 0x378-f */
    vt82c686b_superio_cfg_write(s, 0, 0xe6, 1);
    vt82c686b_superio_cfg_write(s, 1, 0xde, 1);
    /* Serial port 1 base addr 0x3f8-f */
    vt82c686b_superio_cfg_write(s, 0, 0xe7, 1);
    vt82c686b_superio_cfg_write(s, 1, 0xfe, 1);
    /* Serial port 2 base addr 0x2f8-f */
    vt82c686b_superio_cfg_write(s, 0, 0xe8, 1);
    vt82c686b_superio_cfg_write(s, 1, 0xbe, 1);

    vt82c686b_superio_cfg_write(s, 0, 0, 1);
}

static void vt82c686b_superio_init(Object *obj)
{
    VIA_SUPERIO(obj)->io_ops = &vt82c686b_superio_cfg_ops;
}

static void vt82c686b_superio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    device_class_set_legacy_reset(dc, vt82c686b_superio_reset);
    sc->serial.count = 2;
    sc->parallel.count = 1;
    sc->ide.count = 0; /* emulated by via-ide */
    sc->floppy.count = 1;
}

static const TypeInfo vt82c686b_superio_info = {
    .name          = TYPE_VT82C686B_SUPERIO,
    .parent        = TYPE_VIA_SUPERIO,
    .instance_size = sizeof(ViaSuperIOState),
    .instance_init = vt82c686b_superio_init,
    .class_size    = sizeof(ISASuperIOClass),
    .class_init    = vt82c686b_superio_class_init,
};


#define TYPE_VT8231_SUPERIO "vt8231-superio"

static void vt8231_superio_cfg_write(void *opaque, hwaddr addr,
                                     uint64_t data, unsigned size)
{
    ViaSuperIOState *sc = opaque;
    uint8_t idx = sc->regs[0];

    if (addr == 0) { /* config index register */
        sc->regs[0] = data;
        return;
    }

    /* config data register */
    trace_via_superio_write(idx, data);
    switch (idx) {
    case 0x00 ... 0xdf:
    case 0xe7 ... 0xef:
    case 0xf0 ... 0xf1:
    case 0xf5:
    case 0xf8:
    case 0xfd:
        /* ignore write to read only registers */
        return;
    case 0xf2:
        data &= 0x17;
        via_superio_devices_enable(sc, data);
        break;
    case 0xf4:
        data &= 0xfe;
        isa_serial_set_iobase(sc->superio.serial[0], data << 2);
        break;
    case 0xf6:
        isa_parallel_set_iobase(sc->superio.parallel[0], data << 2);
        break;
    case 0xf7:
        data &= 0xfc;
        isa_fdc_set_iobase(sc->superio.floppy, data << 2);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "via_superio_cfg: unimplemented register 0x%x\n", idx);
        break;
    }
    sc->regs[idx] = data;
}

static const MemoryRegionOps vt8231_superio_cfg_ops = {
    .read = via_superio_cfg_read,
    .write = vt8231_superio_cfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void vt8231_superio_reset(DeviceState *dev)
{
    ViaSuperIOState *s = VIA_SUPERIO(dev);

    memset(s->regs, 0, sizeof(s->regs));
    /* Device ID */
    s->regs[0xf0] = 0x3c;
    /* Device revision */
    s->regs[0xf1] = 0x01;
    /* Function select - all disabled */
    vt8231_superio_cfg_write(s, 0, 0xf2, 1);
    vt8231_superio_cfg_write(s, 1, 0x03, 1);
    /* Serial port base addr */
    vt8231_superio_cfg_write(s, 0, 0xf4, 1);
    vt8231_superio_cfg_write(s, 1, 0xfe, 1);
    /* Parallel port base addr */
    vt8231_superio_cfg_write(s, 0, 0xf6, 1);
    vt8231_superio_cfg_write(s, 1, 0xde, 1);
    /* Floppy ctrl base addr */
    vt8231_superio_cfg_write(s, 0, 0xf7, 1);
    vt8231_superio_cfg_write(s, 1, 0xfc, 1);

    vt8231_superio_cfg_write(s, 0, 0, 1);
}

static void vt8231_superio_init(Object *obj)
{
    VIA_SUPERIO(obj)->io_ops = &vt8231_superio_cfg_ops;
}

static void vt8231_superio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    device_class_set_legacy_reset(dc, vt8231_superio_reset);
    sc->serial.count = 1;
    sc->parallel.count = 1;
    sc->ide.count = 0; /* emulated by via-ide */
    sc->floppy.count = 1;
}

static const TypeInfo vt8231_superio_info = {
    .name          = TYPE_VT8231_SUPERIO,
    .parent        = TYPE_VIA_SUPERIO,
    .instance_size = sizeof(ViaSuperIOState),
    .instance_init = vt8231_superio_init,
    .class_size    = sizeof(ISASuperIOClass),
    .class_init    = vt8231_superio_class_init,
};


#define TYPE_VIA_ISA "via-isa"
OBJECT_DECLARE_SIMPLE_TYPE(ViaISAState, VIA_ISA)

struct ViaISAState {
    PCIDevice dev;

    IRQState i8259_irq;
    qemu_irq cpu_intr;
    qemu_irq *isa_irqs_in;
    uint16_t irq_state[ISA_NUM_IRQS];
    ViaSuperIOState via_sio;
    MC146818RtcState rtc;
    PCIIDEState ide;
    UHCIState uhci[2];
    ViaPMState pm;
    ViaAC97State ac97;
    PCIDevice mc97;
};

static const VMStateDescription vmstate_via = {
    .name = "via-isa",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, ViaISAState),
        VMSTATE_END_OF_LIST()
    }
};

static void via_isa_init(Object *obj)
{
    ViaISAState *s = VIA_ISA(obj);

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_MC146818_RTC);
    object_initialize_child(obj, "ide", &s->ide, TYPE_VIA_IDE);
    object_initialize_child(obj, "uhci1", &s->uhci[0], TYPE_VT82C686B_USB_UHCI);
    object_initialize_child(obj, "uhci2", &s->uhci[1], TYPE_VT82C686B_USB_UHCI);
    object_initialize_child(obj, "ac97", &s->ac97, TYPE_VIA_AC97);
    object_initialize_child(obj, "mc97", &s->mc97, TYPE_VIA_MC97);
}

static const TypeInfo via_isa_info = {
    .name          = TYPE_VIA_ISA,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ViaISAState),
    .instance_init = via_isa_init,
    .abstract      = true,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static int via_isa_get_pci_irq(const ViaISAState *s, int pin)
{
    switch (pin) {
    case 0:
        return s->dev.config[0x55] >> 4;
    case 1:
        return s->dev.config[0x56] & 0xf;
    case 2:
        return s->dev.config[0x56] >> 4;
    case 3:
        return s->dev.config[0x57] >> 4;
    }
    return 0;
}

void via_isa_set_irq(PCIDevice *d, int pin, int level)
{
    ViaISAState *s = VIA_ISA(pci_get_function_0(d));
    uint8_t irq = d->config[PCI_INTERRUPT_LINE], max_irq = 15;
    int f = PCI_FUNC(d->devfn);
    uint16_t mask;

    switch (f) {
    case 0: /* PIRQ/PINT inputs */
        irq = via_isa_get_pci_irq(s, pin);
        f = 8 + pin; /* Use function 8-11 for PCI interrupt inputs */
        break;
    case 2: /* USB ports 0-1 */
    case 3: /* USB ports 2-3 */
    case 5: /* AC97 audio */
        max_irq = 14;
        break;
    }

    /* Keep track of the state of all sources */
    mask = BIT(f);
    if (level) {
        s->irq_state[0] |= mask;
    } else {
        s->irq_state[0] &= ~mask;
    }
    if (irq == 0 || irq == 0xff) {
        return; /* disabled */
    }
    if (unlikely(irq > max_irq || irq == 2)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid ISA IRQ routing %d for %d",
                      irq, f);
        return;
    }
    /* Record source state at mapped IRQ */
    if (level) {
        s->irq_state[irq] |= mask;
    } else {
        s->irq_state[irq] &= ~mask;
    }
    /* Make sure there are no stuck bits if mapping has changed */
    s->irq_state[irq] &= s->irq_state[0];
    /* ISA IRQ level is the OR of all sources routed to it */
    qemu_set_irq(s->isa_irqs_in[irq], !!s->irq_state[irq]);
}

static void via_isa_pirq(void *opaque, int pin, int level)
{
    via_isa_set_irq(opaque, pin, level);
}

static void via_isa_request_i8259_irq(void *opaque, int irq, int level)
{
    ViaISAState *s = opaque;
    qemu_set_irq(s->cpu_intr, level);
}

static void via_isa_realize(PCIDevice *d, Error **errp)
{
    ViaISAState *s = VIA_ISA(d);
    DeviceState *dev = DEVICE(d);
    PCIBus *pci_bus = pci_get_bus(d);
    ISABus *isa_bus;
    int i;

    qdev_init_gpio_out_named(dev, &s->cpu_intr, "intr", 1);
    qdev_init_gpio_in_named(dev, via_isa_pirq, "pirq", PCI_NUM_PINS);
    qemu_init_irq(&s->i8259_irq, via_isa_request_i8259_irq, s, 0);
    isa_bus = isa_bus_new(dev, pci_address_space(d), pci_address_space_io(d),
                          errp);

    if (!isa_bus) {
        return;
    }

    s->isa_irqs_in = i8259_init(isa_bus, &s->i8259_irq);
    isa_bus_register_input_irqs(isa_bus, s->isa_irqs_in);
    i8254_pit_init(isa_bus, 0x40, 0, NULL);
    i8257_dma_init(OBJECT(d), isa_bus, 0);

    /* RTC */
    qdev_prop_set_int32(DEVICE(&s->rtc), "base_year", 2000);
    if (!qdev_realize(DEVICE(&s->rtc), BUS(isa_bus), errp)) {
        return;
    }
    isa_connect_gpio_out(ISA_DEVICE(&s->rtc), 0, s->rtc.isairq);

    for (i = 0; i < PCI_CONFIG_HEADER_SIZE; i++) {
        if (i < PCI_COMMAND || i >= PCI_REVISION_ID) {
            d->wmask[i] = 0;
        }
    }

    /* Super I/O */
    if (!qdev_realize(DEVICE(&s->via_sio), BUS(isa_bus), errp)) {
        return;
    }

    /* Function 1: IDE */
    qdev_prop_set_int32(DEVICE(&s->ide), "addr", d->devfn + 1);
    if (!qdev_realize(DEVICE(&s->ide), BUS(pci_bus), errp)) {
        return;
    }
    for (i = 0; i < 2; i++) {
        qdev_connect_gpio_out_named(DEVICE(&s->ide), "isa-irq", i,
                                    s->isa_irqs_in[14 + i]);
    }

    /* Functions 2-3: USB Ports */
    for (i = 0; i < ARRAY_SIZE(s->uhci); i++) {
        qdev_prop_set_int32(DEVICE(&s->uhci[i]), "addr", d->devfn + 2 + i);
        if (!qdev_realize(DEVICE(&s->uhci[i]), BUS(pci_bus), errp)) {
            return;
        }
    }

    /* Function 4: Power Management */
    qdev_prop_set_int32(DEVICE(&s->pm), "addr", d->devfn + 4);
    if (!qdev_realize(DEVICE(&s->pm), BUS(pci_bus), errp)) {
        return;
    }

    /* Function 5: AC97 Audio */
    qdev_prop_set_int32(DEVICE(&s->ac97), "addr", d->devfn + 5);
    if (!qdev_realize(DEVICE(&s->ac97), BUS(pci_bus), errp)) {
        return;
    }

    /* Function 6: MC97 Modem */
    qdev_prop_set_int32(DEVICE(&s->mc97), "addr", d->devfn + 6);
    if (!qdev_realize(DEVICE(&s->mc97), BUS(pci_bus), errp)) {
        return;
    }
}

/* TYPE_VT82C686B_ISA */

static void vt82c686b_write_config(PCIDevice *d, uint32_t addr,
                                   uint32_t val, int len)
{
    ViaISAState *s = VIA_ISA(d);

    trace_via_isa_write(addr, val, len);
    pci_default_write_config(d, addr, val, len);
    if (addr == 0x85) {
        /* BIT(1): enable or disable superio config io ports */
        via_superio_io_enable(&s->via_sio, val & BIT(1));
    }
}

static void vt82c686b_isa_reset(DeviceState *dev)
{
    ViaISAState *s = VIA_ISA(dev);
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
}

static void vt82c686b_init(Object *obj)
{
    ViaISAState *s = VIA_ISA(obj);

    object_initialize_child(obj, "sio", &s->via_sio, TYPE_VT82C686B_SUPERIO);
    object_initialize_child(obj, "pm", &s->pm, TYPE_VT82C686B_PM);
}

static void vt82c686b_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = via_isa_realize;
    k->config_write = vt82c686b_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_82C686B_ISA;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    k->revision = 0x40;
    device_class_set_legacy_reset(dc, vt82c686b_isa_reset);
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_via;
    /* Reason: part of VIA VT82C686 southbridge, needs to be wired up */
    dc->user_creatable = false;
}

static const TypeInfo vt82c686b_isa_info = {
    .name          = TYPE_VT82C686B_ISA,
    .parent        = TYPE_VIA_ISA,
    .instance_size = sizeof(ViaISAState),
    .instance_init = vt82c686b_init,
    .class_init    = vt82c686b_class_init,
};

/* TYPE_VT8231_ISA */

static void vt8231_write_config(PCIDevice *d, uint32_t addr,
                                uint32_t val, int len)
{
    ViaISAState *s = VIA_ISA(d);

    trace_via_isa_write(addr, val, len);
    pci_default_write_config(d, addr, val, len);
    if (addr == 0x50) {
        /* BIT(2): enable or disable superio config io ports */
        via_superio_io_enable(&s->via_sio, val & BIT(2));
    }
}

static void vt8231_isa_reset(DeviceState *dev)
{
    ViaISAState *s = VIA_ISA(dev);
    uint8_t *pci_conf = s->dev.config;

    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x000000c0);
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                 PCI_COMMAND_MASTER | PCI_COMMAND_SPECIAL);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM);

    pci_conf[0x4c] = 0x04; /* IDE interrupt Routing */
    pci_conf[0x58] = 0x40; /* Miscellaneous Control 0 */
    pci_conf[0x67] = 0x08; /* Fast IR Config */
    pci_conf[0x6b] = 0x01; /* Fast IR I/O Base */
}

static void vt8231_init(Object *obj)
{
    ViaISAState *s = VIA_ISA(obj);

    object_initialize_child(obj, "sio", &s->via_sio, TYPE_VT8231_SUPERIO);
    object_initialize_child(obj, "pm", &s->pm, TYPE_VT8231_PM);
}

static void vt8231_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = via_isa_realize;
    k->config_write = vt8231_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_8231_ISA;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    k->revision = 0x10;
    device_class_set_legacy_reset(dc, vt8231_isa_reset);
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_via;
    /* Reason: part of VIA VT8231 southbridge, needs to be wired up */
    dc->user_creatable = false;
}

static const TypeInfo vt8231_isa_info = {
    .name          = TYPE_VT8231_ISA,
    .parent        = TYPE_VIA_ISA,
    .instance_size = sizeof(ViaISAState),
    .instance_init = vt8231_init,
    .class_init    = vt8231_class_init,
};


static void vt82c686b_register_types(void)
{
    type_register_static(&via_pm_info);
    type_register_static(&vt82c686b_pm_info);
    type_register_static(&vt8231_pm_info);
    type_register_static(&via_superio_info);
    type_register_static(&vt82c686b_superio_info);
    type_register_static(&vt8231_superio_info);
    type_register_static(&via_isa_info);
    type_register_static(&vt82c686b_isa_info);
    type_register_static(&vt8231_isa_info);
}

type_init(vt82c686b_register_types)
