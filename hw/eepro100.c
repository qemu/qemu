/*
 * QEMU i8255x (PRO100) emulation
 *
 * Copyright (C) 2006-2009 Stefan Weil
 *
 * Portions of the code are copies from grub / etherboot eepro100.c
 * and linux e100.c.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) version 3 or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Tested features (i82559c):
 *      PXE boot (i386 guest, i386 / ppc host) ok
 *      Linux networking (i386 guest, i386 / ppc / ppc64 host) ok
 *      Linux networking e100 driver (mips / mipsel guest, i386 host) ok
 *      Linux networking eepro100 driver (mipsel guest) not ok
 *      Windows networking (Vista) ???
 *
 * References:
 *
 * Intel 8255x 10/100 Mbps Ethernet Controller Family
 * Open Source Software Developer Manual
 *
 * TODO:
 *      * PHY emulation should be separated from nic emulation.
 *        Most nic emulations could share the same phy code.
 *      * i82550 is untested. It is programmed like the i82559.
 *      * i82562 is untested. It is programmed like the i82559.
 *      * Power management (i82558 and later) is not implemented.
 *
 * EE100   eepro100_write2         addr=General Status/Control+2 val=0x0080
 * EE100   eepro100_write2         feature is missing in this emulation: unknown word write
 * EE100   eepro100_read2          addr=General Status/Control+2 val=0x0080
 * EE100   eepro100_read2          feature is missing in this emulation: unknown word read
 */

#include <stddef.h>             /* offsetof */
#include "hw.h"
#include "pci.h"
#include "net.h"
#include "eeprom93xx.h"

/* Common declarations for all PCI devices. */

#define PCI_CONFIG_8(offset, value) \
    (pci_conf[offset] = (value))
#define PCI_CONFIG_16(offset, value) \
    (*(uint16_t *)&pci_conf[offset] = cpu_to_le16(value))
#define PCI_CONFIG_32(offset, value) \
    (*(uint32_t *)&pci_conf[offset] = cpu_to_le32(value))

#define KiB 1024

/* Debug EEPRO100 card. */
//~ #define DEBUG_EEPRO100

#ifdef DEBUG_EEPRO100
#define logout(fmt, ...) fprintf(stderr, "EE100\t%-24s" fmt, __func__, ## __VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

/* Set flags to 0 to disable debug output. */
#define INT     1       /* interrupt related actions */
#define MDI     1       /* mdi related actions */
#define OTHER   1
#define RXTX    1
#define EEPROM  1       /* eeprom related actions */

#define TRACE(flag, command) ((flag) ? (command) : (void)0)

#define UNEXPECTED() logout("%s:%u unexpected\n", __FILE__, __LINE__)

//~ #define missing(text)       assert(!"feature is missing in this emulation: " text)
#define missing(text)       logout("feature is missing in this emulation: " text "\n")

#define MAX_ETH_FRAME_SIZE 1514

/* This driver supports several different devices which are declared here. */
#define i82550          0x82550
#define i82551          0x82551
#define i82557A         0x82557a
#define i82557B         0x82557b
#define i82557C         0x82557c
#define i82558A         0x82558a
#define i82558B         0x82558b
#define i82559A         0x82559a
#define i82559B         0x82559b
#define i82559C         0x82559c
#define i82559ER        0x82559e
#define i82562          0x82562

/* Use 64 word EEPROM. TODO: could be a runtime option. */
#define EEPROM_SIZE     64

#define PCI_MEM_SIZE            (4 * KiB)
#define PCI_IO_SIZE             64
#define PCI_FLASH_SIZE          (128 * KiB)

#define BIT(n) (1 << (n))
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

/* The SCB accepts the following controls for the Tx and Rx units: */
#define  CU_NOP         0x0000  /* No operation. */
#define  CU_START       0x0010  /* CU start. */
#define  CU_RESUME      0x0020  /* CU resume. */
#define  CU_STATSADDR   0x0040  /* Load dump counters address. */
#define  CU_SHOWSTATS   0x0050  /* Dump statistical counters. */
#define  CU_CMD_BASE    0x0060  /* Load CU base address. */
#define  CU_DUMPSTATS   0x0070  /* Dump and reset statistical counters. */
#define  CU_SRESUME     0x00a0  /* CU static resume. */

#define  RU_NOP         0x0000
#define  RX_START       0x0001
#define  RX_RESUME      0x0002
#define  RX_ABORT       0x0004
#define  RX_ADDR_LOAD   0x0006
#define  RX_RESUMENR    0x0007
#define INT_MASK        0x0100
#define DRVR_INT        0x0200  /* Driver generated interrupt. */

typedef unsigned char bool;

/* Offsets to the various registers.
   All accesses need not be longword aligned. */
typedef enum {
    SCBStatus = 0,
    SCBAck = 1,
    SCBCmd = 2,                 /* Rx/Command Unit command and status. */
    SCBIntmask = 3,
    SCBPointer = 4,             /* General purpose pointer. */
    SCBPort = 8,                /* Misc. commands and operands.  */
    SCBflash = 12,              /* Flash memory control. */
    SCBeeprom = 14,             /* EEPROM control. */
    SCBCtrlMDI = 16,            /* MDI interface control. */
    SCBEarlyRx = 20,            /* Early receive byte count. */
    SCBFlow = 24,               /* Flow Control. */
    SCBpmdr = 27,               /* Power Management Driver. */
    SCBgctrl = 28,              /* General Control. */
    SCBgstat = 29,              /* General Status. */
} speedo_offset_t;

/* A speedo3 transmit buffer descriptor with two buffers... */
typedef struct {
    uint16_t status;
    uint16_t command;
    uint32_t link;              /* void * */
    uint32_t tbd_array_addr;    /* transmit buffer descriptor array address. */
    uint16_t tcb_bytes;         /* transmit command block byte count (in lower 14 bits */
    uint8_t tx_threshold;       /* transmit threshold */
    uint8_t tbd_count;          /* TBD number */
    //~ /* This constitutes two "TBD" entries: hdr and data */
    //~ uint32_t tx_buf_addr0;  /* void *, header of frame to be transmitted.  */
    //~ int32_t  tx_buf_size0;  /* Length of Tx hdr. */
    //~ uint32_t tx_buf_addr1;  /* void *, data to be transmitted.  */
    //~ int32_t  tx_buf_size1;  /* Length of Tx data. */
} eepro100_tx_t;

/* Receive frame descriptor. */
typedef struct {
    int16_t status;
    uint16_t command;
    uint32_t link;              /* struct RxFD * */
    uint32_t rx_buf_addr;       /* void * */
    uint16_t count;
    uint16_t size;
    char packet[MAX_ETH_FRAME_SIZE + 4];
} eepro100_rx_t;

typedef enum {
    COMMAND_EL = BIT(15),
    COMMAND_S = BIT(14),
    COMMAND_I = BIT(13),
    COMMAND_NC = BIT(4),
    COMMAND_SF = BIT(3),
    COMMAND_CMD = BITS(2, 0),
} cb_command_bit_t;

typedef enum {
    STATUS_C = BIT(15),
    STATUS_OK = BIT(13),
} cb_status_bit_t;

typedef struct {
    uint32_t tx_good_frames, tx_max_collisions, tx_late_collisions,
             tx_underruns, tx_lost_crs, tx_deferred, tx_single_collisions,
             tx_multiple_collisions, tx_total_collisions;
    uint32_t rx_good_frames, rx_crc_errors, rx_alignment_errors,
             rx_resource_errors, rx_overrun_errors, rx_cdt_errors,
             rx_short_frame_errors;
    uint32_t fc_xmt_pause, fc_rcv_pause, fc_rcv_unsupported;
    uint16_t xmt_tco_frames, rcv_tco_frames;
    uint32_t complete;
} eepro100_stats_t;

typedef enum {
    cu_idle = 0,
    cu_suspended = 1,
    cu_active = 2,
    cu_lpq_active = 2,
    cu_hqp_active = 3
} cu_state_t;

typedef enum {
    ru_idle = 0,
    ru_suspended = 1,
    ru_no_resources = 2,
    ru_ready = 4
} ru_state_t;

typedef struct {
    uint8_t cmd;
    uint32_t start;
    uint32_t stop;
    uint8_t mult[8];            /* multicast mask array */
    int mmio_index;
    PCIDevice *pci_dev;
    VLANClientState *vc;
    uint8_t scb_stat;           /* SCB stat/ack byte */
    uint8_t int_stat;           /* PCI interrupt status */
    uint32_t region[3];         /* PCI region addresses */
    uint8_t macaddr[6];
    uint16_t mdimem[32];
    eeprom_t *eeprom;
    uint32_t device;            /* device variant */
    uint32_t pointer;
    /* (cu_base + cu_offset) address the next command block in the command block list. */
    uint32_t cu_base;           /* CU base address */
    uint32_t cu_offset;         /* CU address offset */
    /* (ru_base + ru_offset) address the RFD in the Receive Frame Area. */
    uint32_t ru_base;           /* RU base address */
    uint32_t ru_offset;         /* RU address offset */
    uint32_t statsaddr;         /* pointer to eepro100_stats_t */

    cu_state_t cu_state;
    ru_state_t ru_state;

    /* Temporary data. */
    eepro100_tx_t tx;
    uint32_t cb_address;

    /* Statistical counters. */
    eepro100_stats_t statistics;

    /* Configuration bytes. */
    uint8_t configuration[22];

    /* Data in mem is always in the byte order of the controller (le). */
    uint8_t mem[PCI_MEM_SIZE];
} EEPRO100State;

