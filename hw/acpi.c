/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#include "hw.h"
#include "pc.h"
#include "apm.h"
#include "pm_smbus.h"
#include "pci.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "i2c.h"
#include "smbus.h"
#include "acpi.h"

//#define DEBUG

#define ACPI_DBG_IO_ADDR  0xb044

typedef struct PIIX4PMState {
    PCIDevice dev;
    uint16_t pmsts;
    uint16_t pmen;
    uint16_t pmcntrl;

    APMState apm;

    QEMUTimer *tmr_timer;
    int64_t tmr_overflow_time;

    PMSMBus smb;

    qemu_irq irq;
    qemu_irq cmos_s3;
    qemu_irq smi_irq;
    int kvm_enabled;
} PIIX4PMState;

#define ACPI_ENABLE 0xf1
#define ACPI_DISABLE 0xf0

static PIIX4PMState *pm_state;

static uint32_t get_pmtmr(PIIX4PMState *s)
{
    uint32_t d;
    d = muldiv64(qemu_get_clock(vm_clock), PM_TIMER_FREQUENCY, get_ticks_per_sec());
    return d & 0xffffff;
}

static int get_pmsts(PIIX4PMState *s)
{
    int64_t d;

    d = muldiv64(qemu_get_clock(vm_clock), PM_TIMER_FREQUENCY,
                 get_ticks_per_sec());
    if (d >= s->tmr_overflow_time)
        s->pmsts |= ACPI_BITMASK_TIMER_STATUS;
    return s->pmsts;
}

static void pm_update_sci(PIIX4PMState *s)
{
    int sci_level, pmsts;
    int64_t expire_time;

    pmsts = get_pmsts(s);
    sci_level = (((pmsts & s->pmen) &
                  (ACPI_BITMASK_RT_CLOCK_ENABLE |
                   ACPI_BITMASK_POWER_BUTTON_ENABLE |
                   ACPI_BITMASK_GLOBAL_LOCK_ENABLE |
                   ACPI_BITMASK_TIMER_ENABLE)) != 0);
    qemu_set_irq(s->irq, sci_level);
    /* schedule a timer interruption if needed */
    if ((s->pmen & ACPI_BITMASK_TIMER_ENABLE) &&
        !(pmsts & ACPI_BITMASK_TIMER_STATUS)) {
        expire_time = muldiv64(s->tmr_overflow_time, get_ticks_per_sec(),
                               PM_TIMER_FREQUENCY);
        qemu_mod_timer(s->tmr_timer, expire_time);
    } else {
        qemu_del_timer(s->tmr_timer);
    }
}

static void pm_tmr_timer(void *opaque)
{
    PIIX4PMState *s = opaque;
    pm_update_sci(s);
}

