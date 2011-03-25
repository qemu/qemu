/*
 * VT82C686B south bridge support
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2009 chenming (chenming@rdc.faw.com.cn)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 * This code is licensed under the GNU GPL v2.
 */

#include "hw.h"
#include "pc.h"
#include "vt82c686.h"
#include "i2c.h"
#include "smbus.h"
#include "pci.h"
#include "isa.h"
#include "sysbus.h"
#include "mips.h"
#include "apm.h"
#include "acpi.h"
#include "pm_smbus.h"
#include "sysemu.h"
#include "qemu-timer.h"

typedef uint32_t pci_addr_t;
#include "pci_host.h"
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
    SuperIOConfig superio_conf;
} VT82C686BState;

static void superio_ioport_writeb(void *opaque, uint32_t addr, uint32_t data)
{
    int can_write;
    SuperIOConfig *superio_conf = opaque;

    DPRINTF("superio_ioport_writeb  address 0x%x  val 0x%x  \n", addr, data);
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
                        DPRINTF("chage uart 1 base. unsupported yet \n");
                    }
                    break;
                case 0xe8:
                    if ((data & 0xff) != 0xbe) {
                        DPRINTF("chage uart 2 base. unsupported yet \n");
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

static uint32_t superio_ioport_readb(void *opaque, uint32_t addr)
{
    SuperIOConfig *superio_conf = opaque;

    DPRINTF("superio_ioport_readb  address 0x%x   \n", addr);
    return (superio_conf->config[superio_conf->index]);
}

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

    DPRINTF("vt82c686b_write_config  address 0x%x  val 0x%x len 0x%x \n",
           address, val, len);

    pci_default_write_config(d, address, val, len);
    if (address == 0x85) {  /* enable or disable super IO configure */
        if (val & 0x2) {
            /* floppy also uses 0x3f0 and 0x3f1.
             * But we do not emulate flopy,so just set it here. */
            isa_unassign_ioport(0x3f0, 2);
            register_ioport_read(0x3f0, 2, 1, superio_ioport_readb,
                                 &vt686->superio_conf);
            register_ioport_write(0x3f0, 2, 1, superio_ioport_writeb,
                                  &vt686->superio_conf);
        } else {
            isa_unassign_ioport(0x3f0, 2);
        }
    }
}

#define ACPI_DBG_IO_ADDR  0xb044

typedef struct VT686PMState {
    PCIDevice dev;
    ACPIPM1EVT pm1a;
    uint16_t pmcntrl;
    APMState apm;
    ACPIPMTimer tmr;
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

    pmsts = acpi_pm1_evt_get_sts(&s->pm1a, s->tmr.overflow_time);
    sci_level = (((pmsts & s->pm1a.en) &
                  (ACPI_BITMASK_RT_CLOCK_ENABLE |
                   ACPI_BITMASK_POWER_BUTTON_ENABLE |
                   ACPI_BITMASK_GLOBAL_LOCK_ENABLE |
                   ACPI_BITMASK_TIMER_ENABLE)) != 0);
    qemu_set_irq(s->dev.irq[0], sci_level);
    /* schedule a timer interruption if needed */
    acpi_pm_tmr_update(&s->tmr, (s->pm1a.en & ACPI_BITMASK_TIMER_ENABLE) &&
                       !(pmsts & ACPI_BITMASK_TIMER_STATUS));
}

static void pm_tmr_timer(ACPIPMTimer *tmr)
{
    VT686PMState *s = container_of(tmr, VT686PMState, tmr);
    pm_update_sci(s);
}