/* Word indices in EEPROM. */
typedef enum {
    eeprom_cnfg_mdix  = 0x03,
    eeprom_id         = 0x05,
    eeprom_phy_id     = 0x06,
    eeprom_vendor_id  = 0x0c,
    eeprom_config_asf = 0x0d,
    eeprom_device_id  = 0x23,
    eeprom_smbus_addr = 0x90,
} eeprom_offset_t;

/* Bit values for EEPROM ID word (offset 0x0a). */
typedef enum {
    eeprom_id_mdm = BIT(0),     /* Modem */
    eeprom_id_stb = BIT(1),     /* Standby Enable */
    eeprom_id_wmr = BIT(2),     /* ??? */
    eeprom_id_wol = BIT(5),     /* Wake on LAN */
    eeprom_id_dpd = BIT(6),     /* Deep Power Down */
    eeprom_id_alt = BIT(7),     /* */
    /* BITS(10, 8) device revision */
    eeprom_id_bd = BIT(11),     /* boot disable */
    eeprom_id_id = BIT(13),     /* id bit */
    /* BITS(15, 14) signature */
    eeprom_id_valid = BIT(14),  /* signature for valid eeprom */
} eeprom_id_t;

/* Parameters for nic_save, nic_load. */
static int eepro100_instance = 0;
static const int eepro100_version = 20090807;

/* Default values for MDI (PHY) registers */
static const uint16_t eepro100_mdi_default[] = {
    /* MDI Registers 0 - 6, 7 */
    0x3000, 0x780d, 0x02a8, 0x0154, 0x05e1, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 8 - 15 */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 16 - 31 */
    0x0003, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* Readonly mask for MDI (PHY) registers */
static const uint16_t eepro100_mdi_mask[] = {
    0x0000, 0xffff, 0xffff, 0xffff, 0xc01f, 0xffff, 0xffff, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0fff, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* XXX: optimize */
static uint32_t lduw_le_phys(target_phys_addr_t addr)
{
    /* Load 16 bit (little endian) word from emulated hardware. */
    uint16_t val;
    cpu_physical_memory_read(addr, (uint8_t *)&val, sizeof(val));
    return le16_to_cpu(val);
}

/* XXX: optimize */
static uint32_t ldl_le_phys(target_phys_addr_t addr)
{
    /* Load 32 bit (little endian) word from emulated hardware. */
    uint32_t val;
    cpu_physical_memory_read(addr, (uint8_t *)&val, sizeof(val));
    return le32_to_cpu(val);
}

/* XXX: optimize */
static void stw_le_phys(target_phys_addr_t addr, uint16_t val)
{
    val = cpu_to_le16(val);
    cpu_physical_memory_write(addr, (const uint8_t *)&val, sizeof(val));
}

/* XXX: optimize */
static void stl_le_phys(target_phys_addr_t addr, uint32_t val)
{
    val = cpu_to_le32(val);
    cpu_physical_memory_write(addr, (const uint8_t *)&val, sizeof(val));
}

#define POLYNOMIAL 0x04c11db6

/* From FreeBSD */
/* XXX: optimize */
static unsigned compute_mcast_idx(const uint8_t * ep)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < 6; i++) {
        b = *ep++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry) {
                crc = ((crc ^ POLYNOMIAL) | carry);
            }
        }
    }
    return (crc & BITS(7, 2)) >> 2;
}

#if defined(DEBUG_EEPRO100)
static const char *nic_dump(const uint8_t * buf, unsigned size)
{
    static char dump[3 * 16 + 1];
    char *p = &dump[0];
    if (size > 16) {
        size = 16;
    }
    while (size-- > 0) {
        p += sprintf(p, " %02x", *buf++);
    }
    return dump;
}
#endif                          /* DEBUG_EEPRO100 */

#if 0 // TODO
enum scb_stat_ack {
    stat_ack_not_ours = 0x00,
    stat_ack_sw_gen = 0x04,
    stat_ack_rnr = 0x10,
    stat_ack_cu_idle = 0x20,
    stat_ack_frame_rx = 0x40,
    stat_ack_cu_cmd_done = 0x80,
    stat_ack_not_present = 0xFF,
    stat_ack_rx = (stat_ack_sw_gen | stat_ack_rnr | stat_ack_frame_rx),
    stat_ack_tx = (stat_ack_cu_idle | stat_ack_cu_cmd_done),
};
#endif

static void disable_interrupt(EEPRO100State * s)
{
    if (s->int_stat) {
        TRACE(INT, logout("interrupt disabled\n"));
        qemu_irq_lower(s->pci_dev->irq[0]);
        s->int_stat = 0;
    }
}

static void enable_interrupt(EEPRO100State * s)
{
    if (!s->int_stat) {
        TRACE(INT, logout("interrupt enabled\n"));
        qemu_irq_raise(s->pci_dev->irq[0]);
        s->int_stat = 1;
    }
}

static void eepro100_acknowledge(EEPRO100State * s)
{
    s->scb_stat &= ~s->mem[SCBAck];
    s->mem[SCBAck] = s->scb_stat;
    if (s->scb_stat == 0) {
        disable_interrupt(s);
    }
}

static void eepro100_interrupt(EEPRO100State * s, uint8_t status)
{
    uint8_t mask = ~s->mem[SCBIntmask];
    s->mem[SCBAck] |= status;
    status = s->scb_stat = s->mem[SCBAck];
    status &= (mask | 0x0f);
    //~ status &= (~s->mem[SCBIntmask] | 0x0xf);
    if (status && (mask & 0x01)) {
        /* SCB mask and SCB Bit M do not disable interrupt. */
        enable_interrupt(s);
    } else if (s->int_stat) {
        disable_interrupt(s);
    }
}

static void eepro100_cx_interrupt(EEPRO100State * s)
{
    /* CU completed action command. */
    /* Transmit not ok (82557 only, not in emulation). */
    eepro100_interrupt(s, 0x80);
}

static void eepro100_cna_interrupt(EEPRO100State * s)
{
    /* CU left the active state. */
    eepro100_interrupt(s, 0x20);
}

static void eepro100_fr_interrupt(EEPRO100State * s)
{
    /* RU received a complete frame. */
    eepro100_interrupt(s, 0x40);
}

#if 0
static void eepro100_rnr_interrupt(EEPRO100State * s)
{
    /* RU is not ready. */
    eepro100_interrupt(s, 0x10);
}
#endif

static void eepro100_mdi_interrupt(EEPRO100State * s)
{
    /* MDI completed read or write cycle. */
    eepro100_interrupt(s, 0x08);
}

static void eepro100_swi_interrupt(EEPRO100State * s)
{
    /* Software has requested an interrupt. */
    eepro100_interrupt(s, 0x04);
}

#if 0
static void eepro100_fcp_interrupt(EEPRO100State * s)
{
    /* Flow control pause interrupt (82558 and later). */
    eepro100_interrupt(s, 0x01);
}
#endif

static void pci_reset(EEPRO100State * s)
{
    uint32_t device = s->device;
    uint8_t *pci_conf = s->pci_dev->config;
    bool power_management = 1;

    TRACE(OTHER, logout("%p\n", s));

    /* PCI Vendor ID */
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    /* PCI Device ID depends on device and is set below. */
    /* PCI Command */
    PCI_CONFIG_16(PCI_COMMAND, 0x0000);         // TODO: maybe 0x17
    /* PCI Status */
    PCI_CONFIG_16(PCI_STATUS, 0x0280);
    /* PCI Revision ID depends on device. */
    /* PCI Class Code */
    PCI_CONFIG_8(0x09, 0x00);
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);
    /* PCI Cache Line Size */
    /* Only bit 3 and bit 4 are writable (not emulated). */
    /* PCI Latency Timer */
    PCI_CONFIG_8(0x0d, 0x20);   // latency timer = 32 clocks
    PCI_CONFIG_8(PCI_HEADER_TYPE, 0x00);
    /* BIST (built-in self test) */
    /* Expansion ROM Base Address (depends on boot disable!!!) */
    PCI_CONFIG_32(0x30, 0x00000000);
    /* Capability Pointer */
    PCI_CONFIG_8(0x34, 0xdc);
    /* Interrupt Line */
    /* Interrupt Pin */
    PCI_CONFIG_8(0x3d, 1);      // interrupt pin 0
    /* Minimum Grant */
    PCI_CONFIG_8(0x3e, 0x08);
    /* Maximum Latency */
    PCI_CONFIG_8(0x3f, 0x18);

    switch (device) {
    case i82550:
        // TODO: check device id.
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82551IT);
        /* Revision ID: 0x0c, 0x0d, 0x0e. */
        PCI_CONFIG_8(PCI_REVISION_ID, 0x0e);
        break;
    case i82551:
        // TODO: check device id.
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        /* Revision ID: 0x0f, 0x10. */
        PCI_CONFIG_8(PCI_REVISION_ID, 0x0f);
        break;
    case i82557A:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x01);
        PCI_CONFIG_8(0x34, 0x00);
        power_management = 0;
        break;
    case i82557B:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x02);
        PCI_CONFIG_8(0x34, 0x00);
        power_management = 0;
        break;
    case i82557C:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x03);
        PCI_CONFIG_8(0x34, 0x00);
        power_management = 0;
        break;
    case i82558A:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_16(PCI_STATUS, 0x0290);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x04);
        break;
    case i82558B:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_16(PCI_STATUS, 0x0290);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x05);
        break;
    case i82559A:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_16(PCI_STATUS, 0x0290);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x06);
        break;
    case i82559B:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_16(PCI_STATUS, 0x0290);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x07);
        break;
    case i82559C:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82557);
        PCI_CONFIG_16(PCI_STATUS, 0x0290);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x08);
        // TODO: Windows wants revision id 0x0c.
        PCI_CONFIG_8(PCI_REVISION_ID, 0x0c);