static void pm_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    PIIX4PMState *s = opaque;
    addr &= 0x3f;
    switch(addr) {
    case 0x00:
        {
            int64_t d;
            int pmsts;
            pmsts = get_pmsts(s);
            if (pmsts & val & ACPI_BITMASK_TIMER_STATUS) {
                /* if TMRSTS is reset, then compute the new overflow time */
                d = muldiv64(qemu_get_clock(vm_clock), PM_TIMER_FREQUENCY,
                             get_ticks_per_sec());
                s->tmr_overflow_time = (d + 0x800000LL) & ~0x7fffffLL;
            }
            s->pmsts &= ~val;
            pm_update_sci(s);
        }
        break;
    case 0x02:
        s->pmen = val;
        pm_update_sci(s);
        break;
    case 0x04:
        {
            int sus_typ;
            s->pmcntrl = val & ~(ACPI_BITMASK_SLEEP_ENABLE);
            if (val & ACPI_BITMASK_SLEEP_ENABLE) {
                /* change suspend type */
                sus_typ = (val >> 10) & 7;
                switch(sus_typ) {
                case 0: /* soft power off */
                    qemu_system_shutdown_request();
                    break;
                case 1:
                    /* ACPI_BITMASK_WAKE_STATUS should be set on resume.
                       Pretend that resume was caused by power button */
                    s->pmsts |= (ACPI_BITMASK_WAKE_STATUS |
                                 ACPI_BITMASK_POWER_BUTTON_STATUS);
                    qemu_system_reset_request();
                    if (s->cmos_s3) {
                        qemu_irq_raise(s->cmos_s3);
                    }
                default:
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
#ifdef DEBUG
    printf("PM writew port=0x%04x val=0x%04x\n", addr, val);
#endif
}

static uint32_t pm_ioport_readw(void *opaque, uint32_t addr)
{
    PIIX4PMState *s = opaque;
    uint32_t val;

    addr &= 0x3f;
    switch(addr) {
    case 0x00:
        val = get_pmsts(s);
        break;
    case 0x02:
        val = s->pmen;
        break;
    case 0x04:
        val = s->pmcntrl;
        break;
    default:
        val = 0;
        break;
    }
#ifdef DEBUG
    printf("PM readw port=0x%04x val=0x%04x\n", addr, val);
#endif
    return val;
}

static void pm_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    //    PIIX4PMState *s = opaque;
#ifdef DEBUG
    addr &= 0x3f;
    printf("PM writel port=0x%04x val=0x%08x\n", addr, val);
#endif
}

static uint32_t pm_ioport_readl(void *opaque, uint32_t addr)
{
    PIIX4PMState *s = opaque;
    uint32_t val;

    addr &= 0x3f;
    switch(addr) {
    case 0x08:
        val = get_pmtmr(s);
        break;
    default:
        val = 0;
        break;
    }
#ifdef DEBUG
    printf("PM readl port=0x%04x val=0x%08x\n", addr, val);
#endif
    return val;
}

static void apm_ctrl_changed(uint32_t val, void *arg)
{
    PIIX4PMState *s = arg;

    /* ACPI specs 3.0, 4.7.2.5 */
    if (val == ACPI_ENABLE) {
        s->pmcntrl |= ACPI_BITMASK_SCI_ENABLE;
    } else if (val == ACPI_DISABLE) {
        s->pmcntrl &= ~ACPI_BITMASK_SCI_ENABLE;
    }

    if (s->dev.config[0x5b] & (1 << 1)) {
        if (s->smi_irq) {
            qemu_irq_raise(s->smi_irq);
        }
    }
}

static void acpi_dbg_writel(void *opaque, uint32_t addr, uint32_t val)
{
#if defined(DEBUG)
    printf("ACPI: DBG: 0x%08x\n", val);
#endif
}

static void pm_io_space_update(PIIX4PMState *s)
{
    uint32_t pm_io_base;

    if (s->dev.config[0x80] & 1) {
        pm_io_base = le32_to_cpu(*(uint32_t *)(s->dev.config + 0x40));
        pm_io_base &= 0xffc0;

        /* XXX: need to improve memory and ioport allocation */
#if defined(DEBUG)
        printf("PM: mapping to 0x%x\n", pm_io_base);
#endif
        register_ioport_write(pm_io_base, 64, 2, pm_ioport_writew, s);
        register_ioport_read(pm_io_base, 64, 2, pm_ioport_readw, s);
        register_ioport_write(pm_io_base, 64, 4, pm_ioport_writel, s);
        register_ioport_read(pm_io_base, 64, 4, pm_ioport_readl, s);
    }
}

static void pm_write_config(PCIDevice *d,
                            uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(d, address, val, len);
    if (range_covers_byte(address, len, 0x80))
        pm_io_space_update((PIIX4PMState *)d);
}

static int vmstate_acpi_post_load(void *opaque, int version_id)
{
    PIIX4PMState *s = opaque;

    pm_io_space_update(s);
    return 0;
}

static const VMStateDescription vmstate_acpi = {
    .name = "piix4_pm",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = vmstate_acpi_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PIIX4PMState),
        VMSTATE_UINT16(pmsts, PIIX4PMState),
        VMSTATE_UINT16(pmen, PIIX4PMState),
        VMSTATE_UINT16(pmcntrl, PIIX4PMState),
        VMSTATE_STRUCT(apm, PIIX4PMState, 0, vmstate_apm, APMState),
        VMSTATE_TIMER(tmr_timer, PIIX4PMState),
        VMSTATE_INT64(tmr_overflow_time, PIIX4PMState),
        VMSTATE_END_OF_LIST()
    }
};

static void piix4_reset(void *opaque)
{
    PIIX4PMState *s = opaque;
    uint8_t *pci_conf = s->dev.config;

    pci_conf[0x58] = 0;
    pci_conf[0x59] = 0;
    pci_conf[0x5a] = 0;
    pci_conf[0x5b] = 0;

    if (s->kvm_enabled) {
        /* Mark SMM as already inited (until KVM supports SMM). */
        pci_conf[0x5B] = 0x02;
    }
}

static void piix4_powerdown(void *opaque, int irq, int power_failing)
{
    PIIX4PMState *s = opaque;

    if (!s) {
        qemu_system_shutdown_request();
    } else if (s->pmen & ACPI_BITMASK_POWER_BUTTON_ENABLE) {
        s->pmsts |= ACPI_BITMASK_POWER_BUTTON_STATUS;
        pm_update_sci(s);
    }
}

