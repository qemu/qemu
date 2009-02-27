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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "i2c.h"
#include "smbus.h"
#include "kvm.h"

//#define DEBUG

/* i82731AB (PIIX4) compatible power management function */
#define PM_FREQ 3579545

#define ACPI_DBG_IO_ADDR  0xb044

typedef struct PIIX4PMState {
    PCIDevice dev;
    uint16_t pmsts;
    uint16_t pmen;
    uint16_t pmcntrl;
    uint8_t apmc;
    uint8_t apms;
    QEMUTimer *tmr_timer;
    int64_t tmr_overflow_time;
    i2c_bus *smbus;
    uint8_t smb_stat;
    uint8_t smb_ctl;
    uint8_t smb_cmd;
    uint8_t smb_addr;
    uint8_t smb_data0;
    uint8_t smb_data1;
    uint8_t smb_data[32];
    uint8_t smb_index;
    qemu_irq irq;
} PIIX4PMState;

#define RSM_STS (1 << 15)
#define PWRBTN_STS (1 << 8)
#define RTC_EN (1 << 10)
#define PWRBTN_EN (1 << 8)
#define GBL_EN (1 << 5)
#define TMROF_EN (1 << 0)

#define SCI_EN (1 << 0)

#define SUS_EN (1 << 13)

#define ACPI_ENABLE 0xf1
#define ACPI_DISABLE 0xf0

#define SMBHSTSTS 0x00
#define SMBHSTCNT 0x02
#define SMBHSTCMD 0x03
#define SMBHSTADD 0x04
#define SMBHSTDAT0 0x05
#define SMBHSTDAT1 0x06
#define SMBBLKDAT 0x07

static PIIX4PMState *pm_state;

static uint32_t get_pmtmr(PIIX4PMState *s)
{
    uint32_t d;
    d = muldiv64(qemu_get_clock(vm_clock), PM_FREQ, ticks_per_sec);
    return d & 0xffffff;
}

static int get_pmsts(PIIX4PMState *s)
{
    int64_t d;
    int pmsts;
    pmsts = s->pmsts;
    d = muldiv64(qemu_get_clock(vm_clock), PM_FREQ, ticks_per_sec);
    if (d >= s->tmr_overflow_time)
        s->pmsts |= TMROF_EN;
    return s->pmsts;
}