#if EEPROM_SIZE > 0
        PCI_CONFIG_16(PCI_SUBSYSTEM_VENDOR_ID, 0x8086);
        PCI_CONFIG_16(PCI_SUBSYSTEM_ID, 0x0040);
#endif
        break;
    case i82559ER:
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82551IT);
        PCI_CONFIG_16(PCI_STATUS, 0x0290);
        PCI_CONFIG_8(PCI_REVISION_ID, 0x09);
        break;
        //~ pci_config_set_device_id(pci_conf, 0x1030);       /* 82559 InBusiness 10/100 !!! */
    case i82562:
        // TODO: check device id.
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82551IT);
        /* TODO: wrong revision id. */
        PCI_CONFIG_8(PCI_REVISION_ID, 0x0e);
        break;
    default:
        logout("Device %X is undefined!\n", device);
    }

    if (power_management) {
        /* Power Management Capabilities */
        PCI_CONFIG_8(0xdc, 0x01);
        /* Next Item Pointer */
        /* Capability ID */
        PCI_CONFIG_16(0xde, 0x7e21);
        /* TODO: Power Management Control / Status. */
        /* TODO: Ethernet Power Consumption Registers (i82559 and later). */
    }

#if EEPROM_SIZE > 0
    if (device == i82557C || device == i82558B || device == i82559C) {
        // TODO: get vendor id from EEPROM for i82557C or later.
        // TODO: get device id from EEPROM for i82557C or later.
        // TODO: status bit 4 can be disabled by EEPROM for i82558, i82559.
        // TODO: header type is determined by EEPROM for i82559.
        // TODO: get subsystem id from EEPROM for i82557C or later.
        // TODO: get subsystem vendor id from EEPROM for i82557C or later.
        // TODO: exp. rom baddr depends on a bit in EEPROM for i82558 or later.
        // TODO: capability pointer depends on EEPROM for i82558.
        logout("Get device id and revision from EEPROM!!!\n");
    }
#endif
}

static void nic_selective_reset(EEPRO100State * s)
{
#if EEPROM_SIZE > 0
    static const uint16_t eeprom_i82559[] = {
        /* 0x0000 */ 0x0000, 0x0000, 0x0000, 0x020b,
        /* 0x0008 */ 0xffff, 0x0201, 0x4701, 0xffff,
        /* 0x0010 */ 0x7517, 0x6704, 0x50a2, 0x0040,
        /* 0x0018 */ 0x8086, 0x0064, 0xffff, 0xffff,
        /* 0x0020 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0028 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0030 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0038 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0040 */ 0xffff, 0xffff, 0xffff, 0x1229,
        /* 0x0048 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0050 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0058 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0060 */ 0x002c, 0x4000, 0x3003, 0xffff,
        /* 0x0068 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0070 */ 0xffff, 0xffff, 0xffff, 0xffff,
        /* 0x0078 */ 0xffff, 0xffff, 0xffff, 0xffff,
    };
    size_t i;
    uint8_t *pci_conf = s->pci_dev->config;
    uint16_t *eeprom_contents = eeprom93xx_data(s->eeprom);
    //~ eeprom93xx_reset(s->eeprom);
    memcpy(eeprom_contents, eeprom_i82559, EEPROM_SIZE * 2);
    memcpy(eeprom_contents, s->macaddr, 6);
#if defined(WORDS_BIGENDIAN)
    bswap16s(&eeprom_contents[0]);
    bswap16s(&eeprom_contents[1]);
    bswap16s(&eeprom_contents[2]);
#endif
#if 0
    /* Only needed to set a different vendor id. */
    memcpy(eeprom_contents + eeprom_vendor_id, pci_conf + PCI_VENDOR_ID, 2);
#if defined(WORDS_BIGENDIAN)
    bswap16s(&eeprom_contents[eeprom_vendor_id]);
#endif
#endif /* EEPROM_SIZE > 0 */
    memcpy(eeprom_contents + eeprom_device_id, pci_conf + PCI_DEVICE_ID, 2);
#if defined(WORDS_BIGENDIAN)
    bswap16s(&eeprom_contents[eeprom_device_id]);
#endif
#if 0
    /* We might change the phy id here. */
    eeprom_contents[eeprom_phy_id] =
        (eeprom_contents[eeprom_phy_id] & 0xff00) + 1;
#endif
    /* TODO: eeprom_id_alt for i82559 */
    eeprom_contents[eeprom_id] |= eeprom_id_valid;
    uint16_t sum = 0;
    for (i = 0; i < EEPROM_SIZE - 1; i++) {
        sum += eeprom_contents[i];
    }
    eeprom_contents[EEPROM_SIZE - 1] = (0xbaba - sum);
    TRACE(EEPROM, logout("checksum=0x%04x\n", eeprom_contents[EEPROM_SIZE - 1]));
#endif

    memset(s->mem, 0, sizeof(s->mem));
    uint32_t val = cpu_to_le32(BIT(21));
    memcpy(&s->mem[SCBCtrlMDI], &val, sizeof(val));

    assert(sizeof(s->mdimem) == sizeof(eepro100_mdi_default));
    memcpy(&s->mdimem[0], &eepro100_mdi_default[0], sizeof(s->mdimem));
}

static void nic_reset(void *opaque)
{
    static int first;
    EEPRO100State *s = (EEPRO100State *) opaque;
    TRACE(OTHER, logout("%p\n", s));
    if (!first) {
        first = 1;
    }
    /* TODO: Clearing of multicast table for selective reset, too? */
    memset(&s->mult[0], 0, sizeof(s->mult));
    nic_selective_reset(s);
}

#if defined(DEBUG_EEPRO100)
static const char *e100_reg[PCI_IO_SIZE / 4] = {
    "Command/Status",
    "General Pointer",
    "Port",
    "EEPROM/Flash Control",
    "MDI Control",
    "Receive DMA Byte Count",
    "Flow Control",
    "General Status/Control"
};

static char *regname(uint32_t addr)
{
    static char buf[16];
    if (addr < PCI_IO_SIZE) {
        const char *r = e100_reg[addr / 4];
        if (r != 0) {
            snprintf(buf, sizeof(buf), "%s+%u", r, addr % 4);
        } else {
            snprintf(buf, sizeof(buf), "0x%02x", addr);
        }
    } else {
        snprintf(buf, sizeof(buf), "??? 0x%08x", addr);
    }
    return buf;
}
#endif                          /* DEBUG_EEPRO100 */

#if 0
static uint16_t eepro100_read_status(EEPRO100State * s)
{
    uint16_t val = s->status;
    TRACE(OTHER, logout("val=0x%04x\n", val));
    return val;
}

static void eepro100_write_status(EEPRO100State * s, uint16_t val)
{
    TRACE(OTHER, logout("val=0x%04x\n", val));
    s->status = val;
}
#endif

/*****************************************************************************
 *
 * Command emulation.
 *
 ****************************************************************************/

#if 0
static uint16_t eepro100_read_command(EEPRO100State * s)
{
    uint16_t val = 0xffff;
    //~ TRACE(OTHER, logout("val=0x%04x\n", val));
    return val;
}
#endif

/* Commands that can be put in a command list entry. */
enum commands {
    CmdNOp = 0,
    CmdIASetup = 1,
    CmdConfigure = 2,
    CmdMulticastList = 3,
    CmdTx = 4,
    CmdTDR = 5,                 /* load microcode */
    CmdDump = 6,
    CmdDiagnose = 7,

    /* And some extra flags: */
    CmdTxFlex = 0x0008,         /* Use "Flexible mode" for CmdTx command. */
};

static cu_state_t get_cu_state(EEPRO100State * s)
{
    return s->cu_state;
    //~ return ((s->mem[SCBStatus] >> 6) & 0x03);
}

static void set_cu_state(EEPRO100State * s, cu_state_t state)
{
    s->cu_state = state;
    s->mem[SCBStatus] = (s->mem[SCBStatus] & 0x3f) + (state << 6);
}