i2c_bus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                       qemu_irq sci_irq, qemu_irq cmos_s3, qemu_irq smi_irq,
                       int kvm_enabled)
{
    PIIX4PMState *s;
    uint8_t *pci_conf;

    s = (PIIX4PMState *)pci_register_device(bus,
                                         "PM", sizeof(PIIX4PMState),
                                         devfn, NULL, pm_write_config);
    pm_state = s;
    pci_conf = s->dev.config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82371AB_3);
    pci_conf[0x06] = 0x80;
    pci_conf[0x07] = 0x02;
    pci_conf[0x08] = 0x03; // revision number
    pci_conf[0x09] = 0x00;
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_OTHER);
    pci_conf[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    pci_conf[0x3d] = 0x01; // interrupt pin 1

    pci_conf[0x40] = 0x01; /* PM io base read only bit */

    /* APM */
    apm_init(&s->apm, apm_ctrl_changed, s);

    register_ioport_write(ACPI_DBG_IO_ADDR, 4, 4, acpi_dbg_writel, s);

    s->kvm_enabled = kvm_enabled;
    if (s->kvm_enabled) {
        /* Mark SMM as already inited to prevent SMM from running.  KVM does not
         * support SMM mode. */
        pci_conf[0x5B] = 0x02;
    }

    /* XXX: which specification is used ? The i82731AB has different
       mappings */
    pci_conf[0x5f] = (parallel_hds[0] != NULL ? 0x80 : 0) | 0x10;
    pci_conf[0x63] = 0x60;
    pci_conf[0x67] = (serial_hds[0] != NULL ? 0x08 : 0) |
	(serial_hds[1] != NULL ? 0x90 : 0);

    pci_conf[0x90] = smb_io_base | 1;
    pci_conf[0x91] = smb_io_base >> 8;
    pci_conf[0xd2] = 0x09;
    register_ioport_write(smb_io_base, 64, 1, smb_ioport_writeb, &s->smb);
    register_ioport_read(smb_io_base, 64, 1, smb_ioport_readb, &s->smb);

    s->tmr_timer = qemu_new_timer(vm_clock, pm_tmr_timer, s);

    qemu_system_powerdown = *qemu_allocate_irqs(piix4_powerdown, s, 1);

    vmstate_register(0, &vmstate_acpi, s);

    pm_smbus_init(NULL, &s->smb);
    s->irq = sci_irq;
    s->cmos_s3 = cmos_s3;
    s->smi_irq = smi_irq;
    qemu_register_reset(piix4_reset, s);

    return s->smb.smbus;
}

#define GPE_BASE 0xafe0
#define PCI_BASE 0xae00
#define PCI_EJ_BASE 0xae08

struct gpe_regs {
    uint16_t sts; /* status */
    uint16_t en;  /* enabled */
};

struct pci_status {
    uint32_t up;
    uint32_t down;
};

static struct gpe_regs gpe;
static struct pci_status pci0_status;

static uint32_t gpe_read_val(uint16_t val, uint32_t addr)
{
    if (addr & 1)
        return (val >> 8) & 0xff;
    return val & 0xff;
}

static uint32_t gpe_readb(void *opaque, uint32_t addr)
{
    uint32_t val = 0;
    struct gpe_regs *g = opaque;
    switch (addr) {
        case GPE_BASE:
        case GPE_BASE + 1:
            val = gpe_read_val(g->sts, addr);
            break;
        case GPE_BASE + 2:
        case GPE_BASE + 3:
            val = gpe_read_val(g->en, addr);
            break;
        default:
            break;
    }

#if defined(DEBUG)
    printf("gpe read %x == %x\n", addr, val);
#endif
    return val;
}

static void gpe_write_val(uint16_t *cur, int addr, uint32_t val)
{
    if (addr & 1)
        *cur = (*cur & 0xff) | (val << 8);
    else
        *cur = (*cur & 0xff00) | (val & 0xff);
}

static void gpe_reset_val(uint16_t *cur, int addr, uint32_t val)
{
    uint16_t x1, x0 = val & 0xff;
    int shift = (addr & 1) ? 8 : 0;

    x1 = (*cur >> shift) & 0xff;

    x1 = x1 & ~x0;

    *cur = (*cur & (0xff << (8 - shift))) | (x1 << shift);
}

