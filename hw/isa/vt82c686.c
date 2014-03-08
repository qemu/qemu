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

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/isa/vt82c686.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus.h"
#include "hw/pci/pci.h"
#include "hw/isa/isa.h"
#include "hw/sysbus.h"
#include "hw/mips/mips.h"
#include "hw/isa/apm.h"
#include "hw/acpi/acpi.h"
#include "hw/i2c/pm_smbus.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"
#include "exec/address-spaces.h"

//#define DEBUG_VT82C686B

#ifdef DEBUG_VT82C686B
#define DPRINTF(fmt, ...) fprintf(stderr, "%s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#endif

typedef struct SuperIOConfig
{
    uint8_t config[0xff];
    uint8_t index;
    uint8_t data;
} SuperIOConfig;

typedef struct VT82C686BState {
    PCIDevice dev;
    MemoryRegion superio;
    SuperIOConfig superio_conf;
} VT82C686BState;

static void superio_ioport_writeb(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned size)
{
    int can_write;
    SuperIOConfig *superio_conf = opaque;

    DPRINTF("superio_ioport_writeb  address 0x%x  val 0x%x\n", addr, data);
    if (addr == 0x3f0) {
        superio_conf->index = data & 0xff;
    } else {
        /* 0x3f1 */
        switch (superio_conf->index) {
        case 0x00 ... 0xdf:
        case 0xe4:
        case 0xe5:
        case 0xe9 ... 0xed:
        case 0xf3:
        case 0xf5:
        case 0xf7:
        case 0xf9 ... 0xfb:
        case 0xfd ... 0xff:
            can_write = 0;
            break;
        default:
            can_write = 1;

            if (can_write) {
                switch (superio_conf->index) {
                case 0xe7:
                    if ((data & 0xff) != 0xfe) {
                        DPRINTF("chage uart 1 base. unsupported yet\n");
                    }
                    break;
                case 0xe8:
                    if ((data & 0xff) != 0xbe) {
                        DPRINTF("chage uart 2 base. unsupported yet\n");
                    }
                    break;

                default:
                    superio_conf->config[superio_conf->index] = data & 0xff;
                }
            }
        }
        superio_conf->config[superio_conf->index] = data & 0xff;
    }
}

static uint64_t superio_ioport_readb(void *opaque, hwaddr addr, unsigned size)
{
    SuperIOConfig *superio_conf = opaque;

    DPRINTF("superio_ioport_readb  address 0x%x\n", addr);
    return (superio_conf->config[superio_conf->index]);
}