static ru_state_t get_ru_state(EEPRO100State * s)
{
    return s->ru_state;
    //~ return ((s->mem[SCBStatus] >> 2) & 0x0f);
}

static void set_ru_state(EEPRO100State * s, ru_state_t state)
{
    s->ru_state = state;
    s->mem[SCBStatus] = (s->mem[SCBStatus] & 0xc3) + (state << 2);
}

static void dump_statistics(EEPRO100State * s)
{
    /* Dump statistical data. Most data is never changed by the emulation
     * and always 0, so we first just copy the whole block and then those
     * values which really matter.
     * Number of data should check configuration!!!
     */
    cpu_physical_memory_write(s->statsaddr, (uint8_t *) & s->statistics, 64);
    stl_le_phys(s->statsaddr + 0, s->statistics.tx_good_frames);
    stl_le_phys(s->statsaddr + 36, s->statistics.rx_good_frames);
    stl_le_phys(s->statsaddr + 48, s->statistics.rx_resource_errors);
    stl_le_phys(s->statsaddr + 60, s->statistics.rx_short_frame_errors);
    //~ stw_le_phys(s->statsaddr + 76, s->statistics.xmt_tco_frames);
    //~ stw_le_phys(s->statsaddr + 78, s->statistics.rcv_tco_frames);
    //~ missing("CU dump statistical counters");
}

static void read_cb(EEPRO100State *s)
{
    cpu_physical_memory_read(s->cb_address, (uint8_t *) &s->tx, sizeof(s->tx));
    s->tx.status = le16_to_cpu(s->tx.status);
    s->tx.command = le16_to_cpu(s->tx.command);
    s->tx.link = le32_to_cpu(s->tx.link);
    s->tx.tbd_array_addr = le32_to_cpu(s->tx.tbd_array_addr);
    s->tx.tcb_bytes = le16_to_cpu(s->tx.tcb_bytes);
}

static void tx_command(EEPRO100State *s)
{
    uint32_t tbd_array = s->tx.tbd_array_addr;
    uint16_t tcb_bytes = (s->tx.tcb_bytes & 0x3fff);
    uint8_t buf[2600];
    uint16_t size = 0;
    uint32_t tbd_address = s->cb_address + 0x10;
    TRACE(RXTX, logout
        ("transmit, TBD array address 0x%08x, TCB byte count 0x%04x, TBD count %u\n",
         tbd_array, tcb_bytes, s->tx.tbd_count));
    assert(!(s->tx.command & COMMAND_NC));
    assert(tcb_bytes <= sizeof(buf));
    if (!((tcb_bytes > 0) || (tbd_array != 0xffffffff))) {
        logout
            ("illegal values of TBD array address and TCB byte count!\n");
    }
    for (size = 0; size < tcb_bytes; ) {
        uint32_t tx_buffer_address = ldl_le_phys(tbd_address);
        uint16_t tx_buffer_size = lduw_le_phys(tbd_address + 4);
        //~ uint16_t tx_buffer_el = lduw_le_phys(tbd_address + 6);
        tbd_address += 8;
        TRACE(RXTX, logout
            ("TBD (simplified mode): buffer address 0x%08x, size 0x%04x\n",
             tx_buffer_address, tx_buffer_size));
        assert(size + tx_buffer_size <= sizeof(buf));
        cpu_physical_memory_read(tx_buffer_address, &buf[size],
                                 tx_buffer_size);
        size += tx_buffer_size;
    }
    if (!(s->tx.command & COMMAND_SF)) {
        /* Simplified mode. Was already handled by code above. */
        if (tbd_array != 0xffffffff) {
            UNEXPECTED();
        }
    } else if (s->device >= i82557A && s->device <= i82557C) {
        /* 82557 does not support extend TCB. */
        UNEXPECTED();
    } else {
        /* Flexible mode. */
        uint8_t tbd_count = 0;
        if (!(s->configuration[6] & BIT(4)) && s->device != i82557C) {
            /* Extended TCB (not for 82557). */
            assert(tcb_bytes == 0);
            for (; tbd_count < 2 && tbd_count < s->tx.tbd_count; tbd_count++) {
                uint32_t tx_buffer_address = ldl_le_phys(tbd_address);
                uint16_t tx_buffer_size = lduw_le_phys(tbd_address + 4);
                uint16_t tx_buffer_el = lduw_le_phys(tbd_address + 6);
                tbd_address += 8;
                TRACE(RXTX, logout
                    ("TBD (extended mode): buffer address 0x%08x, size 0x%04x\n",
                     tx_buffer_address, tx_buffer_size));
                if (size + tx_buffer_size > sizeof(buf)) {
                    logout("bad extended TCB with size 0x%04x\n", tx_buffer_size);
                } else if (tx_buffer_size > 0) {
                    assert(tx_buffer_address != 0);
                    cpu_physical_memory_read(tx_buffer_address, &buf[size],
                                             tx_buffer_size);
                    size += tx_buffer_size;
                }
                if (tx_buffer_el & 1) {
                    break;
                }
            }
        }
        tbd_address = tbd_array;
        for (; tbd_count < s->tx.tbd_count; tbd_count++) {
            uint32_t tx_buffer_address = ldl_le_phys(tbd_address);
            uint16_t tx_buffer_size = lduw_le_phys(tbd_address + 4);
            uint16_t tx_buffer_el = lduw_le_phys(tbd_address + 6);
            tbd_address += 8;
            TRACE(RXTX, logout
                ("TBD (flexible mode): buffer address 0x%08x, size 0x%04x\n",
                 tx_buffer_address, tx_buffer_size));
            if (size + tx_buffer_size > sizeof(buf)) {
                logout("bad flexible TCB with size 0x%04x\n", tx_buffer_size);
            } else {
                cpu_physical_memory_read(tx_buffer_address, &buf[size],
                                         tx_buffer_size);
                size += tx_buffer_size;
            }
            if (tx_buffer_el & 1) {
                break;
            }
        }
    }
    TRACE(RXTX, logout("%p sending frame, len=%d,%s\n", s, size, nic_dump(buf, size)));
    assert(size <= sizeof(buf));
    qemu_send_packet(s->vc, buf, size);
    s->statistics.tx_good_frames++;
    /* Transmit with bad status would raise an CX/TNO interrupt.
     * (82557 only). Emulation never has bad status. */
    //~ eepro100_cx_interrupt(s);
}

static void set_multicast_list(EEPRO100State *s)
{
    uint16_t multicast_count = s->tx.tbd_array_addr & BITS(13, 0);
    uint16_t i;
    memset(&s->mult[0], 0, sizeof(s->mult));
    TRACE(OTHER, logout("multicast list, multicast count = %u\n", multicast_count));
    for (i = 0; i < multicast_count; i += 6) {
        uint8_t multicast_addr[6];
        cpu_physical_memory_read(s->cb_address + 10 + i, multicast_addr, 6);
        TRACE(OTHER, logout("multicast entry %s\n", nic_dump(multicast_addr, 6)));
        unsigned mcast_idx = compute_mcast_idx(multicast_addr);
        assert(mcast_idx < 64);
        s->mult[mcast_idx >> 3] |= (1 << (mcast_idx & 7));
    }
}

static void action_command(EEPRO100State *s)
{
    for (;;) {
        s->cb_address = s->cu_base + s->cu_offset;
        read_cb(s);
        bool bit_el = ((s->tx.command & COMMAND_EL) != 0);
        bool bit_s = ((s->tx.command & COMMAND_S) != 0);
        s->cu_offset = s->tx.link;
        TRACE(OTHER, logout
            ("val=(cu start), status=0x%04x, command=0x%04x, link=0x%08x\n",
             s->tx.status, s->tx.command, s->cu_offset));
        switch (s->tx.command & COMMAND_CMD) {
        case CmdNOp:
            /* Do nothing. */
            break;
        case CmdIASetup:
            cpu_physical_memory_read(s->cb_address + 8, &s->macaddr[0], 6);
            TRACE(OTHER, logout("macaddr: %s\n", nic_dump(&s->macaddr[0], 6)));
            // TODO: missing code.
            break;
        case CmdConfigure:
            cpu_physical_memory_read(s->cb_address + 8, &s->configuration[0],
                                     sizeof(s->configuration));
            TRACE(OTHER, logout("configuration: %s\n", nic_dump(&s->configuration[0], 16)));
            break;
        case CmdMulticastList:
            set_multicast_list(s);
            break;
        case CmdTx:
            tx_command(s);
            break;
        case CmdTDR:
            TRACE(OTHER, logout("load microcode\n"));
            /* Starting with offset 8, the command contains
             * 64 dwords microcode which we just ignore here. */
            break;
        default:
            missing("undefined command");
        }
        /* Write new status (success). */
        stw_le_phys(s->cb_address, s->tx.status | STATUS_C | STATUS_OK);
        if (s->tx.command & COMMAND_I) {
            /* CU completed action. */
            eepro100_cx_interrupt(s);
        }
        if (bit_el) {
            /* CU becomes idle. Terminate command loop. */
            set_cu_state(s, cu_idle);
            eepro100_cna_interrupt(s);
            break;
        } else if (bit_s) {
            /* CU becomes suspended. Terminate command loop. */
            set_cu_state(s, cu_suspended);
            eepro100_cna_interrupt(s);
            break;
        } else {
            /* More entries in list. */
            TRACE(OTHER, logout("CU list with at least one more entry\n"));
        }
    }
    TRACE(OTHER, logout("CU list empty\n"));
    /* List is empty. Now CU is idle or suspended. */
}