static void gpe_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    struct gpe_regs *g = opaque;
    switch (addr) {
        case GPE_BASE:
        case GPE_BASE + 1:
            gpe_reset_val(&g->sts, addr, val);
            break;
        case GPE_BASE + 2:
        case GPE_BASE + 3:
            gpe_write_val(&g->en, addr, val);
            break;
        default:
            break;
   }

#if defined(DEBUG)
    printf("gpe write %x <== %d\n", addr, val);
#endif
}

static uint32_t pcihotplug_read(void *opaque, uint32_t addr)
{
    uint32_t val = 0;
    struct pci_status *g = opaque;
    switch (addr) {
        case PCI_BASE:
            val = g->up;
            break;
        case PCI_BASE + 4:
            val = g->down;
            break;
        default:
            break;
    }

#if defined(DEBUG)
    printf("pcihotplug read %x == %x\n", addr, val);
#endif
    return val;
}

static void pcihotplug_write(void *opaque, uint32_t addr, uint32_t val)
{
    struct pci_status *g = opaque;
    switch (addr) {
        case PCI_BASE:
            g->up = val;
            break;
        case PCI_BASE + 4:
            g->down = val;
            break;
   }

#if defined(DEBUG)
    printf("pcihotplug write %x <== %d\n", addr, val);
#endif
}

static uint32_t pciej_read(void *opaque, uint32_t addr)
{
#if defined(DEBUG)
    printf("pciej read %x\n", addr);
#endif
    return 0;
}

static void pciej_write(void *opaque, uint32_t addr, uint32_t val)
{
    BusState *bus = opaque;
    DeviceState *qdev, *next;
    PCIDevice *dev;
    int slot = ffs(val) - 1;

    QLIST_FOREACH_SAFE(qdev, &bus->children, sibling, next) {
        dev = DO_UPCAST(PCIDevice, qdev, qdev);
        if (PCI_SLOT(dev->devfn) == slot) {
            qdev_free(qdev);
        }
    }


#if defined(DEBUG)
    printf("pciej write %x <== %d\n", addr, val);
#endif
}

static int piix4_device_hotplug(PCIDevice *dev, int state);

void piix4_acpi_system_hot_add_init(PCIBus *bus)
{
    register_ioport_write(GPE_BASE, 4, 1, gpe_writeb, &gpe);
    register_ioport_read(GPE_BASE, 4, 1,  gpe_readb, &gpe);

    register_ioport_write(PCI_BASE, 8, 4, pcihotplug_write, &pci0_status);
    register_ioport_read(PCI_BASE, 8, 4,  pcihotplug_read, &pci0_status);

    register_ioport_write(PCI_EJ_BASE, 4, 4, pciej_write, bus);
    register_ioport_read(PCI_EJ_BASE, 4, 4,  pciej_read, bus);

    pci_bus_hotplug(bus, piix4_device_hotplug);
}

static void enable_device(struct pci_status *p, struct gpe_regs *g, int slot)
{
    g->sts |= 2;
    p->up |= (1 << slot);
}

static void disable_device(struct pci_status *p, struct gpe_regs *g, int slot)
{
    g->sts |= 2;
    p->down |= (1 << slot);
}

static int piix4_device_hotplug(PCIDevice *dev, int state)
{
    int slot = PCI_SLOT(dev->devfn);

    pci0_status.up = 0;
    pci0_status.down = 0;
    if (state)
        enable_device(&pci0_status, &gpe, slot);
    else
        disable_device(&pci0_status, &gpe, slot);
    if (gpe.en & 2) {
        qemu_set_irq(pm_state->irq, 1);
        qemu_set_irq(pm_state->irq, 0);
    }
    return 0;
}

struct acpi_table_header
{
    char signature [4];    /* ACPI signature (4 ASCII characters) */
    uint32_t length;          /* Length of table, in bytes, including header */
    uint8_t revision;         /* ACPI Specification minor version # */
    uint8_t checksum;         /* To make sum of entire table == 0 */
    char oem_id [6];       /* OEM identification */
    char oem_table_id [8]; /* OEM table identification */
    uint32_t oem_revision;    /* OEM revision number */
    char asl_compiler_id [4]; /* ASL compiler vendor ID */
    uint32_t asl_compiler_revision; /* ASL compiler revision number */
} __attribute__((packed));

char *acpi_tables;
size_t acpi_tables_len;

static int acpi_checksum(const uint8_t *data, int len)
{
    int sum, i;
    sum = 0;
    for(i = 0; i < len; i++)
        sum += data[i];
    return (-sum) & 0xff;
}