static const MemoryRegionOps superio_ops = {
    .read = superio_ioport_readb,
    .write = superio_ioport_writeb,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void vt82c686b_reset(void * opaque)
{
    PCIDevice *d = opaque;
    uint8_t *pci_conf = d->config;
    VT82C686BState *vt82c = DO_UPCAST(VT82C686BState, dev, d);

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

    vt82c->superio_conf.config[0xe0] = 0x3c;
    vt82c->superio_conf.config[0xe2] = 0x03;
    vt82c->superio_conf.config[0xe3] = 0xfc;
    vt82c->superio_conf.config[0xe6] = 0xde;
    vt82c->superio_conf.config[0xe7] = 0xfe;
    vt82c->superio_conf.config[0xe8] = 0xbe;
}

/* write config pci function0 registers. PCI-ISA bridge */
static void vt82c686b_write_config(PCIDevice * d, uint32_t address,
                                   uint32_t val, int len)
{
    VT82C686BState *vt686 = DO_UPCAST(VT82C686BState, dev, d);

    DPRINTF("vt82c686b_write_config  address 0x%x  val 0x%x len 0x%x\n",
           address, val, len);

    pci_default_write_config(d, address, val, len);
    if (address == 0x85) {  /* enable or disable super IO configure */
        memory_region_set_enabled(&vt686->superio, val & 0x2);
    }
}

#define ACPI_DBG_IO_ADDR  0xb044

typedef struct VT686PMState {
    PCIDevice dev;
    MemoryRegion io;
    ACPIREGS ar;
    APMState apm;
    PMSMBus smb;
    uint32_t smb_io_base;
} VT686PMState;

typedef struct VT686AC97State {
    PCIDevice dev;
} VT686AC97State;

typedef struct VT686MC97State {
    PCIDevice dev;
} VT686MC97State;

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

static void pm_write_config(PCIDevice *d,
                            uint32_t address, uint32_t val, int len)
{
    DPRINTF("pm_write_config  address 0x%x  val 0x%x len 0x%x\n",
           address, val, len);
    pci_default_write_config(d, address, val, len);
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
    .minimum_version_id_old = 1,
    .post_load = vmstate_acpi_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, VT686PMState),
        VMSTATE_UINT16(ar.pm1.evt.sts, VT686PMState),
        VMSTATE_UINT16(ar.pm1.evt.en, VT686PMState),
        VMSTATE_UINT16(ar.pm1.cnt.cnt, VT686PMState),
        VMSTATE_STRUCT(apm, VT686PMState, 0, vmstate_apm, APMState),
        VMSTATE_TIMER(ar.tmr.timer, VT686PMState),
        VMSTATE_INT64(ar.tmr.overflow_time, VT686PMState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * TODO: vt82c686b_ac97_init() and vt82c686b_mc97_init()
 * just register a PCI device now, functionalities will be implemented later.
 */

static int vt82c686b_ac97_initfn(PCIDevice *dev)
{
    VT686AC97State *s = DO_UPCAST(VT686AC97State, dev, dev);
    uint8_t *pci_conf = s->dev.config;

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_INVALIDATE |
                 PCI_COMMAND_PARITY);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_CAP_LIST |
                 PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_long(pci_conf + PCI_INTERRUPT_PIN, 0x03);

    return 0;
}

void vt82c686b_ac97_init(PCIBus *bus, int devfn)
{
    PCIDevice *dev;

    dev = pci_create(bus, devfn, "VT82C686B_AC97");
    qdev_init_nofail(&dev->qdev);
}

static void via_ac97_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = vt82c686b_ac97_initfn;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_AC97;
    k->revision = 0x50;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "AC97";
}

static const TypeInfo via_ac97_info = {
    .name          = "VT82C686B_AC97",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VT686AC97State),
    .class_init    = via_ac97_class_init,
};

static int vt82c686b_mc97_initfn(PCIDevice *dev)
{
    VT686MC97State *s = DO_UPCAST(VT686MC97State, dev, dev);
    uint8_t *pci_conf = s->dev.config;

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_INVALIDATE |
                 PCI_COMMAND_VGA_PALETTE);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_long(pci_conf + PCI_INTERRUPT_PIN, 0x03);

    return 0;
}

void vt82c686b_mc97_init(PCIBus *bus, int devfn)
{
    PCIDevice *dev;

    dev = pci_create(bus, devfn, "VT82C686B_MC97");
    qdev_init_nofail(&dev->qdev);
}

static void via_mc97_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = vt82c686b_mc97_initfn;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_MC97;
    k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
    k->revision = 0x30;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "MC97";
}

static const TypeInfo via_mc97_info = {
    .name          = "VT82C686B_MC97",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VT686MC97State),
    .class_init    = via_mc97_class_init,
};

/* vt82c686 pm init */
static int vt82c686b_pm_initfn(PCIDevice *dev)
{
    VT686PMState *s = DO_UPCAST(VT686PMState, dev, dev);
    uint8_t *pci_conf;

    pci_conf = s->dev.config;
    pci_set_word(pci_conf + PCI_COMMAND, 0);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_FAST_BACK |
                 PCI_STATUS_DEVSEL_MEDIUM);

    /* 0x48-0x4B is Power Management I/O Base */
    pci_set_long(pci_conf + 0x48, 0x00000001);

    /* SMB ports:0xeee0~0xeeef */
    s->smb_io_base =((s->smb_io_base & 0xfff0) + 0x0);
    pci_conf[0x90] = s->smb_io_base | 1;
    pci_conf[0x91] = s->smb_io_base >> 8;
    pci_conf[0xd2] = 0x90;
    pm_smbus_init(&s->dev.qdev, &s->smb);
    memory_region_add_subregion(get_system_io(), s->smb_io_base, &s->smb.io);

    apm_init(dev, &s->apm, NULL, s);

    memory_region_init(&s->io, OBJECT(dev), "vt82c686-pm", 64);
    memory_region_set_enabled(&s->io, false);
    memory_region_add_subregion(get_system_io(), 0, &s->io);

    acpi_pm_tmr_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_evt_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_cnt_init(&s->ar, &s->io, 2);

    return 0;
}