static void eepro100_cu_command(EEPRO100State * s, uint8_t val)
{
    cu_state_t cu_state;
    switch (val) {
    case CU_NOP:
        /* No operation. */
        break;
    case CU_START:
        cu_state = get_cu_state(s);
        if (cu_state != cu_idle && cu_state != cu_suspended) {
            /* Intel documentation says that CU must be idle or suspended
             * for the CU start command. */
            logout("unexpected CU state is %u\n", cu_state);
        }
        set_cu_state(s, cu_active);
        s->cu_offset = s->pointer;
        action_command(s);
        break;
    case CU_RESUME:
        if (get_cu_state(s) != cu_suspended) {
            logout("bad CU resume from CU state %u\n", get_cu_state(s));
            /* Workaround for bad Linux eepro100 driver which resumes
             * from idle state. */
            //~ missing("cu resume");
            set_cu_state(s, cu_suspended);
        }
        if (get_cu_state(s) == cu_suspended) {
            TRACE(OTHER, logout("CU resuming\n"));
            set_cu_state(s, cu_active);
            action_command(s);
        }
        break;
    case CU_STATSADDR:
        /* Load dump counters address. */
        s->statsaddr = s->pointer;
        TRACE(OTHER, logout("val=0x%02x (status address)\n", val));
        break;
    case CU_SHOWSTATS:
        /* Dump statistical counters. */
        TRACE(OTHER, logout("val=0x%02x (dump stats)\n", val));
        dump_statistics(s);
        break;
    case CU_CMD_BASE:
        /* Load CU base. */
        TRACE(OTHER, logout("val=0x%02x (CU base address)\n", val));
        s->cu_base = s->pointer;
        break;
    case CU_DUMPSTATS:
        /* Dump and reset statistical counters. */
        TRACE(OTHER, logout("val=0x%02x (dump stats and reset)\n", val));
        dump_statistics(s);
        memset(&s->statistics, 0, sizeof(s->statistics));
        break;
    case CU_SRESUME:
        /* CU static resume. */
        missing("CU static resume");
        break;
    default:
        missing("Undefined CU command");
    }
}

static void eepro100_ru_command(EEPRO100State * s, uint8_t val)
{
    switch (val) {
    case RU_NOP:
        /* No operation. */
        break;
    case RX_START:
        /* RU start. */
        if (get_ru_state(s) != ru_idle) {
            logout("RU state is %u, should be %u\n", get_ru_state(s), ru_idle);
            //~ assert(!"wrong RU state");
        }
        set_ru_state(s, ru_ready);
        s->ru_offset = s->pointer;
        TRACE(OTHER, logout("val=0x%02x (rx start)\n", val));
        break;
    case RX_RESUME:
        /* Restart RU. */
        if (get_ru_state(s) != ru_suspended) {
            logout("RU state is %u, should be %u\n", get_ru_state(s),
                   ru_suspended);
            //~ assert(!"wrong RU state");
        }
        set_ru_state(s, ru_ready);
        break;
    case RX_ADDR_LOAD:
        /* Load RU base. */
        TRACE(OTHER, logout("val=0x%02x (RU base address)\n", val));
        s->ru_base = s->pointer;
        break;
    default:
        logout("val=0x%02x (undefined RU command)\n", val);
        missing("Undefined SU command");
    }
}

static void eepro100_write_command(EEPRO100State * s, uint8_t val)
{
    eepro100_ru_command(s, val & 0x0f);
    eepro100_cu_command(s, val & 0xf0);
    if ((val) == 0) {
        TRACE(OTHER, logout("val=0x%02x\n", val));
    }
    /* Clear command byte after command was accepted. */
    s->mem[SCBCmd] = 0;
}

/*****************************************************************************
 *
 * EEPROM emulation.
 *
 ****************************************************************************/

#define EEPROM_CS       0x02
#define EEPROM_SK       0x01
#define EEPROM_DI       0x04
#define EEPROM_DO       0x08

static uint16_t eepro100_read_eeprom(EEPRO100State * s)
{
    uint16_t val;
    memcpy(&val, &s->mem[SCBeeprom], sizeof(val));
    val = le16_to_cpu(val);
    if (eeprom93xx_read(s->eeprom)) {
        val |= EEPROM_DO;
    } else {
        val &= ~EEPROM_DO;
    }
    val = cpu_to_le16(val);
    TRACE(EEPROM, logout("val=0x%04x\n", val));
    return val;
}

static void eepro100_write_eeprom(eeprom_t * eeprom, uint8_t val)
{
    TRACE(EEPROM, logout("val=0x%02x\n", val));

    /* mask unwriteable bits */
    //~ val = SET_MASKED(val, 0x31, eeprom->value);

    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);
    eeprom93xx_write(eeprom, eecs, eesk, eedi);
}

static void eepro100_write_pointer(EEPRO100State * s, uint32_t val)
{
    s->pointer = val;
    TRACE(OTHER, logout("val=0x%08x\n", val));
}

/*****************************************************************************
 *
 * MDI emulation.
 *
 ****************************************************************************/

#if defined(DEBUG_EEPRO100)
static const char *mdi_op_name[] = {
    "opcode 0",
    "write",
    "read",
    "opcode 3"
};

static const char *mdi_reg_name[] = {
    "Control",
    "Status",
    "PHY Identification (Word 1)",
    "PHY Identification (Word 2)",
    "Auto-Negotiation Advertisement",
    "Auto-Negotiation Link Partner Ability",
    "Auto-Negotiation Expansion"
};

static const char *reg2name(uint8_t reg)
{
    static char buffer[10];
    const char *p = buffer;
    if (reg < ARRAY_SIZE(mdi_reg_name)) {
        p = mdi_reg_name[reg];
    } else {
        snprintf(buffer, sizeof(buffer), "reg=0x%02x", reg);
    }
    return p;
}

#endif                          /* DEBUG_EEPRO100 */

static uint32_t eepro100_read_mdi(EEPRO100State * s)
{
    uint32_t val;
    memcpy(&val, &s->mem[0x10], sizeof(val));
    val = le32_to_cpu(val);

#ifdef DEBUG_EEPRO100
    uint8_t raiseint = (val & BIT(29)) >> 29;
    uint8_t opcode = (val & BITS(27, 26)) >> 26;
    uint8_t phy = (val & BITS(25, 21)) >> 21;
    uint8_t reg = (val & BITS(20, 16)) >> 16;
    uint16_t data = (val & BITS(15, 0));
#endif
    /* Emulation takes no time to finish MDI transaction. */
    val |= BIT(28);
    TRACE(MDI, logout("val=0x%08x (int=%u, %s, phy=%u, %s, data=0x%04x\n",
                      val, raiseint, mdi_op_name[opcode], phy,
                      reg2name(reg), data));
    return val;
}

static void eepro100_write_mdi(EEPRO100State * s, uint32_t val)
{
    uint8_t raiseint = (val & BIT(29)) >> 29;
    uint8_t opcode = (val & BITS(27, 26)) >> 26;
    uint8_t phy = (val & BITS(25, 21)) >> 21;
    uint8_t reg = (val & BITS(20, 16)) >> 16;
    uint16_t data = (val & BITS(15, 0));
    TRACE(MDI, logout("val=0x%08x (int=%u, %s, phy=%u, %s, data=0x%04x\n",
          val, raiseint, mdi_op_name[opcode], phy, reg2name(reg), data));
    if (phy != 1) {
        /* Unsupported PHY address. */
        //~ logout("phy must be 1 but is %u\n", phy);
        data = 0;
    } else if (opcode != 1 && opcode != 2) {
        /* Unsupported opcode. */
        logout("opcode must be 1 or 2 but is %u\n", opcode);
        data = 0;
    } else if (reg > 6) {
        /* Unsupported register. */
        logout("register must be 0...6 but is %u\n", reg);
        data = 0;
    } else {
        TRACE(MDI, logout("val=0x%08x (int=%u, %s, phy=%u, %s, data=0x%04x\n",
                          val, raiseint, mdi_op_name[opcode], phy,
                          reg2name(reg), data));
        if (opcode == 1) {
            /* MDI write */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                    data = s->mdimem[reg];
                } else {
                    /* Restart Auto Configuration = Normal Operation */
                    data &= ~0x0200;
                }
                break;
            case 1:            /* Status Register */
                missing("not writable");
                data = s->mdimem[reg];
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
                missing("not implemented");
                break;
            case 4:            /* Auto-Negotiation Advertisement Register */
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
            default:
                missing("not implemented");
            }
            s->mdimem[reg] = data;
        } else if (opcode == 2) {
            /* MDI read */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                }
                break;
            case 1:            /* Status Register */
                s->mdimem[reg] |= 0x0020;
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
            case 4:            /* Auto-Negotiation Advertisement Register */
                break;
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                s->mdimem[reg] = 0x41fe;
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
                s->mdimem[reg] = 0x0001;
                break;
            }
            data = s->mdimem[reg];
        }
        /* Emulation takes no time to finish MDI transaction.
         * Set MDI bit in SCB status register. */
        s->mem[SCBAck] |= 0x08;
        val |= BIT(28);
        if (raiseint) {
            eepro100_mdi_interrupt(s);
        }
    }
    val = (val & 0xffff0000) + data;
    val = cpu_to_le32(val);
    memcpy(&s->mem[0x10], &val, sizeof(val));
}