int acpi_table_add(const char *t)
{
    static const char *dfl_id = "QEMUQEMU";
    char buf[1024], *p, *f;
    struct acpi_table_header acpi_hdr;
    unsigned long val;
    size_t off;

    memset(&acpi_hdr, 0, sizeof(acpi_hdr));
  
    if (get_param_value(buf, sizeof(buf), "sig", t)) {
        strncpy(acpi_hdr.signature, buf, 4);
    } else {
        strncpy(acpi_hdr.signature, dfl_id, 4);
    }
    if (get_param_value(buf, sizeof(buf), "rev", t)) {
        val = strtoul(buf, &p, 10);
        if (val > 255 || *p != '\0')
            goto out;
    } else {
        val = 1;
    }
    acpi_hdr.revision = (int8_t)val;

    if (get_param_value(buf, sizeof(buf), "oem_id", t)) {
        strncpy(acpi_hdr.oem_id, buf, 6);
    } else {
        strncpy(acpi_hdr.oem_id, dfl_id, 6);
    }

    if (get_param_value(buf, sizeof(buf), "oem_table_id", t)) {
        strncpy(acpi_hdr.oem_table_id, buf, 8);
    } else {
        strncpy(acpi_hdr.oem_table_id, dfl_id, 8);
    }

    if (get_param_value(buf, sizeof(buf), "oem_rev", t)) {
        val = strtol(buf, &p, 10);
        if(*p != '\0')
            goto out;
    } else {
        val = 1;
    }
    acpi_hdr.oem_revision = cpu_to_le32(val);

    if (get_param_value(buf, sizeof(buf), "asl_compiler_id", t)) {
        strncpy(acpi_hdr.asl_compiler_id, buf, 4);
    } else {
        strncpy(acpi_hdr.asl_compiler_id, dfl_id, 4);
    }

    if (get_param_value(buf, sizeof(buf), "asl_compiler_rev", t)) {
        val = strtol(buf, &p, 10);
        if(*p != '\0')
            goto out;
    } else {
        val = 1;
    }
    acpi_hdr.asl_compiler_revision = cpu_to_le32(val);
    
    if (!get_param_value(buf, sizeof(buf), "data", t)) {
         buf[0] = '\0';
    }

    acpi_hdr.length = sizeof(acpi_hdr);

    f = buf;
    while (buf[0]) {
        struct stat s;
        char *n = strchr(f, ':');
        if (n)
            *n = '\0';
        if(stat(f, &s) < 0) {
            fprintf(stderr, "Can't stat file '%s': %s\n", f, strerror(errno));
            goto out;
        }
        acpi_hdr.length += s.st_size;
        if (!n)
            break;
        *n = ':';
        f = n + 1;
    }

    if (!acpi_tables) {
        acpi_tables_len = sizeof(uint16_t);
        acpi_tables = qemu_mallocz(acpi_tables_len);
    }
    p = acpi_tables + acpi_tables_len;
    acpi_tables_len += sizeof(uint16_t) + acpi_hdr.length;
    acpi_tables = qemu_realloc(acpi_tables, acpi_tables_len);

    acpi_hdr.length = cpu_to_le32(acpi_hdr.length);
    *(uint16_t*)p = acpi_hdr.length;
    p += sizeof(uint16_t);
    memcpy(p, &acpi_hdr, sizeof(acpi_hdr));
    off = sizeof(acpi_hdr);

    f = buf;
    while (buf[0]) {
        struct stat s;
        int fd;
        char *n = strchr(f, ':');
        if (n)
            *n = '\0';
        fd = open(f, O_RDONLY);

        if(fd < 0)
            goto out;
        if(fstat(fd, &s) < 0) {
            close(fd);
            goto out;
        }

        do {
            int r;
            r = read(fd, p + off, s.st_size);
            if (r > 0) {
                off += r;
                s.st_size -= r;
            } else if ((r < 0 && errno != EINTR) || r == 0) {
                close(fd);
                goto out;
            }
        } while(s.st_size);

        close(fd);
        if (!n)
            break;
        f = n + 1;
    }

    ((struct acpi_table_header*)p)->checksum = acpi_checksum((uint8_t*)p, off);
    /* increase number of tables */
    (*(uint16_t*)acpi_tables) =
	    cpu_to_le32(le32_to_cpu(*(uint16_t*)acpi_tables) + 1);
    return 0;
out:
    if (acpi_tables) {
        qemu_free(acpi_tables);
        acpi_tables = NULL;
    }
    return -1;
}