static void pm_update_sci(PIIX4PMState *s)
{
    int sci_level, pmsts;
    int64_t expire_time;

    pmsts = get_pmsts(s);
    sci_level = (((pmsts & s->pmen) &
                  (RTC_EN | PWRBTN_EN | GBL_EN | TMROF_EN)) != 0);
    qemu_set_irq(s->irq, sci_level);
    /* schedule a timer interruption if needed */
    if ((s->pmen & TMROF_EN) && !(pmsts & TMROF_EN)) {
        expire_time = muldiv64(s->tmr_overflow_time, ticks_per_sec, PM_FREQ);
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
            if (pmsts & val & TMROF_EN) {
                /* if TMRSTS is reset, then compute the new overflow time */
                d = muldiv64(qemu_get_clock(vm_clock), PM_FREQ, ticks_per_sec);
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
            s->pmcntrl = val & ~(SUS_EN);
            if (val & SUS_EN) {
                /* change suspend type */
                sus_typ = (val >> 10) & 7;
                switch(sus_typ) {
                case 0: /* soft power off */
                    qemu_system_shutdown_request();
                    break;
                case 1:
                    /* RSM_STS should be set on resume. Pretend that resume
                       was caused by power button */
                    s->pmsts |= (RSM_STS | PWRBTN_STS);
                    qemu_system_reset_request();
#if defined(TARGET_I386)
                    cmos_set_s3_resume();
#endif
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
    addr &= 0x3f;
#ifdef DEBUG
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

static void pm_smi_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PIIX4PMState *s = opaque;
    addr &= 1;
#ifdef DEBUG
    printf("pm_smi_writeb addr=0x%x val=0x%02x\n", addr, val);
#endif
    if (addr == 0) {
        s->apmc = val;

        /* ACPI specs 3.0, 4.7.2.5 */
        if (val == ACPI_ENABLE) {
            s->pmcntrl |= SCI_EN;
        } else if (val == ACPI_DISABLE) {
            s->pmcntrl &= ~SCI_EN;
        }

        if (s->dev.config[0x5b] & (1 << 1)) {
            cpu_interrupt(first_cpu, CPU_INTERRUPT_SMI);
        }
    } else {
        s->apms = val;
    }
}

static uint32_t pm_smi_readb(void *opaque, uint32_t addr)
{
    PIIX4PMState *s = opaque;
    uint32_t val;

    addr &= 1;
    if (addr == 0) {
        val = s->apmc;
    } else {
        val = s->apms;
    }
#ifdef DEBUG
    printf("pm_smi_readb addr=0x%x val=0x%02x\n", addr, val);
#endif
    return val;
}

static void acpi_dbg_writel(void *opaque, uint32_t addr, uint32_t val)
{
#if defined(DEBUG)
    printf("ACPI: DBG: 0x%08x\n", val);
#endif
}

static void smb_transaction(PIIX4PMState *s)
{
    uint8_t prot = (s->smb_ctl >> 2) & 0x07;
    uint8_t read = s->smb_addr & 0x01;
    uint8_t cmd = s->smb_cmd;
    uint8_t addr = s->smb_addr >> 1;
    i2c_bus *bus = s->smbus;

#ifdef DEBUG
    printf("SMBus trans addr=0x%02x prot=0x%02x\n", addr, prot);
#endif
    switch(prot) {
    case 0x0:
        smbus_quick_command(bus, addr, read);
        break;
    case 0x1:
        if (read) {
            s->smb_data0 = smbus_receive_byte(bus, addr);
        } else {
            smbus_send_byte(bus, addr, cmd);
        }
        break;
    case 0x2:
        if (read) {
            s->smb_data0 = smbus_read_byte(bus, addr, cmd);
        } else {
            smbus_write_byte(bus, addr, cmd, s->smb_data0);
        }
        break;
    case 0x3:
        if (read) {
            uint16_t val;
            val = smbus_read_word(bus, addr, cmd);
            s->smb_data0 = val;
            s->smb_data1 = val >> 8;
        } else {
            smbus_write_word(bus, addr, cmd, (s->smb_data1 << 8) | s->smb_data0);
        }
        break;
    case 0x5:
        if (read) {
            s->smb_data0 = smbus_read_block(bus, addr, cmd, s->smb_data);
        } else {
            smbus_write_block(bus, addr, cmd, s->smb_data, s->smb_data0);
        }
        break;
    default:
        goto error;
    }
    return;

  error:
    s->smb_stat |= 0x04;
}

static void smb_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PIIX4PMState *s = opaque;
    addr &= 0x3f;
#ifdef DEBUG
    printf("SMB writeb port=0x%04x val=0x%02x\n", addr, val);
#endif
    switch(addr) {
    case SMBHSTSTS:
        s->smb_stat = 0;
        s->smb_index = 0;
        break;
    case SMBHSTCNT:
        s->smb_ctl = val;
        if (val & 0x40)
            smb_transaction(s);
        break;
    case SMBHSTCMD:
        s->smb_cmd = val;
        break;
    case SMBHSTADD:
        s->smb_addr = val;
        break;
    case SMBHSTDAT0:
        s->smb_data0 = val;
        break;
    case SMBHSTDAT1:
        s->smb_data1 = val;
        break;
    case SMBBLKDAT:
        s->smb_data[s->smb_index++] = val;
        if (s->smb_index > 31)
            s->smb_index = 0;
        break;
    default:
        break;
    }
}

static uint32_t smb_ioport_readb(void *opaque, uint32_t addr)
{
    PIIX4PMState *s = opaque;
    uint32_t val;

    addr &= 0x3f;
    switch(addr) {
    case SMBHSTSTS:
        val = s->smb_stat;
        break;
    case SMBHSTCNT:
        s->smb_index = 0;
        val = s->smb_ctl & 0x1f;
        break;
    case SMBHSTCMD:
        val = s->smb_cmd;
        break;
    case SMBHSTADD:
        val = s->smb_addr;
        break;
    case SMBHSTDAT0:
        val = s->smb_data0;
        break;
    case SMBHSTDAT1:
        val = s->smb_data1;
        break;
    case SMBBLKDAT:
        val = s->smb_data[s->smb_index++];
        if (s->smb_index > 31)
            s->smb_index = 0;
        break;
    default:
        val = 0;
        break;
    }
#ifdef DEBUG
    printf("SMB readb port=0x%04x val=0x%02x\n", addr, val);
#endif
    return val;
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
    if (address == 0x80)
        pm_io_space_update((PIIX4PMState *)d);
}

static void pm_save(QEMUFile* f,void *opaque)
{
    PIIX4PMState *s = opaque;

    pci_device_save(&s->dev, f);

    qemu_put_be16s(f, &s->pmsts);
    qemu_put_be16s(f, &s->pmen);
    qemu_put_be16s(f, &s->pmcntrl);
    qemu_put_8s(f, &s->apmc);
    qemu_put_8s(f, &s->apms);
    qemu_put_timer(f, s->tmr_timer);
    qemu_put_be64(f, s->tmr_overflow_time);
}

static int pm_load(QEMUFile* f,void* opaque,int version_id)
{
    PIIX4PMState *s = opaque;
    int ret;

    if (version_id > 1)
        return -EINVAL;

    ret = pci_device_load(&s->dev, f);
    if (ret < 0)
        return ret;

    qemu_get_be16s(f, &s->pmsts);
    qemu_get_be16s(f, &s->pmen);
    qemu_get_be16s(f, &s->pmcntrl);
    qemu_get_8s(f, &s->apmc);
    qemu_get_8s(f, &s->apms);
    qemu_get_timer(f, s->tmr_timer);
    s->tmr_overflow_time=qemu_get_be64(f);

    pm_io_space_update(s);

    return 0;
}

static void piix4_reset(void *opaque)
{
	PIIX4PMState *s = opaque;
	uint8_t *pci_conf = s->dev.config;

	pci_conf[0x58] = 0;
	pci_conf[0x59] = 0;
	pci_conf[0x5a] = 0;
	pci_conf[0x5b] = 0;
}

i2c_bus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                       qemu_irq sci_irq)
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
    pci_conf[0x0e] = 0x00; // header_type
    pci_conf[0x3d] = 0x01; // interrupt pin 1

    pci_conf[0x40] = 0x01; /* PM io base read only bit */

    register_ioport_write(0xb2, 2, 1, pm_smi_writeb, s);
    register_ioport_read(0xb2, 2, 1, pm_smi_readb, s);

    register_ioport_write(ACPI_DBG_IO_ADDR, 4, 4, acpi_dbg_writel, s);

    if (kvm_enabled()) {
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
    register_ioport_write(smb_io_base, 64, 1, smb_ioport_writeb, s);
    register_ioport_read(smb_io_base, 64, 1, smb_ioport_readb, s);

    s->tmr_timer = qemu_new_timer(vm_clock, pm_tmr_timer, s);

    register_savevm("piix4_pm", 0, 1, pm_save, pm_load, s);

    s->smbus = i2c_init_bus();
    s->irq = sci_irq;
    qemu_register_reset(piix4_reset, s);

    return s->smbus;
}

#if defined(TARGET_I386)
void qemu_system_powerdown(void)
{
    if (!pm_state) {
        qemu_system_shutdown_request();
    } else if (pm_state->pmen & PWRBTN_EN) {
        pm_state->pmsts |= PWRBTN_EN;
	pm_update_sci(pm_state);
    }
}
#endif

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
    printf("gpe read %lx == %lx\n", addr, val);
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
    printf("gpe write %lx <== %d\n", addr, val);
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
    printf("pcihotplug read %lx == %lx\n", addr, val);
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
    printf("pcihotplug write %lx <== %d\n", addr, val);
#endif
}

static uint32_t pciej_read(void *opaque, uint32_t addr)
{
#if defined(DEBUG)
    printf("pciej read %lx == %lx\n", addr, val);
#endif
    return 0;
}

static void pciej_write(void *opaque, uint32_t addr, uint32_t val)
{
#if defined (TARGET_I386)
    int slot = ffs(val) - 1;

    pci_device_hot_remove_success(0, slot);
#endif

#if defined(DEBUG)
    printf("pciej write %lx <== %d\n", addr, val);
#endif
}

void qemu_system_hot_add_init(void)
{
    register_ioport_write(GPE_BASE, 4, 1, gpe_writeb, &gpe);
    register_ioport_read(GPE_BASE, 4, 1,  gpe_readb, &gpe);

    register_ioport_write(PCI_BASE, 8, 4, pcihotplug_write, &pci0_status);
    register_ioport_read(PCI_BASE, 8, 4,  pcihotplug_read, &pci0_status);

    register_ioport_write(PCI_EJ_BASE, 4, 4, pciej_write, NULL);
    register_ioport_read(PCI_EJ_BASE, 4, 4,  pciej_read, NULL);
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

void qemu_system_device_hot_add(int bus, int slot, int state)
{
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
        free(acpi_tables);
        acpi_tables = NULL;
    }
    return -1;
}
