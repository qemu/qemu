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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "vl.h"

//#define DEBUG

/* i82731AB (PIIX4) compatible power management function */
#define PM_FREQ 3579545

/* XXX: make them variable */
#define PM_IO_BASE        0xb000
#define SMI_CMD_IO_ADDR   0xb040
#define ACPI_DBG_IO_ADDR  0xb044

typedef struct PIIX4PMState {
    PCIDevice dev;
    uint16_t pmsts;
    uint16_t pmen;
    uint16_t pmcntrl;
    QEMUTimer *tmr_timer;
    int64_t tmr_overflow_time;
} PIIX4PMState;

#define RTC_EN (1 << 10)
#define PWRBTN_EN (1 << 8)
#define GBL_EN (1 << 5)
#define TMROF_EN (1 << 0)

#define SCI_EN (1 << 0)

#define SUS_EN (1 << 13)

/* Note: only used for ACPI bios init. Could be deleted when ACPI init
   is integrated in Bochs BIOS */
static PIIX4PMState *piix4_pm_state;

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
    return pmsts;
}

static void pm_update_sci(PIIX4PMState *s)
{
    int sci_level, pmsts;
    int64_t expire_time;
    
    pmsts = get_pmsts(s);
    sci_level = (((pmsts & s->pmen) & 
                  (RTC_EN | PWRBTN_EN | GBL_EN | TMROF_EN)) != 0);
    pci_set_irq(&s->dev, 0, sci_level);
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
                sus_typ = (val >> 10) & 3;
                switch(sus_typ) {
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

static void smi_cmd_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PIIX4PMState *s = opaque;
#ifdef DEBUG
    printf("SMI cmd val=0x%02x\n", val);
#endif
    switch(val) {
    case 0xf0: /* ACPI disable */
        s->pmcntrl &= ~SCI_EN;
        break;
    case 0xf1: /* ACPI enable */
        s->pmcntrl |= SCI_EN;
        break;
    }
}

static void acpi_dbg_writel(void *opaque, uint32_t addr, uint32_t val)
{
#if defined(DEBUG)
    printf("ACPI: DBG: 0x%08x\n", val);
#endif
}

/* XXX: we still add it to the PIIX3 and we count on the fact that
   OSes are smart enough to accept this strange configuration */
void piix4_pm_init(PCIBus *bus, int devfn)
{
    PIIX4PMState *s;
    uint8_t *pci_conf;
    uint32_t pm_io_base;

    s = (PIIX4PMState *)pci_register_device(bus,
                                         "PM", sizeof(PIIX4PMState),
                                         devfn, NULL, NULL);
    pci_conf = s->dev.config;
    pci_conf[0x00] = 0x86;
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x13;
    pci_conf[0x03] = 0x71;
    pci_conf[0x08] = 0x00; // revision number
    pci_conf[0x09] = 0x00;
    pci_conf[0x0a] = 0x80; // other bridge device
    pci_conf[0x0b] = 0x06; // bridge device
    pci_conf[0x0e] = 0x00; // header_type
    pci_conf[0x3d] = 0x01; // interrupt pin 1
    pci_conf[0x60] = 0x10; // release number
    
    pm_io_base = PM_IO_BASE;
    pci_conf[0x40] = pm_io_base | 1;
    pci_conf[0x41] = pm_io_base >> 8;
    register_ioport_write(pm_io_base, 64, 2, pm_ioport_writew, s);
    register_ioport_read(pm_io_base, 64, 2, pm_ioport_readw, s);
    register_ioport_write(pm_io_base, 64, 4, pm_ioport_writel, s);
    register_ioport_read(pm_io_base, 64, 4, pm_ioport_readl, s);
    
    register_ioport_write(SMI_CMD_IO_ADDR, 1, 1, smi_cmd_writeb, s);
    register_ioport_write(ACPI_DBG_IO_ADDR, 4, 4, acpi_dbg_writel, s);

    s->tmr_timer = qemu_new_timer(vm_clock, pm_tmr_timer, s);
    piix4_pm_state = s;
}

/* ACPI tables */
/* XXX: move them in the Bochs BIOS ? */

/*************************************************/

/* Table structure from Linux kernel (the ACPI tables are under the
   BSD license) */

#define ACPI_TABLE_HEADER_DEF   /* ACPI common table header */ \
	uint8_t                            signature [4];          /* ACPI signature (4 ASCII characters) */\
	uint32_t                             length;                 /* Length of table, in bytes, including header */\
	uint8_t                              revision;               /* ACPI Specification minor version # */\
	uint8_t                              checksum;               /* To make sum of entire table == 0 */\
	uint8_t                            oem_id [6];             /* OEM identification */\
	uint8_t                            oem_table_id [8];       /* OEM table identification */\
	uint32_t                             oem_revision;           /* OEM revision number */\
	uint8_t                            asl_compiler_id [4];    /* ASL compiler vendor ID */\
	uint32_t                             asl_compiler_revision;  /* ASL compiler revision number */


struct acpi_table_header         /* ACPI common table header */
{
	ACPI_TABLE_HEADER_DEF
};

struct rsdp_descriptor         /* Root System Descriptor Pointer */
{
	uint8_t                            signature [8];          /* ACPI signature, contains "RSD PTR " */
	uint8_t                              checksum;               /* To make sum of struct == 0 */
	uint8_t                            oem_id [6];             /* OEM identification */
	uint8_t                              revision;               /* Must be 0 for 1.0, 2 for 2.0 */
	uint32_t                             rsdt_physical_address;  /* 32-bit physical address of RSDT */
	uint32_t                             length;                 /* XSDT Length in bytes including hdr */
	uint64_t                             xsdt_physical_address;  /* 64-bit physical address of XSDT */
	uint8_t                              extended_checksum;      /* Checksum of entire table */
	uint8_t                            reserved [3];           /* Reserved field must be 0 */
};

/*
 * ACPI 1.0 Root System Description Table (RSDT)
 */
struct rsdt_descriptor_rev1
{
	ACPI_TABLE_HEADER_DEF                           /* ACPI common table header */
	uint32_t                             table_offset_entry [2]; /* Array of pointers to other */
			 /* ACPI tables */
};

/*
 * ACPI 1.0 Firmware ACPI Control Structure (FACS)
 */
struct facs_descriptor_rev1
{
	uint8_t                            signature[4];           /* ACPI Signature */
	uint32_t                             length;                 /* Length of structure, in bytes */
	uint32_t                             hardware_signature;     /* Hardware configuration signature */
	uint32_t                             firmware_waking_vector; /* ACPI OS waking vector */
	uint32_t                             global_lock;            /* Global Lock */
	uint32_t                             S4bios_f        : 1;    /* Indicates if S4BIOS support is present */
	uint32_t                             reserved1       : 31;   /* Must be 0 */
	uint8_t                              resverved3 [40];        /* Reserved - must be zero */
};


/*
 * ACPI 1.0 Fixed ACPI Description Table (FADT)
 */
struct fadt_descriptor_rev1
{
	ACPI_TABLE_HEADER_DEF                           /* ACPI common table header */
	uint32_t                             firmware_ctrl;          /* Physical address of FACS */
	uint32_t                             dsdt;                   /* Physical address of DSDT */
	uint8_t                              model;                  /* System Interrupt Model */
	uint8_t                              reserved1;              /* Reserved */
	uint16_t                             sci_int;                /* System vector of SCI interrupt */
	uint32_t                             smi_cmd;                /* Port address of SMI command port */
	uint8_t                              acpi_enable;            /* Value to write to smi_cmd to enable ACPI */
	uint8_t                              acpi_disable;           /* Value to write to smi_cmd to disable ACPI */
	uint8_t                              S4bios_req;             /* Value to write to SMI CMD to enter S4BIOS state */
	uint8_t                              reserved2;              /* Reserved - must be zero */
	uint32_t                             pm1a_evt_blk;           /* Port address of Power Mgt 1a acpi_event Reg Blk */
	uint32_t                             pm1b_evt_blk;           /* Port address of Power Mgt 1b acpi_event Reg Blk */
	uint32_t                             pm1a_cnt_blk;           /* Port address of Power Mgt 1a Control Reg Blk */
	uint32_t                             pm1b_cnt_blk;           /* Port address of Power Mgt 1b Control Reg Blk */
	uint32_t                             pm2_cnt_blk;            /* Port address of Power Mgt 2 Control Reg Blk */
	uint32_t                             pm_tmr_blk;             /* Port address of Power Mgt Timer Ctrl Reg Blk */
	uint32_t                             gpe0_blk;               /* Port addr of General Purpose acpi_event 0 Reg Blk */
	uint32_t                             gpe1_blk;               /* Port addr of General Purpose acpi_event 1 Reg Blk */
	uint8_t                              pm1_evt_len;            /* Byte length of ports at pm1_x_evt_blk */
	uint8_t                              pm1_cnt_len;            /* Byte length of ports at pm1_x_cnt_blk */
	uint8_t                              pm2_cnt_len;            /* Byte Length of ports at pm2_cnt_blk */
	uint8_t                              pm_tmr_len;              /* Byte Length of ports at pm_tm_blk */
	uint8_t                              gpe0_blk_len;           /* Byte Length of ports at gpe0_blk */
	uint8_t                              gpe1_blk_len;           /* Byte Length of ports at gpe1_blk */
	uint8_t                              gpe1_base;              /* Offset in gpe model where gpe1 events start */
	uint8_t                              reserved3;              /* Reserved */
	uint16_t                             plvl2_lat;              /* Worst case HW latency to enter/exit C2 state */
	uint16_t                             plvl3_lat;              /* Worst case HW latency to enter/exit C3 state */
	uint16_t                             flush_size;             /* Size of area read to flush caches */
	uint16_t                             flush_stride;           /* Stride used in flushing caches */
	uint8_t                              duty_offset;            /* Bit location of duty cycle field in p_cnt reg */
	uint8_t                              duty_width;             /* Bit width of duty cycle field in p_cnt reg */
	uint8_t                              day_alrm;               /* Index to day-of-month alarm in RTC CMOS RAM */
	uint8_t                              mon_alrm;               /* Index to month-of-year alarm in RTC CMOS RAM */
	uint8_t                              century;                /* Index to century in RTC CMOS RAM */
	uint8_t                              reserved4;              /* Reserved */
	uint8_t                              reserved4a;             /* Reserved */
	uint8_t                              reserved4b;             /* Reserved */
#if 0
	uint32_t                             wb_invd         : 1;    /* The wbinvd instruction works properly */
	uint32_t                             wb_invd_flush   : 1;    /* The wbinvd flushes but does not invalidate */
	uint32_t                             proc_c1         : 1;    /* All processors support C1 state */
	uint32_t                             plvl2_up        : 1;    /* C2 state works on MP system */
	uint32_t                             pwr_button      : 1;    /* Power button is handled as a generic feature */
	uint32_t                             sleep_button    : 1;    /* Sleep button is handled as a generic feature, or not present */
	uint32_t                             fixed_rTC       : 1;    /* RTC wakeup stat not in fixed register space */
	uint32_t                             rtcs4           : 1;    /* RTC wakeup stat not possible from S4 */
	uint32_t                             tmr_val_ext     : 1;    /* The tmr_val width is 32 bits (0 = 24 bits) */
	uint32_t                             reserved5       : 23;   /* Reserved - must be zero */
#else
        uint32_t flags;
#endif
};

/*
 * MADT values and structures
 */

/* Values for MADT PCATCompat */

#define DUAL_PIC                0
#define MULTIPLE_APIC           1


/* Master MADT */

struct multiple_apic_table
{
	ACPI_TABLE_HEADER_DEF                           /* ACPI common table header */
	uint32_t                             local_apic_address;     /* Physical address of local APIC */
#if 0
	uint32_t                             PCATcompat      : 1;    /* A one indicates system also has dual 8259s */
	uint32_t                             reserved1       : 31;
#else
        uint32_t                             flags;
#endif
};


/* Values for Type in APIC_HEADER_DEF */

#define APIC_PROCESSOR          0
#define APIC_IO                 1
#define APIC_XRUPT_OVERRIDE     2
#define APIC_NMI                3
#define APIC_LOCAL_NMI          4
#define APIC_ADDRESS_OVERRIDE   5
#define APIC_IO_SAPIC           6
#define APIC_LOCAL_SAPIC        7
#define APIC_XRUPT_SOURCE       8
#define APIC_RESERVED           9           /* 9 and greater are reserved */

/*
 * MADT sub-structures (Follow MULTIPLE_APIC_DESCRIPTION_TABLE)
 */
#define APIC_HEADER_DEF                     /* Common APIC sub-structure header */\
	uint8_t                              type; \
	uint8_t                              length;

/* Sub-structures for MADT */

struct madt_processor_apic
{
	APIC_HEADER_DEF
	uint8_t                              processor_id;           /* ACPI processor id */
	uint8_t                              local_apic_id;          /* Processor's local APIC id */
#if 0
	uint32_t                             processor_enabled: 1;   /* Processor is usable if set */
	uint32_t                             reserved2       : 31;   /* Reserved, must be zero */
#else
        uint32_t flags;
#endif
};

struct madt_io_apic
{
	APIC_HEADER_DEF
	uint8_t                              io_apic_id;             /* I/O APIC ID */
	uint8_t                              reserved;               /* Reserved - must be zero */
	uint32_t                             address;                /* APIC physical address */
	uint32_t                             interrupt;              /* Global system interrupt where INTI
			  * lines start */
};

#include "acpi-dsdt.hex"

static int acpi_checksum(const uint8_t *data, int len)
{
    int sum, i;
    sum = 0;
    for(i = 0; i < len; i++)
        sum += data[i];
    return (-sum) & 0xff;
}

static void acpi_build_table_header(struct acpi_table_header *h, 
                                    char *sig, int len)
{
    memcpy(h->signature, sig, 4);
    h->length = cpu_to_le32(len);
    h->revision = 0;
    memcpy(h->oem_id, "QEMU  ", 6);
    memcpy(h->oem_table_id, "QEMU", 4);
    memcpy(h->oem_table_id + 4, sig, 4);
    h->oem_revision = cpu_to_le32(1);
    memcpy(h->asl_compiler_id, "QEMU", 4);
    h->asl_compiler_revision = cpu_to_le32(1);
    h->checksum = acpi_checksum((void *)h, len);
}

#define ACPI_TABLES_BASE 0x000e8000

/* base_addr must be a multiple of 4KB */
void acpi_bios_init(void)
{
    struct rsdp_descriptor *rsdp;
    struct rsdt_descriptor_rev1 *rsdt;
    struct fadt_descriptor_rev1 *fadt;
    struct facs_descriptor_rev1 *facs;
    struct multiple_apic_table *madt;
    uint8_t *dsdt;
    uint32_t base_addr, rsdt_addr, fadt_addr, addr, facs_addr, dsdt_addr;
    uint32_t pm_io_base, acpi_tables_size, madt_addr, madt_size;
    int i;

    /* compute PCI I/O addresses */
    pm_io_base = (piix4_pm_state->dev.config[0x40] | 
        (piix4_pm_state->dev.config[0x41] << 8)) & ~0x3f;
    
    base_addr = ACPI_TABLES_BASE;

    /* reserve memory space for tables */
    addr = base_addr;
    rsdp = (void *)(phys_ram_base + addr);
    addr += sizeof(*rsdp);

    rsdt_addr = addr;
    rsdt = (void *)(phys_ram_base + addr);
    addr += sizeof(*rsdt);
    
    fadt_addr = addr;
    fadt = (void *)(phys_ram_base + addr);
    addr += sizeof(*fadt);

    /* XXX: FACS should be in RAM */
    addr = (addr + 63) & ~63; /* 64 byte alignment for FACS */
    facs_addr = addr;
    facs = (void *)(phys_ram_base + addr);
    addr += sizeof(*facs);

    dsdt_addr = addr;
    dsdt = (void *)(phys_ram_base + addr);
    addr += sizeof(AmlCode);

    addr = (addr + 7) & ~7;
    madt_addr = addr;
    madt_size = sizeof(*madt) + 
        sizeof(struct madt_processor_apic) * smp_cpus +
        sizeof(struct madt_io_apic);
    madt = (void *)(phys_ram_base + addr);
    addr += madt_size;

    acpi_tables_size = addr - base_addr;

    cpu_register_physical_memory(base_addr, acpi_tables_size, 
                                 base_addr | IO_MEM_ROM);
    
    /* RSDP */
    memset(rsdp, 0, sizeof(*rsdp));
    memcpy(rsdp->signature, "RSD PTR ", 8);
    memcpy(rsdp->oem_id, "QEMU  ", 6);
    rsdp->rsdt_physical_address = cpu_to_le32(rsdt_addr);
    rsdp->checksum = acpi_checksum((void *)rsdp, 20);
    
    /* RSDT */
    rsdt->table_offset_entry[0] = cpu_to_le32(fadt_addr);
    rsdt->table_offset_entry[1] = cpu_to_le32(madt_addr);
    acpi_build_table_header((struct acpi_table_header *)rsdt, 
                            "RSDT", sizeof(*rsdt));
    
    /* FADT */
    memset(fadt, 0, sizeof(*fadt));
    fadt->firmware_ctrl = cpu_to_le32(facs_addr);
    fadt->dsdt = cpu_to_le32(dsdt_addr);
    fadt->model = 1;
    fadt->reserved1 = 0;
    fadt->sci_int = cpu_to_le16(piix4_pm_state->dev.config[0x3c]);
    fadt->smi_cmd = cpu_to_le32(SMI_CMD_IO_ADDR);
    fadt->acpi_enable = 0xf1;
    fadt->acpi_disable = 0xf0;
    fadt->pm1a_evt_blk = cpu_to_le32(pm_io_base);
    fadt->pm1a_cnt_blk = cpu_to_le32(pm_io_base + 0x04);
    fadt->pm_tmr_blk = cpu_to_le32(pm_io_base + 0x08);
    fadt->pm1_evt_len = 4;
    fadt->pm1_cnt_len = 2;
    fadt->pm_tmr_len = 4;
    fadt->plvl2_lat = cpu_to_le16(50);
    fadt->plvl3_lat = cpu_to_le16(50);
    fadt->plvl3_lat = cpu_to_le16(50);
    /* WBINVD + PROC_C1 + PWR_BUTTON + SLP_BUTTON + FIX_RTC */
    fadt->flags = cpu_to_le32((1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 6));
    acpi_build_table_header((struct acpi_table_header *)fadt, "FACP", 
                            sizeof(*fadt));

    /* FACS */
    memset(facs, 0, sizeof(*facs));
    memcpy(facs->signature, "FACS", 4);
    facs->length = cpu_to_le32(sizeof(*facs));

    /* DSDT */
    memcpy(dsdt, AmlCode, sizeof(AmlCode));

    /* MADT */
    {
        struct madt_processor_apic *apic;
        struct madt_io_apic *io_apic;

        memset(madt, 0, madt_size);
        madt->local_apic_address = cpu_to_le32(0xfee00000);
        madt->flags = cpu_to_le32(1);
        apic = (void *)(madt + 1);
        for(i=0;i<smp_cpus;i++) {
            apic->type = APIC_PROCESSOR;
            apic->length = sizeof(*apic);
            apic->processor_id = i;
            apic->local_apic_id = i;
            apic->flags = cpu_to_le32(1);
            apic++;
        }
        io_apic = (void *)apic;
        io_apic->type = APIC_IO;
        io_apic->length = sizeof(*io_apic);
        io_apic->io_apic_id = smp_cpus;
        io_apic->address = cpu_to_le32(0xfec00000);
        io_apic->interrupt = cpu_to_le32(0);

        acpi_build_table_header((struct acpi_table_header *)madt, 
                                "APIC", madt_size);
    }
}