/*****************************************************************************
 *
 * Port emulation.
 *
 ****************************************************************************/

#define PORT_SOFTWARE_RESET     0
#define PORT_SELFTEST           1
#define PORT_SELECTIVE_RESET    2
#define PORT_DUMP               3
#define PORT_SELECTION_MASK     3

typedef struct {
    uint32_t st_sign;           /* Self Test Signature */
    uint32_t st_result;         /* Self Test Results */
} eepro100_selftest_t;

static uint32_t eepro100_read_port(EEPRO100State * s)
{
    return 0;
}

static void eepro100_write_port(EEPRO100State * s, uint32_t val)
{
    uint32_t address = (val & ~PORT_SELECTION_MASK);
    uint8_t selection = (val & PORT_SELECTION_MASK);
    switch (selection) {
    case PORT_SOFTWARE_RESET:
        nic_reset(s);
        break;
    case PORT_SELFTEST:
        TRACE(OTHER, logout("selftest address=0x%08x\n", address));
        eepro100_selftest_t data;
        cpu_physical_memory_read(address, (uint8_t *) & data, sizeof(data));
        data.st_sign = cpu_to_le32(0xffffffff);
        data.st_result = cpu_to_le32(0);
        cpu_physical_memory_write(address, (uint8_t *) & data, sizeof(data));
        break;
    case PORT_SELECTIVE_RESET:
        TRACE(OTHER, logout("selective reset, selftest address=0x%08x\n", address));
        nic_selective_reset(s);
        break;
    default:
        logout("val=0x%08x\n", val);
        missing("unknown port selection");
    }
}

/*****************************************************************************
 *
 * General hardware emulation.
 *
 ****************************************************************************/

static uint8_t eepro100_read1(EEPRO100State * s, uint32_t addr)
{
    uint8_t val;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&val, &s->mem[addr], sizeof(val));
    }

    switch (addr) {
    case SCBStatus:
        //~ val = eepro100_read_status(s);
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBAck:
        //~ val = eepro100_read_status(s);
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBCmd:
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        //~ val = eepro100_read_command(s);
        break;
    case SCBIntmask:
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBPort + 3:
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBeeprom:
        val = le16_to_cpu(eepro100_read_eeprom(s));
        break;
    case SCBpmdr:       /* Power Management Driver Register */
        val = 0;
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBgctrl:      /* General Control Register */
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBgstat:      /* General Status Register */
        /* 100 Mbps full duplex, valid link */
        val = 0x07;
        TRACE(OTHER, logout("addr=General Status val=%02x\n", val));
        break;
    default:
        logout("addr=%s val=0x%02x\n", regname(addr), val);
        missing("unknown byte read");
    }
    return val;
}

static uint16_t eepro100_read2(EEPRO100State * s, uint32_t addr)
{
    uint16_t val;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&val, &s->mem[addr], sizeof(val));
    }
    val = le16_to_cpu(val);

    switch (addr) {
    case SCBStatus:
        //~ val = eepro100_read_status(s);
        TRACE(OTHER, logout("addr=%s val=0x%04x\n", regname(addr), val));
        break;
    case SCBeeprom:
        val = eepro100_read_eeprom(s);
        TRACE(OTHER, logout("addr=%s val=0x%04x\n", regname(addr), val));
        break;
    default:
        logout("addr=%s val=0x%04x\n", regname(addr), val);
        missing("unknown word read");
    }
    val = cpu_to_le16(val);
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap16s(&val);
#endif
    return val;
}

static uint32_t eepro100_read4(EEPRO100State * s, uint32_t addr)
{
    uint32_t val = 0;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&val, &s->mem[addr], sizeof(val));
    }
    val = le32_to_cpu(val);
    switch (addr) {
    case SCBStatus:
        //~ val = eepro100_read_status(s);
        TRACE(OTHER, logout("addr=%s val=0x%08x\n", regname(addr), val));
        break;
    case SCBPointer:
        //~ val = eepro100_read_pointer(s);
        TRACE(OTHER, logout("addr=%s val=0x%08x\n", regname(addr), val));
        break;
    case SCBPort:
        val = eepro100_read_port(s);
        TRACE(OTHER, logout("addr=%s val=0x%08x\n", regname(addr), val));
        break;
    case SCBflash:
        val = eepro100_read_eeprom(s);
        TRACE(OTHER, logout("addr=%s val=0x%08x\n", regname(addr), val));
        break;
    case SCBCtrlMDI:
        val = le32_to_cpu(eepro100_read_mdi(s));
        break;
    default:
        logout("addr=%s val=0x%08x\n", regname(addr), val);
        missing("unknown longword read");
    }
    val = cpu_to_le32(val);
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap32s(&val);
#endif
    return val;
}

static void eepro100_write1(EEPRO100State * s, uint32_t addr, uint8_t val)
{
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&s->mem[addr], &val, sizeof(val));
    }

    TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));

    switch (addr) {
    case SCBStatus:
        //~ eepro100_write_status(s, val);
        break;
    case SCBAck:
        eepro100_acknowledge(s);
        break;
    case SCBCmd:
        eepro100_write_command(s, val);
        break;
    case SCBIntmask:
        if (val & BIT(1)) {
            eepro100_swi_interrupt(s);
        }
        eepro100_interrupt(s, 0);
        break;
    case SCBPort + 3:
    case SCBFlow:       /* does not exist on 82557 */
    case SCBFlow + 1:
    case SCBFlow + 2:
    case SCBpmdr:       /* does not exist on 82557 */
        TRACE(OTHER, logout("addr=%s val=0x%02x\n", regname(addr), val));
        break;
    case SCBeeprom:
        eepro100_write_eeprom(s->eeprom, val);
        break;
    default:
        logout("addr=%s val=0x%02x\n", regname(addr), val);
        missing("unknown byte write");
    }
}

static void eepro100_write2(EEPRO100State * s, uint32_t addr, uint16_t val)
{
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap16s(&val);
#endif
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&s->mem[addr], &val, sizeof(val));
    }

    TRACE(OTHER, logout("addr=%s val=0x%04x\n", regname(addr), val));

    switch (addr) {
    case SCBStatus:
        //~ eepro100_write_status(s, val);
        eepro100_acknowledge(s);
        break;
    case SCBCmd:
        eepro100_write_command(s, val);
        eepro100_write1(s, SCBIntmask, val >> 8);
        break;
    case SCBeeprom:
        eepro100_write_eeprom(s->eeprom, val);
        break;
    default:
        logout("addr=%s val=0x%04x\n", regname(addr), val);
        missing("unknown word write");
    }
}

static void eepro100_write4(EEPRO100State * s, uint32_t addr, uint32_t val)
{
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap32s(&val);
#endif
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&s->mem[addr], &val, sizeof(val));
    }

    switch (addr) {
    case SCBPointer:
        eepro100_write_pointer(s, val);
        break;
    case SCBPort:
        TRACE(OTHER, logout("addr=%s val=0x%08x\n", regname(addr), val));
        eepro100_write_port(s, val);
        break;
    case SCBflash:
        TRACE(OTHER, logout("addr=%s val=0x%08x\n", regname(addr), val));
        val = val >> 16;
        eepro100_write_eeprom(s->eeprom, val);
        break;
    case SCBCtrlMDI:
        eepro100_write_mdi(s, val);
        break;
    default:
        logout("addr=%s val=0x%08x\n", regname(addr), val);
        missing("unknown longword write");
    }
}

/*****************************************************************************
 *
 * Port mapped I/O.
 *
 ****************************************************************************/

static uint32_t ioport_read1(void *opaque, uint32_t addr)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read1(s, addr - s->region[1]);
}

static uint32_t ioport_read2(void *opaque, uint32_t addr)
{
    EEPRO100State *s = opaque;
    return eepro100_read2(s, addr - s->region[1]);
}

static uint32_t ioport_read4(void *opaque, uint32_t addr)
{
    EEPRO100State *s = opaque;
    return eepro100_read4(s, addr - s->region[1]);
}

static void ioport_write1(void *opaque, uint32_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write1(s, addr - s->region[1], val);
}

static void ioport_write2(void *opaque, uint32_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    eepro100_write2(s, addr - s->region[1], val);
}

static void ioport_write4(void *opaque, uint32_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    eepro100_write4(s, addr - s->region[1], val);
}

/***********************************************************/
/* PCI EEPRO100 definitions */

typedef struct {
    PCIDevice dev;
    EEPRO100State eepro100;
} PCIEEPRO100State;