static void pm_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    VT686PMState *s = opaque;

    addr &= 0x0f;
    switch (addr) {
    case 0x00:
        acpi_pm1_evt_write_sts(&s->pm1a, &s->tmr, val);
        pm_update_sci(s);
        break;
    case 0x02:
        s->pm1a.en = val;
        pm_update_sci(s);
        break;
    case 0x04:
        {
            int sus_typ;
            s->pmcntrl = val & ~(SUS_EN);
            if (val & SUS_EN) {
                /* change suspend type */
                sus_typ = (val >> 10) & 3;
                switch (sus_typ) {
                case 0: /* soft power off */
                    qemu_system_shutdown_request();
                    break;
                default:
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
    DPRINTF("PM writew port=0x%04x val=0x%02x\n", addr, val);
}

static uint32_t pm_ioport_readw(void *opaque, uint32_t addr)
{
    VT686PMState *s = opaque;
    uint32_t val;

    addr &= 0x0f;
    switch (addr) {
    case 0x00:
        val = acpi_pm1_evt_get_sts(&s->pm1a, s->tmr.overflow_time);
        break;
    case 0x02:
        val = s->pm1a.en;
        break;
    case 0x04:
        val = s->pmcntrl;
        break;
    default:
        val = 0;
        break;
    }
    DPRINTF("PM readw port=0x%04x val=0x%02x\n", addr, val);
    return val;
}

static void pm_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    addr &= 0x0f;
    DPRINTF("PM writel port=0x%04x val=0x%08x\n", addr, val);
}

static uint32_t pm_ioport_readl(void *opaque, uint32_t addr)
{
    VT686PMState *s = opaque;
    uint32_t val;

    addr &= 0x0f;
    switch (addr) {
    case 0x08:
        val = acpi_pm_tmr_get(&s->tmr);
        break;
    default:
        val = 0;
        break;
    }
    DPRINTF("PM readl port=0x%04x val=0x%08x\n", addr, val);
    return val;
}

static void pm_io_space_update(VT686PMState *s)
{
    uint32_t pm_io_base;

    if (s->dev.config[0x80] & 1) {
        pm_io_base = pci_get_long(s->dev.config + 0x40);
        pm_io_base &= 0xffc0;

        /* XXX: need to improve memory and ioport allocation */
        DPRINTF("PM: mapping to 0x%x\n", pm_io_base);
        register_ioport_write(pm_io_base, 64, 2, pm_ioport_writew, s);
        register_ioport_read(pm_io_base, 64, 2, pm_ioport_readw, s);
        register_ioport_write(pm_io_base, 64, 4, pm_ioport_writel, s);
        register_ioport_read(pm_io_base, 64, 4, pm_ioport_readl, s);
    }
}

static void pm_write_config(PCIDevice *d,
                            uint32_t address, uint32_t val, int len)
{
    DPRINTF("pm_write_config  address 0x%x  val 0x%x len 0x%x \n",
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
        VMSTATE_UINT16(pm1a.sts, VT686PMState),
        VMSTATE_UINT16(pm1a.en, VT686PMState),
        VMSTATE_UINT16(pmcntrl, VT686PMState),
        VMSTATE_STRUCT(apm, VT686PMState, 0, vmstate_apm, APMState),
        VMSTATE_TIMER(tmr.timer, VT686PMState),
        VMSTATE_INT64(tmr.overflow_time, VT686PMState),
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

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_VIA);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_VIA_AC97);
    pci_config_set_class(pci_conf, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_config_set_revision(pci_conf, 0x50);

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

static PCIDeviceInfo via_ac97_info = {
    .qdev.name          = "VT82C686B_AC97",
    .qdev.desc          = "AC97",
    .qdev.size          = sizeof(VT686AC97State),
    .init               = vt82c686b_ac97_initfn,
};

static void vt82c686b_ac97_register(void)
{
    pci_qdev_register(&via_ac97_info);
}

device_init(vt82c686b_ac97_register);

static int vt82c686b_mc97_initfn(PCIDevice *dev)
{
    VT686MC97State *s = DO_UPCAST(VT686MC97State, dev, dev);
    uint8_t *pci_conf = s->dev.config;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_VIA);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_VIA_MC97);
    pci_config_set_class(pci_conf, PCI_CLASS_COMMUNICATION_OTHER);
    pci_config_set_revision(pci_conf, 0x30);

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

static PCIDeviceInfo via_mc97_info = {
    .qdev.name          = "VT82C686B_MC97",
    .qdev.desc          = "MC97",
    .qdev.size          = sizeof(VT686MC97State),
    .init               = vt82c686b_mc97_initfn,
};

static void vt82c686b_mc97_register(void)
{
    pci_qdev_register(&via_mc97_info);
}

device_init(vt82c686b_mc97_register);

/* vt82c686 pm init */
static int vt82c686b_pm_initfn(PCIDevice *dev)
{
    VT686PMState *s = DO_UPCAST(VT686PMState, dev, dev);
    uint8_t *pci_conf;

    pci_conf = s->dev.config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_VIA);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_VIA_ACPI);
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_OTHER);
    pci_config_set_revision(pci_conf, 0x40);

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
    register_ioport_write(s->smb_io_base, 0xf, 1, smb_ioport_writeb, &s->smb);
    register_ioport_read(s->smb_io_base, 0xf, 1, smb_ioport_readb, &s->smb);

    apm_init(&s->apm, NULL, s);

    acpi_pm_tmr_init(&s->tmr, pm_tmr_timer);

    pm_smbus_init(&s->dev.qdev, &s->smb);

    return 0;
}

i2c_bus *vt82c686b_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
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

static PCIDeviceInfo via_pm_info = {
    .qdev.name          = "VT82C686B_PM",
    .qdev.desc          = "PM",
    .qdev.size          = sizeof(VT686PMState),
    .qdev.vmsd          = &vmstate_acpi,
    .init               = vt82c686b_pm_initfn,
    .config_write       = pm_write_config,
    .qdev.props         = (Property[]) {
        DEFINE_PROP_UINT32("smb_io_base", VT686PMState, smb_io_base, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void vt82c686b_pm_register(void)
{
    pci_qdev_register(&via_pm_info);
}

device_init(vt82c686b_pm_register);

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
    uint8_t *pci_conf;
    uint8_t *wmask;
    int i;

    isa_bus_new(&d->qdev);

    pci_conf = d->config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_VIA);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_VIA_ISA_BRIDGE);
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_ISA);
    pci_config_set_prog_interface(pci_conf, 0x0);
    pci_config_set_revision(pci_conf,0x40); /* Revision 4.0 */

    wmask = d->wmask;
    for (i = 0x00; i < 0xff; i++) {
       if (i<=0x03 || (i>=0x08 && i<=0x3f)) {
           wmask[i] = 0x00;
       }
    }

    qemu_register_reset(vt82c686b_reset, d);

    return 0;
}

int vt82c686b_init(PCIBus *bus, int devfn)
{
    PCIDevice *d;

    d = pci_create_simple_multifunction(bus, devfn, true, "VT82C686B");

    return d->devfn;
}

static PCIDeviceInfo via_info = {
    .qdev.name    = "VT82C686B",
    .qdev.desc    = "ISA bridge",
    .qdev.size    = sizeof(VT82C686BState),
    .qdev.vmsd    = &vmstate_via,
    .qdev.no_user = 1,
    .init         = vt82c686b_initfn,
    .config_write = vt82c686b_write_config,
};

static void vt82c686b_register(void)
{
    pci_qdev_register(&via_info);
}
device_init(vt82c686b_register);