I2CBus *vt82c686b_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                          qemu_irq sci_irq)
{
    PCIDevice *dev;
    VT686PMState *s;

    dev = pci_create(bus, devfn, "VT82C686B_PM");
    qdev_prop_set_uint32(&dev->qdev, "smb_io_base", smb_io_base);

    s = DO_UPCAST(VT686PMState, dev, dev);

    qdev_init_nofail(&dev->qdev);

    return s->smb.smbus;
}

static Property via_pm_properties[] = {
    DEFINE_PROP_UINT32("smb_io_base", VT686PMState, smb_io_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void via_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = vt82c686b_pm_initfn;
    k->config_write = pm_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_ACPI;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    k->revision = 0x40;
    dc->desc = "PM";
    dc->vmsd = &vmstate_acpi;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->props = via_pm_properties;
}

static const TypeInfo via_pm_info = {
    .name          = "VT82C686B_PM",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VT686PMState),
    .class_init    = via_pm_class_init,
};

static const VMStateDescription vmstate_via = {
    .name = "vt82c686b",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, VT82C686BState),
        VMSTATE_END_OF_LIST()
    }
};

/* init the PCI-to-ISA bridge */
static int vt82c686b_initfn(PCIDevice *d)
{
    VT82C686BState *vt82c = DO_UPCAST(VT82C686BState, dev, d);
    uint8_t *pci_conf;
    ISABus *isa_bus;
    uint8_t *wmask;
    int i;

    isa_bus = isa_bus_new(&d->qdev, pci_address_space_io(d));

    pci_conf = d->config;
    pci_config_set_prog_interface(pci_conf, 0x0);

    wmask = d->wmask;
    for (i = 0x00; i < 0xff; i++) {
       if (i<=0x03 || (i>=0x08 && i<=0x3f)) {
           wmask[i] = 0x00;
       }
    }

    memory_region_init_io(&vt82c->superio, OBJECT(d), &superio_ops,
                          &vt82c->superio_conf, "superio", 2);
    memory_region_set_enabled(&vt82c->superio, false);
    /* The floppy also uses 0x3f0 and 0x3f1.
     * But we do not emulate a floppy, so just set it here. */
    memory_region_add_subregion(isa_bus->address_space_io, 0x3f0,
                                &vt82c->superio);

    qemu_register_reset(vt82c686b_reset, d);

    return 0;
}

ISABus *vt82c686b_init(PCIBus *bus, int devfn)
{
    PCIDevice *d;

    d = pci_create_simple_multifunction(bus, devfn, true, "VT82C686B");

    return ISA_BUS(qdev_get_child_bus(DEVICE(d), "isa.0"));
}

static void via_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = vt82c686b_initfn;
    k->config_write = vt82c686b_write_config;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_ISA_BRIDGE;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    k->revision = 0x40;
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_via;
    /*
     * Reason: part of VIA VT82C686 southbridge, needs to be wired up,
     * e.g. by mips_fulong2e_init()
     */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo via_info = {
    .name          = "VT82C686B",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VT82C686BState),
    .class_init    = via_class_init,
};

static void vt82c686b_register_types(void)
{
    type_register_static(&via_ac97_info);
    type_register_static(&via_mc97_info);
    type_register_static(&via_pm_info);
    type_register_static(&via_info);
}

type_init(vt82c686b_register_types)