static void pci_map(PCIDevice * pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *) pci_dev;
    EEPRO100State *s = &d->eepro100;

    TRACE(OTHER, logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
          region_num, addr, size, type));

    assert(region_num == 1);
    register_ioport_write(addr, size, 1, ioport_write1, s);
    register_ioport_read(addr, size, 1, ioport_read1, s);
    register_ioport_write(addr, size, 2, ioport_write2, s);
    register_ioport_read(addr, size, 2, ioport_read2, s);
    register_ioport_write(addr, size, 4, ioport_write4, s);
    register_ioport_read(addr, size, 4, ioport_read4, s);

    s->region[region_num] = addr;
}

/*****************************************************************************
 *
 * Memory mapped I/O.
 *
 ****************************************************************************/

static void pci_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write1(s, addr, val);
}

static void pci_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write2(s, addr, val);
}

static void pci_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write4(s, addr, val);
}

static uint32_t pci_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read1(s, addr);
}

static uint32_t pci_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read2(s, addr);
}

static uint32_t pci_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read4(s, addr);
}

static CPUWriteMemoryFunc * const pci_mmio_write[] = {
    pci_mmio_writeb,
    pci_mmio_writew,
    pci_mmio_writel
};

static CPUReadMemoryFunc * const pci_mmio_read[] = {
    pci_mmio_readb,
    pci_mmio_readw,
    pci_mmio_readl
};

static void pci_mmio_map(PCIDevice * pci_dev, int region_num,
                         uint32_t addr, uint32_t size, int type)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *) pci_dev;

    TRACE(OTHER, logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
          region_num, addr, size, type));

    if (region_num == 0) {
        /* Map control / status registers. */
        cpu_register_physical_memory(addr, size, d->eepro100.mmio_index);
        d->eepro100.region[region_num] = addr;
    }
}

static int nic_can_receive(VLANClientState *vc)
{
    EEPRO100State *s = vc->opaque;
    TRACE(RXTX, logout("%p\n", s));
    return get_ru_state(s) == ru_ready;
    //~ return !eepro100_buffer_full(s);
}

static ssize_t nic_receive(VLANClientState *vc, const uint8_t * buf, size_t size)
{
    /* TODO:
     * - Magic packets should set bit 30 in power management driver register.
     * - Interesting packets should set bit 29 in power management driver register.
     */
    EEPRO100State *s = vc->opaque;
    uint16_t rfd_status = 0xa000;
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    /* TODO: check multiple IA bit. */
    assert(!(s->configuration[20] & BIT(6)));

    if (s->configuration[8] & 0x80) {
        /* CSMA is disabled. */
        logout("%p received while CSMA is disabled\n", s);
        return -1;
    } else if (size < 64 && (s->configuration[7] & 1)) {
        /* Short frame and configuration byte 7/0 (discard short receive) set:
         * Short frame is discarded */
        logout("%p received short frame (%zu byte)\n", s, size);
        s->statistics.rx_short_frame_errors++;
        //~ return -1;
    } else if ((size > MAX_ETH_FRAME_SIZE + 4) && !(s->configuration[18] & 8)) {
        /* Long frame and configuration byte 18/3 (long receive ok) not set:
         * Long frames are discarded. */
        logout("%p received long frame (%zu byte), ignored\n", s, size);
        return -1;
    } else if (memcmp(buf, s->macaddr, 6) == 0) {       // !!!
        /* Frame matches individual address. */
        /* TODO: check configuration byte 15/4 (ignore U/L). */
        TRACE(RXTX, logout("%p received frame for me, len=%zu\n", s, size));
    } else if (memcmp(buf, broadcast_macaddr, 6) == 0) {
        /* Broadcast frame. */
        TRACE(RXTX, logout("%p received broadcast, len=%zu\n", s, size));
        rfd_status |= 0x0002;
    } else if (buf[0] & 0x01) {
        /* Multicast frame. */
        TRACE(RXTX, logout("%p received multicast, len=%zu,%s\n", s, size, nic_dump(buf, size)));
        if (s->configuration[21] & BIT(3)) {
          /* Multicast all bit is set, receive all multicast frames. */
        } else {
          unsigned mcast_idx = compute_mcast_idx(buf);
          assert(mcast_idx < 64);
          if (s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))) {
            /* Multicast frame is allowed in hash table. */
          } else if (s->configuration[15] & 1) {
              /* Promiscuous: receive all. */
              rfd_status |= 0x0004;
          } else {
              TRACE(RXTX, logout("%p multicast ignored\n", s));
              return -1;
          }
        }
        /* TODO: Next not for promiscuous mode? */
        rfd_status |= 0x0002;
    } else if (s->configuration[15] & 1) {
        /* Promiscuous: receive all. */
        TRACE(RXTX, logout("%p received frame in promiscuous mode, len=%zu\n", s, size));
        rfd_status |= 0x0004;
    } else {
        TRACE(RXTX, logout("%p received frame, ignored, len=%zu,%s\n", s, size,
               nic_dump(buf, size)));
        return -1;
    }

    if (get_ru_state(s) != ru_ready) {
        /* No resources available. */
        logout("no resources, state=%u\n", get_ru_state(s));
        s->statistics.rx_resource_errors++;
        //~ assert(!"no resources");
        return -1;
    }
    //~ !!!
    eepro100_rx_t rx;
    cpu_physical_memory_read(s->ru_base + s->ru_offset, (uint8_t *) & rx,
                             offsetof(eepro100_rx_t, packet));
    // !!!
    uint16_t rfd_command = le16_to_cpu(rx.command);
    uint16_t rfd_size = le16_to_cpu(rx.size);
    if (size > rfd_size) {
      logout("received frame with %zu > %u\n", size, rfd_size);
      UNEXPECTED();
    }
    if (size < 64) {
        rfd_status |= 0x0080;
    }
    TRACE(OTHER, logout("command 0x%04x, link 0x%08x, addr 0x%08x, size %u\n",
          rfd_command, rx.link, rx.rx_buf_addr, rfd_size));
    stw_le_phys(s->ru_base + s->ru_offset + offsetof(eepro100_rx_t, status),
                rfd_status);
    stw_le_phys(s->ru_base + s->ru_offset + offsetof(eepro100_rx_t, count), size);
    /* Early receive interrupt not supported. */
    //~ eepro100_er_interrupt(s);
    /* Receive CRC Transfer not supported. */
    assert(!(s->configuration[18] & 4));
    /* TODO: check stripping enable bit. */
    //~ assert(!(s->configuration[17] & 1));
    cpu_physical_memory_write(s->ru_base + s->ru_offset +
                              offsetof(eepro100_rx_t, packet), buf, size);
    s->statistics.rx_good_frames++;
    eepro100_fr_interrupt(s);
    s->ru_offset = le32_to_cpu(rx.link);
    if (rfd_command & 0x8000) {
        /* EL bit is set, so this was the last frame. */
        set_ru_state(s, ru_idle);
    }
    if (rfd_command & 0x4000) {
        /* S bit is set. */
        set_ru_state(s, ru_suspended);
    }
    return size;
}

static int nic_load(QEMUFile * f, void *opaque, int version_id)
{
    EEPRO100State *s = (EEPRO100State *) opaque;
    int i;
    int ret;

    if (version_id != eepro100_version) {
        return -EINVAL;
    }

    if (s->pci_dev) {
        ret = pci_device_load(s->pci_dev, f);
        if (ret < 0) {
            return ret;
        }
    }

    qemu_get_8s(f, &s->cmd);
    qemu_get_be32s(f, &s->start);
    qemu_get_be32s(f, &s->stop);
    qemu_get_buffer(f, s->mult, 8);
    qemu_get_buffer(f, s->mem, sizeof(s->mem));

    /* Restore all members of struct between scv_stat and mem. */
    qemu_get_8s(f, &s->scb_stat);
    qemu_get_8s(f, &s->int_stat);
    for (i = 0; i < 3; i++) {
        qemu_get_be32s(f, &s->region[i]);
    }
    qemu_get_buffer(f, s->macaddr, 6);
    for (i = 0; i < 32; i++) {
        qemu_get_be16s(f, &s->mdimem[i]);
    }
    /* The eeprom should be saved and restored by its own routines. */
    qemu_get_be32s(f, &s->device);
    qemu_get_be32s(f, &s->pointer);
    qemu_get_be32s(f, &s->cu_base);
    qemu_get_be32s(f, &s->cu_offset);
    qemu_get_be32s(f, &s->ru_base);
    qemu_get_be32s(f, &s->ru_offset);
    qemu_get_be32s(f, &s->statsaddr);
    /* Restore epro100_stats_t statistics. */
    qemu_get_be32s(f, &s->statistics.tx_good_frames);
    qemu_get_be32s(f, &s->statistics.tx_max_collisions);
    qemu_get_be32s(f, &s->statistics.tx_late_collisions);
    qemu_get_be32s(f, &s->statistics.tx_underruns);
    qemu_get_be32s(f, &s->statistics.tx_lost_crs);
    qemu_get_be32s(f, &s->statistics.tx_deferred);
    qemu_get_be32s(f, &s->statistics.tx_single_collisions);
    qemu_get_be32s(f, &s->statistics.tx_multiple_collisions);
    qemu_get_be32s(f, &s->statistics.tx_total_collisions);
    qemu_get_be32s(f, &s->statistics.rx_good_frames);
    qemu_get_be32s(f, &s->statistics.rx_crc_errors);
    qemu_get_be32s(f, &s->statistics.rx_alignment_errors);
    qemu_get_be32s(f, &s->statistics.rx_resource_errors);
    qemu_get_be32s(f, &s->statistics.rx_overrun_errors);
    qemu_get_be32s(f, &s->statistics.rx_cdt_errors);
    qemu_get_be32s(f, &s->statistics.rx_short_frame_errors);
    qemu_get_be32s(f, &s->statistics.fc_xmt_pause);
    qemu_get_be32s(f, &s->statistics.fc_rcv_pause);
    qemu_get_be32s(f, &s->statistics.fc_rcv_unsupported);
    qemu_get_be16s(f, &s->statistics.xmt_tco_frames);
    qemu_get_be16s(f, &s->statistics.rcv_tco_frames);
    qemu_get_be32s(f, &s->statistics.complete);
#if 0
    qemu_get_be16s(f, &s->status);
#endif

    /* Configuration bytes. */
    qemu_get_buffer(f, s->configuration, sizeof(s->configuration));

    return 0;
}

static void nic_save(QEMUFile * f, void *opaque)
{
    EEPRO100State *s = (EEPRO100State *) opaque;
    int i;

    if (s->pci_dev) {
        pci_device_save(s->pci_dev, f);
    }

    qemu_put_8s(f, &s->cmd);
    qemu_put_be32s(f, &s->start);
    qemu_put_be32s(f, &s->stop);
    qemu_put_buffer(f, s->mult, 8);
    qemu_put_buffer(f, s->mem, sizeof(s->mem));

    /* Save all members of struct between scv_stat and mem. */
    qemu_put_8s(f, &s->scb_stat);
    qemu_put_8s(f, &s->int_stat);
    for (i = 0; i < 3; i++) {
        qemu_put_be32s(f, &s->region[i]);
    }
    qemu_put_buffer(f, s->macaddr, 6);
    for (i = 0; i < 32; i++) {
        qemu_put_be16s(f, &s->mdimem[i]);
    }
    /* The eeprom should be saved and restored by its own routines. */
    qemu_put_be32s(f, &s->device);
    qemu_put_be32s(f, &s->pointer);
    qemu_put_be32s(f, &s->cu_base);
    qemu_put_be32s(f, &s->cu_offset);
    qemu_put_be32s(f, &s->ru_base);
    qemu_put_be32s(f, &s->ru_offset);
    qemu_put_be32s(f, &s->statsaddr);
    /* Save epro100_stats_t statistics. */
    qemu_put_be32s(f, &s->statistics.tx_good_frames);
    qemu_put_be32s(f, &s->statistics.tx_max_collisions);
    qemu_put_be32s(f, &s->statistics.tx_late_collisions);
    qemu_put_be32s(f, &s->statistics.tx_underruns);
    qemu_put_be32s(f, &s->statistics.tx_lost_crs);
    qemu_put_be32s(f, &s->statistics.tx_deferred);
    qemu_put_be32s(f, &s->statistics.tx_single_collisions);
    qemu_put_be32s(f, &s->statistics.tx_multiple_collisions);
    qemu_put_be32s(f, &s->statistics.tx_total_collisions);
    qemu_put_be32s(f, &s->statistics.rx_good_frames);
    qemu_put_be32s(f, &s->statistics.rx_crc_errors);
    qemu_put_be32s(f, &s->statistics.rx_alignment_errors);
    qemu_put_be32s(f, &s->statistics.rx_resource_errors);
    qemu_put_be32s(f, &s->statistics.rx_overrun_errors);
    qemu_put_be32s(f, &s->statistics.rx_cdt_errors);
    qemu_put_be32s(f, &s->statistics.rx_short_frame_errors);
    qemu_put_be32s(f, &s->statistics.fc_xmt_pause);
    qemu_put_be32s(f, &s->statistics.fc_rcv_pause);
    qemu_put_be32s(f, &s->statistics.fc_rcv_unsupported);
    qemu_put_be16s(f, &s->statistics.xmt_tco_frames);
    qemu_put_be16s(f, &s->statistics.rcv_tco_frames);
    qemu_put_be32s(f, &s->statistics.complete);
#if 0
    qemu_put_be16s(f, &s->status);
#endif

    /* Configuration bytes. */
    qemu_put_buffer(f, s->configuration, sizeof(s->configuration));
}

static void nic_cleanup(VLANClientState *vc)
{
    EEPRO100State *s = vc->opaque;

    unregister_savevm(vc->model, s);

    eeprom93xx_free(s->eeprom);
}

static int pci_nic_uninit(PCIDevice *dev)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *) dev;
    EEPRO100State *s = &d->eepro100;

    cpu_unregister_io_memory(s->mmio_index);

    return 0;
}

static void nic_init(PCIDevice *pci_dev, uint32_t device)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *)pci_dev;
    EEPRO100State *s = &d->eepro100;

    TRACE(OTHER, logout("\n"));

    d->dev.unregister = pci_nic_uninit;

    s->device = device;
    s->pci_dev = &d->dev;

    pci_reset(s);

#if EEPROM_SIZE > 0
    /* Add 64 * 2 EEPROM. i82557 and i82558 support a 64 word EEPROM,
     * i82559 and later support 64 or 256 word EEPROM. */
    s->eeprom = eeprom93xx_new(EEPROM_SIZE);
#endif

    /* Handler for memory-mapped I/O */
    d->eepro100.mmio_index =
        cpu_register_io_memory(pci_mmio_read, pci_mmio_write, s);

    pci_register_bar(&d->dev, 0, PCI_MEM_SIZE,
                           PCI_ADDRESS_SPACE_MEM |
                           PCI_ADDRESS_SPACE_MEM_PREFETCH, pci_mmio_map);
    pci_register_bar(&d->dev, 1, PCI_IO_SIZE, PCI_ADDRESS_SPACE_IO,
                           pci_map);
    pci_register_bar(&d->dev, 2, PCI_FLASH_SIZE, PCI_ADDRESS_SPACE_MEM,
                           pci_mmio_map);

    qdev_get_macaddr(&d->dev.qdev, s->macaddr);
    assert(s->region[1] == 0);

    nic_reset(s);

    s->vc = qdev_get_vlan_client(&d->dev.qdev,
                                 nic_can_receive, nic_receive, NULL,
                                 nic_cleanup, s);

    qemu_format_nic_info_str(s->vc, s->macaddr);
    TRACE(OTHER, logout("%s\n", s->vc->info_str));

    qemu_register_reset(nic_reset, s);

    register_savevm(s->vc->model, eepro100_instance, eepro100_version,
                    nic_save, nic_load, s);
}

static void pci_i82550_init(PCIDevice *dev)
{
    nic_init(dev, i82550);
}

static void pci_i82551_init(PCIDevice *dev)
{
    nic_init(dev, i82551);
}

static void pci_i82557a_init(PCIDevice *dev)
{
    nic_init(dev, i82557A);
}

static void pci_i82557b_init(PCIDevice *dev)
{
    nic_init(dev, i82557B);
}

static void pci_i82557c_init(PCIDevice *dev)
{
    nic_init(dev, i82557C);
}

static void pci_i82558a_init(PCIDevice *dev)
{
    nic_init(dev, i82558A);
}

static void pci_i82558b_init(PCIDevice *dev)
{
    nic_init(dev, i82558B);
}

static void pci_i82559a_init(PCIDevice *dev)
{
    nic_init(dev, i82559A);
}

static void pci_i82559b_init(PCIDevice *dev)
{
    nic_init(dev, i82559B);
}

static void pci_i82559c_init(PCIDevice *dev)
{
    nic_init(dev, i82559C);
}

static void pci_i82559er_init(PCIDevice *dev)
{
    nic_init(dev, i82559ER);
}

static void pci_i82562_init(PCIDevice *dev)
{
    nic_init(dev, i82562);
}

static PCIDeviceInfo eepro100_info[] = {
    {
        .qdev.name = "i82550",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82550_init,
    },{
        .qdev.name = "i82551",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82551_init,
    },{
        .qdev.name = "i82557a",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82557a_init,
    },{
        .qdev.name = "i82557b",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82557b_init,
    },{
        .qdev.name = "i82557c",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82557c_init,
    },{
        .qdev.name = "i82558a",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82558a_init,
    },{
        .qdev.name = "i82558b",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82558b_init,
    },{
        .qdev.name = "i82559a",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82559a_init,
    },{
        .qdev.name = "i82559b",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82559b_init,
    },{
        .qdev.name = "i82559c",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82559c_init,
    },{
        .qdev.name = "i82559er",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82559er_init,
    },{
        .qdev.name = "i82562",
        .qdev.size = sizeof(PCIEEPRO100State),
        .init      = pci_i82562_init,
    },{
        /* end of list */
    }
};

static void eepro100_register_devices(void)
{
    pci_qdev_register_many(eepro100_info);
}

device_init(eepro100_register_devices)

/* eof */
