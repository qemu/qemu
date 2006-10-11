/*
 * QEMU i8255x (EEPRO100) emulation
 *
 * Copyright (c) 2006 Stefan Weil
 *
 * Portions of the code are copies from grub / etherboot eepro100.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* References:
 *
 * Intel 8255x 10/100 Mbps Ethernet Controller Family
 * Open Source Software Developer Manual
 */

#include <assert.h>
#include "vl.h"
#include "eeprom9346.h"

/* Common declarations for all PCI devices. */

#define PCI_VENDOR_ID           0x00    /* 16 bits */
#define PCI_DEVICE_ID           0x02    /* 16 bits */
#define PCI_COMMAND             0x04    /* 16 bits */
#define PCI_STATUS              0x06    /* 16 bits */

#define PCI_REVISION_ID         0x08    /* 8 bits  */
#define PCI_CLASS_CODE          0x0b    /* 8 bits */
#define PCI_SUBCLASS_CODE       0x0a    /* 8 bits */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits */

#define PCI_BASE_ADDRESS_0      0x10    /* 32 bits */
#define PCI_BASE_ADDRESS_1      0x14    /* 32 bits */
#define PCI_BASE_ADDRESS_2      0x18    /* 32 bits */
#define PCI_BASE_ADDRESS_3      0x1c    /* 32 bits */
#define PCI_BASE_ADDRESS_4      0x20    /* 32 bits */
#define PCI_BASE_ADDRESS_5      0x24    /* 32 bits */

#define KiB 1024

/* debug EEPRO100 card */
#define DEBUG_EEPRO100

#ifdef DEBUG_EEPRO100
#define logout(fmt, args...) printf("EE100\t%-24s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

#define missing(text)       assert(!"feature is missing in this emulation: " text)

#define MAX_ETH_FRAME_SIZE 1514

/* This driver supports several different devices which are declared here. */
#define i82551          0x82551
#define i82557B         0x82557b
#define i82557C         0x82557c
#define i82558B         0x82558b
#define i82559C         0x82559c
#define i82559ER        0x82559e

#define DEVICE          i82559ER

#define PCI_MEM_SIZE            (4 * KiB)
#define PCI_IO_SIZE             64
#define PCI_FLASH_SIZE          (128 * KiB)


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
#define DRVR_INT        0x0200          /* Driver generated interrupt. */

typedef unsigned char bool;

/* Offsets to the various registers.
   All accesses need not be longword aligned. */
enum speedo_offsets {
        SCBStatus = 0, SCBCmd = 2,      /* Rx/Command Unit command and status. */
        SCBIntmask = 3,
        SCBPointer = 4,                 /* General purpose pointer. */
        SCBPort = 8,                    /* Misc. commands and operands.  */
        SCBflash = 12, SCBeeprom = 14,  /* EEPROM and flash memory control. */
        SCBCtrlMDI = 16,                /* MDI interface control. */
        SCBEarlyRx = 20,                /* Early receive byte count. */
};

/* A speedo3 transmit buffer descriptor with two buffers... */
typedef struct {
  uint16_t status;
  uint16_t command;
  uint32_t link;          /* void * */
  uint32_t tx_desc_addr;  /* (almost) Always points to the tx_buf_addr element. */
  int32_t  count;         /* # of TBD (=2), Tx start thresh., etc. */
                     /* This constitutes two "TBD" entries: hdr and data */
  uint32_t tx_buf_addr0;  /* void *, header of frame to be transmitted.  */
  int32_t  tx_buf_size0;  /* Length of Tx hdr. */
  uint32_t tx_buf_addr1;  /* void *, data to be transmitted.  */
  int32_t  tx_buf_size1;  /* Length of Tx data. */
} eepro100_tx_t;

/* Receive frame descriptor. */
typedef struct {
  int16_t  status;
  uint16_t command;
  uint32_t link;                 /* struct RxFD * */
  uint32_t rx_buf_addr;          /* void * */
  uint16_t count;
  uint16_t size;
  char packet[MAX_ETH_FRAME_SIZE + 4];
} eepro100_rx_t;

typedef struct {
    uint32_t tx_good_frames;
    uint32_t tx_coll16_errs;
    uint32_t tx_late_colls;
    uint32_t tx_underruns;
    uint32_t tx_lost_carrier;
    uint32_t tx_deferred;
    uint32_t tx_one_colls;
    uint32_t tx_multi_colls;
    uint32_t tx_total_colls;
    uint32_t rx_good_frames;
    uint32_t rx_crc_errs;
    uint32_t rx_align_errs;
    uint32_t rx_resource_errs;
    uint32_t rx_overrun_errs;
    uint32_t rx_colls_errs;
    uint32_t rx_runt_errs;
    uint32_t done_marker;
} eepro100_stats_t;

typedef enum {
    cu_idle = 0,
    cu_suspended,
    cu_active
} cu_state_t;

typedef struct {
#if 1
    uint8_t cmd;
    uint32_t start;
    uint32_t stop;
    uint8_t boundary;
    uint8_t tsr;
    uint8_t tpsr;
    uint16_t tcnt;
    uint16_t rcnt;
    uint32_t rsar;
    uint8_t rsr;
    uint8_t rxcr;
    uint8_t isr;
    uint8_t dcfg;
    uint8_t imr;
    uint8_t phys[6]; /* mac address */
    uint8_t curpag;
    uint8_t mult[8]; /* multicast mask array */
    int mmio_index;
    PCIDevice *pci_dev;
    VLANClientState *vc;
#endif
    uint32_t region[3]; /* PCI region addresses */
    uint8_t macaddr[6];
    uint32_t statcounter[19];
    uint16_t mdimem[32];
    eeprom_t *eeprom;
    uint32_t device;    /* device variant */
    uint32_t pointer;
    cu_state_t cu_state;
    uint32_t cu_base;   /* CU base address */
    uint32_t cu_offset; /* CU address offset */
    uint32_t ru_base;   /* RU base address */
    uint32_t ru_offset; /* RU address offset */
    uint32_t statsaddr; /* pointer to eepro100_stats_t */
#if 0
    uint16_t status;
#endif

    /* Data in mem is always in the byte order of the controller (le). */
    uint8_t mem[PCI_MEM_SIZE];
} EEPRO100State;

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

static void eepro100_update_irq(EEPRO100State *s)
{
    int isr = (s->isr & s->imr) & 0x7f;
    logout("Set IRQ line to %d (%02x %02x)\n", isr ? 1 : 0, s->isr, s->imr);
    /* PCI irq */
    pci_set_irq(s->pci_dev, 0, (isr != 0));
}

#define POLYNOMIAL 0x04c11db6

/* From FreeBSD */
/* XXX: optimize */
static int compute_mcast_idx(const uint8_t *ep)
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
            if (carry)
                crc = ((crc ^ POLYNOMIAL) | carry);
        }
    }
    return (crc >> 26);
}

static int eepro100_buffer_full(EEPRO100State *s)
{
    int is_full = 1;
    if (s->ru_offset != 0) {
        eepro100_rx_t rx;
        cpu_physical_memory_read(s->ru_base + s->ru_offset, (uint8_t *)&rx, sizeof(rx)); // !!! optimize
        is_full = (rx.status != 0);
    }
    return is_full;
    //~ int avail, index, boundary;

    //~ index = s->curpag << 8;
    //~ boundary = s->boundary << 8;
    //~ if (index <= boundary)
        //~ avail = boundary - index;
    //~ else
        //~ avail = (s->stop - s->start) - (index - boundary);
    //~ if (avail < (MAX_ETH_FRAME_SIZE + 4))
        //~ return 1;
    //~ return 0;
}

static void eepro100_interrupt(EEPRO100State *s, uint8_t statbyte)
{
    s->mem[SCBStatus + 1] |= statbyte;
    if ((s->mem[SCBIntmask] & 0x01) == 0) {
        /* SCB Bit M */
        logout("interrupt handling\n");
        pci_set_irq(s->pci_dev, 0, 1);
    }
}

static void eepro100_interrupt_cna(EEPRO100State *s)
{
    eepro100_interrupt(s, 0);
    //~ s->mem[SCBStatus + 1] |= statbyte;
    //~ if ((s->mem[SCBIntmask] & 0x01) == 0) {
        //~ /* SCB Bit M */
        //~ missing("interrupt handling");
    //~ }
}

static void eepro100_interrupt_cx(EEPRO100State *s)
{
    eepro100_interrupt(s, 0);
    //~ s->mem[SCBStatus + 1] |= statbyte;
    //~ if ((s->mem[SCBIntmask] & 0x01) == 0) {
        //~ /* SCB Bit M */
        //~ missing("interrupt handling");
    //~ }
}

static int nic_can_receive(void *opaque)
{
    EEPRO100State *s = opaque;
    logout("%p\n", s);
    return !eepro100_buffer_full(s);
}

#define MIN_BUF_SIZE 60

static void nic_receive(void *opaque, const uint8_t *buf, int size)
{
    EEPRO100State *s = opaque;
    uint8_t *p;
    int total_len, next, avail, len, index, mcast_idx;
    uint8_t buf1[60];
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    logout("%p received len=%d\n", s, size);

    if (eepro100_buffer_full(s))
        return;

    //~ !!!
//~ $3 = {status = 0x0, command = 0xc000, link = 0x2d220, rx_buf_addr = 0x207dc, count = 0x0, size = 0x5f8, packet = {0x0 <repeats 1518 times>}}
    eepro100_rx_t rx;
    cpu_physical_memory_read(s->ru_base + s->ru_offset, (uint8_t *)&rx, sizeof(rx));
    assert(size <= rx.size);
    memcpy(&rx.packet[0], buf, size);
    //~ cpu_physical_memory_write(rx.rx_buf_addr, buf, size);
    rx.count = size;
    rx.status = 1; // !!! check value
    cpu_physical_memory_write(s->ru_base + s->ru_offset, (uint8_t *)&rx, sizeof(rx));
    eepro100_interrupt(s, 0x40);
    return;

    /* XXX: check this */
    if (s->rxcr & 0x10) {
        /* promiscuous: receive all */
    } else {
        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            /* broadcast address */
            if (!(s->rxcr & 0x04))
                return;
        } else if (buf[0] & 0x01) {
            /* multicast */
            if (!(s->rxcr & 0x08))
                return;
            mcast_idx = compute_mcast_idx(buf);
            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))))
                return;
        } else if (s->mem[0] == buf[0] &&
                   s->mem[2] == buf[1] &&
                   s->mem[4] == buf[2] &&
                   s->mem[6] == buf[3] &&
                   s->mem[8] == buf[4] &&
                   s->mem[10] == buf[5]) {
            /* match */
        } else {
            return;
        }
    }


    /* if too small buffer, then expand it */
    if (size < MIN_BUF_SIZE) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, MIN_BUF_SIZE - size);
        buf = buf1;
        size = MIN_BUF_SIZE;
    }

    index = s->curpag << 8;
    /* 4 bytes for header */
    total_len = size + 4;
    /* address for next packet (4 bytes for CRC) */
    next = index + ((total_len + 4 + 255) & ~0xff);
    if (next >= s->stop)
        next -= (s->stop - s->start);
    /* prepare packet header */
    p = s->mem + index;
    //~ s->rsr = ENRSR_RXOK; /* receive status */
    /* XXX: check this */
    //~ if (buf[0] & 0x01)
        //~ s->rsr |= ENRSR_PHY;
    p[0] = s->rsr;
    p[1] = next >> 8;
    p[2] = total_len;
    p[3] = total_len >> 8;
    index += 4;

    /* write packet data */
    while (size > 0) {
        avail = s->stop - index;
        len = size;
        if (len > avail)
            len = avail;
        memcpy(s->mem + index, buf, len);
        buf += len;
        index += len;
        if (index == s->stop)
            index = s->start;
        size -= len;
    }
    s->curpag = next >> 8;

    /* now we can signal we have receive something */
    //~ s->isr |= ENISR_RX;
    eepro100_update_irq(s);
}

#define PCI_CONFIG_8(offset, value) \
    (pci_conf[offset] = (value))
#define PCI_CONFIG_16(offset, value) \
    (*(uint16_t *)&pci_conf[offset] = cpu_to_le16(value))
#define PCI_CONFIG_32(offset, value) \
    (*(uint32_t *)&pci_conf[offset] = cpu_to_le32(value))

static void pci_reset(EEPRO100State *s)
{
    uint32_t device = s->device;
    uint8_t *pci_conf = s->pci_dev->config;

    logout("%p\n", s);

    /* PCI Vendor ID */
    PCI_CONFIG_16(PCI_VENDOR_ID, 0x8086);
    /* PCI Device ID */
    PCI_CONFIG_16(PCI_DEVICE_ID, 0x1209);
    /* PCI Command */
    PCI_CONFIG_16(PCI_COMMAND, 0x0000);
    /* PCI Status */
    PCI_CONFIG_16(PCI_STATUS, 0x2800);
    /* PCI Revision ID */
    PCI_CONFIG_8(PCI_REVISION_ID, 0x08);
    /* PCI Class Code */
    PCI_CONFIG_8(0x09, 0x00);
    PCI_CONFIG_8(PCI_SUBCLASS_CODE, 0x00); // ethernet network controller
    PCI_CONFIG_8(PCI_CLASS_CODE, 0x02); // network controller
    /* PCI Cache Line Size */
    /* check cache line size!!! */
    //~ PCI_CONFIG_8(0x0c, 0x00);
    /* PCI Latency Timer */
    PCI_CONFIG_8(0x0d, 0x20); // latency timer = 32 clocks
    /* PCI Header Type */
    /* BIST (built-in self test) */
    /* PCI Base Address Registers */
    //~ /* CSR Memory Mapped Base Address */
    //~ PCI_CONFIG_32(PCI_BASE_ADDRESS_0, PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_MEM_PREFETCH);
    //~ /* CSR I/O Mapped Base Address */
    //~ PCI_CONFIG_32(PCI_BASE_ADDRESS_1, PCI_ADDRESS_SPACE_IO);
    //~ /* Flash Memory Mapped Base Address */
    //~ PCI_CONFIG_32(PCI_BASE_ADDRESS_2, 0xfffe0000 | PCI_ADDRESS_SPACE_MEM);
    /* Expansion ROM Base Address (depends on boot disable!!!) */
    PCI_CONFIG_32(0x30, 0x00000000);
    /* Capability Pointer */
    PCI_CONFIG_8(0x34, 0xdc);
    /* Interrupt Pin */
    PCI_CONFIG_8(0x3d, 1); // interrupt pin 0
    /* Minimum Grant */
    PCI_CONFIG_8(0x3e, 0x08);
    /* Maximum Latency */
    PCI_CONFIG_8(0x3f, 0x18);
    /* Power Management Capabilities / Next Item Pointer / Capability ID */
    PCI_CONFIG_32(0xdc, 0x7e210001);

    switch (device) {
        case i82551:
            //~ PCI_CONFIG_16(PCI_DEVICE_ID, 0x1209);
            PCI_CONFIG_8(PCI_REVISION_ID, 0x0f);
            break;
        case i82557B:
            PCI_CONFIG_16(PCI_DEVICE_ID, 0x1229);
            PCI_CONFIG_8(PCI_REVISION_ID, 0x02);
            break;
        case i82557C:
            PCI_CONFIG_16(PCI_DEVICE_ID, 0x1229);
            PCI_CONFIG_8(PCI_REVISION_ID, 0x03);
            break;
        case i82558B:
            PCI_CONFIG_16(PCI_DEVICE_ID, 0x1229);
            PCI_CONFIG_16(PCI_STATUS, 0x2810);
            PCI_CONFIG_8(PCI_REVISION_ID, 0x05);
            break;
        case i82559C:
            PCI_CONFIG_16(PCI_DEVICE_ID, 0x1229);
            PCI_CONFIG_16(PCI_STATUS, 0x2810);
            //~ PCI_CONFIG_8(PCI_REVISION_ID, 0x08);
            break;
        case i82559ER:
            //~ PCI_CONFIG_16(PCI_DEVICE_ID, 0x1209);
            PCI_CONFIG_16(PCI_STATUS, 0x2810);
            PCI_CONFIG_8(PCI_REVISION_ID, 0x09);
            break;
        default:
            logout("Device %X is undefined!\n", device);
    }

    if (device == i82557C || device == i82558B || device == i82559C) {
        logout("Get device id and revision from EEPROM!!!\n");
    }
}

static void nic_selective_reset(EEPRO100State *s)
{
    eeprom9346_reset(s->eeprom, s->macaddr);

    memset(s->mem, 0, sizeof(s->mem));
    uint32_t val = (1 << 21);
    memcpy(&s->mem[SCBCtrlMDI], &val, sizeof(val));

    assert(sizeof(s->mdimem) == sizeof(eepro100_mdi_default));
    memcpy(&s->mdimem[0], &eepro100_mdi_default[0], sizeof(s->mdimem));
}

static void nic_reset(void *opaque)
{
    EEPRO100State *s = (EEPRO100State *)opaque;
    logout("%p\n", s);
static int first;
    if (!first) {
      first = 1;
    }
    nic_selective_reset(s);
}

static const char *reg[PCI_IO_SIZE / 4] = {
  "Command/Status",
  "General Pointer",
  "Port",
  "EPROM/Flash Control",
  "MDI Control",
  "Receive DMA Byte Count",
  "Flow control register",
  "General Status/Control"
};

static char *regname(uint32_t addr)
{
  static char buf[16];
  if (addr < PCI_IO_SIZE) {
    const char *r = reg[addr / 4];
    if (r != 0) {
      sprintf(buf, "%s+%u", r, addr % 4);
    } else {
      sprintf(buf, "0x%02x", addr);
    }
  } else {
    sprintf(buf, "??? 0x%08x", addr);
  }
  return buf;
}

#if 0
static uint16_t eepro100_read_status(EEPRO100State *s)
{
    uint16_t val = s->status;
    logout("val=0x%04x\n", val);
    return val;
}

static void eepro100_write_status(EEPRO100State *s, uint16_t val)
{
    logout("val=0x%04x\n", val);
    s->status = val;
}
#endif

#if 0
static uint16_t eepro100_read_command(EEPRO100State *s)
{
    uint16_t val = 0xffff;
    //~ logout("val=0x%04x\n", val);
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
  CmdTDR = 5,           /* load microcode */
  CmdDump = 6,
  CmdDiagnose = 7,

  /* And some extra flags: */
  CmdSuspend = 0x4000,      /* Suspend after completion. */
  CmdIntr = 0x2000,         /* Interrupt after completion. */
  CmdTxFlex = 0x0008,       /* Use "Flexible mode" for CmdTx command. */
};

static void eepro100_cu_command(EEPRO100State *s, uint8_t val)
{
    eepro100_tx_t tx;
    switch (val) {
        case CU_NOP:
            /* No operation. */
            break;
        case CU_START:
            assert(s->cu_state == cu_idle);
            s->cu_state = cu_active;
            s->cu_offset = s->pointer;
//~ (gdb) p/x tx
//~ $5 = {status = 0x0, command = 0x1, link = 0x208e0, tx_desc_addr = 0x12005452, count = 0x5634, tx_buf_addr0 = 0x0, tx_buf_size0 = 0x0,
  //~ tx_buf_addr1 = 0x0, tx_buf_size1 = 0x0}
//~ $12 = {status = 0x0, command = 0x400c, link = 0x2d200, tx_desc_addr = 0x2d210, count = 0x2208000, tx_buf_addr0 = 0x65d40,
  //~ tx_buf_size0 = 0xe, tx_buf_addr1 = 0x65d94, tx_buf_size1 = 0x1c}
          next_command:
            cpu_physical_memory_read(s->cu_base + s->cu_offset, (uint8_t *)&tx, sizeof(tx));
            uint16_t status = le16_to_cpu(tx.status);
            uint16_t command = le16_to_cpu(tx.command);
            logout("val=0x%02x (cu start), status=0x%04x, command=0x%04x, link=0x%08x\n",
                   val, status, command, tx.link);
            bool bit_el = ((command & 0x8000) != 0);
            bool bit_s = ((command & 0x4000) != 0);
            bool bit_i = ((command & 0x2000) != 0);
            bool bit_nc = ((command & 0x0010) != 0);
            bool bit_sf = ((command & 0x0008) != 0);
            uint16_t cmd = command & 0x0007;
            switch (cmd) {
                uint8_t buf[MAX_ETH_FRAME_SIZE + 4];
                int size;
                case CmdNOp:
                    missing("nop");
                    break;
                case CmdIASetup:
                    //~ missing("IA setup");
                    break;
                case CmdConfigure:
                    //~ missing("configure");
                    break;
                case CmdMulticastList:
                    //~ missing("configure");
                    break;
                case CmdTx:
                    assert(!bit_nc);
                    //~ assert(!bit_sf);
                    cpu_physical_memory_read(tx.tx_buf_addr0, &buf[0], tx.tx_buf_size0);
                    size = tx.tx_buf_size0;
                    cpu_physical_memory_read(tx.tx_buf_addr1, &buf[size], tx.tx_buf_size1);
                    size += tx.tx_buf_size1;
                    qemu_send_packet(s->vc, buf, size);
                    /* Write new status (success). */
                    stw_phys(s->cu_base + s->cu_offset, status | 0xc000);
                    eepro100_interrupt(s, 0x80);
                    break;
                case CmdTDR:
                    logout("load microcode\n");
                    /* Starting with offset 8, the command contains
                     * 64 dwords microcode which we just ignore here.
                     * Write new status (success). */
                    stw_phys(s->cu_base + s->cu_offset, status | 0xa000);
                    break;
                default:
                    missing("undefined command");
            }
            if (bit_i) {
                eepro100_interrupt_cx(s);
            }
            if (bit_el) {
                /* CPU becomes idle. */
                s->cu_state = cu_idle;
                //~ Interrupt with CNA/CI bits set
            } else if (bit_s) {
                /* CPU becomes suspended. */
                s->cu_state = cu_suspended;
                s->cu_offset = le32_to_cpu(tx.link);
                eepro100_interrupt_cna(s);
            } else {
                /* More entries in list. */
                logout("CU list with at least one more entry\n");
                s->cu_offset = le32_to_cpu(tx.link);
                goto next_command;
            }
            logout("CU list empty\n");
            /* List is empty. Now CU is idle or suspended. */
            break;
        case CU_RESUME:
            if (s->cu_state == cu_suspended) {
                logout("CU resuming\n");
                s->cu_state = cu_active;
                goto next_command;
            }
            logout("cu_state=%u\n", s->cu_state);
            missing("cu resume");
            break;
        case CU_STATSADDR:
            /* Load dump counters address. */
            s->statsaddr = s->pointer;
            logout("val=0x%02x (status address)\n", val);
            break;
        case CU_SHOWSTATS:
            /* Dump statistical counters. */
            missing("CU dump statistical counters");
            break;
        case CU_CMD_BASE:
            /* Load CU base. */
            logout("val=0x%02x (CU base address)\n", val);
            s->cu_base = s->pointer;
            break;
        case CU_DUMPSTATS:
            /* Dump and reset statistical counters. */
            //~ missing("CU dump and reset statistical counters");
            break;
        case CU_SRESUME:
            /* CU static resume. */
            missing("CU static resume");
            break;
        default:
            missing("Undefined CU command");
    }
}

static void eepro100_ru_command(EEPRO100State *s, uint8_t val)
{
    eepro100_rx_t rx;
    switch (val) {
        case RU_NOP:
            /* No operation. */
            break;
        case RX_START:
            /* RU start. */
            s->ru_offset = s->pointer;
//~ (gdb) p/x rx
//~ $3 = {status = 0x0, command = 0xc000, link = 0x2d220, rx_buf_addr = 0x207dc, count = 0x0, size = 0x5f8, packet = {0x0 <repeats 1518 times>}}
            cpu_physical_memory_read(s->ru_base + s->ru_offset, (uint8_t *)&rx, sizeof(rx));
            logout("val=0x%02x (rx start, status=0x%04x, command=0x%04x\n",
                   val, rx.status, rx.command);
            break;
        case RX_RESUME:
            missing("RX_RESUME");
            break;
        case RX_ADDR_LOAD:
            /* Load RU base. */
            logout("val=0x%02x (RU base address)\n", val);
            s->ru_base = s->pointer;
            break;
        default:
            logout("val=0x%02x (undefined RU command)\n", val);
            missing("Undefined SU command");
      }
}

static void eepro100_write_command(EEPRO100State *s, uint8_t val)
{
    eepro100_ru_command(s, val & 0x0f);
    eepro100_cu_command(s, val & 0xf0);
    if ((val) == 0) {
        logout("val=0x%02x\n", val);
    }
    /* Clear command byte after command was accepted. */
    s->mem[SCBCmd] = 0;
}



#define EEPROM_CS       0x02
#define EEPROM_SK       0x01
#define EEPROM_DI       0x04
#define EEPROM_DO       0x08

static uint16_t eepro100_read_eeprom(EEPRO100State *s)
{
    uint16_t val;
    memcpy(&val, &s->mem[SCBeeprom], sizeof(val));
    if (eeprom9346_read(s->eeprom)) {
        val |= EEPROM_DO;
    } else {
        val &= ~EEPROM_DO;
    }
    return val;
}

static void eepro100_write_eeprom(eeprom_t *eeprom, uint8_t val)
{
    logout("write val=0x%02x\n", val);

    /* mask unwriteable bits */
    //~ val = SET_MASKED(val, 0x31, eeprom->value);

    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);
    eeprom9346_write(eeprom, eecs, eesk, eedi);
}







static void eepro100_write_pointer(EEPRO100State *s, uint32_t val)
{
    s->pointer = le32_to_cpu(val);
    logout("val=0x%08x\n", val);
}

static uint32_t eepro100_read_mdi(EEPRO100State *s)
{
    uint32_t val;
    memcpy(&val, &s->mem[0x10], sizeof(val));

    uint8_t raiseint = (val & 0x20000000) >> 29;
    uint8_t opcode = (val & 0x0c000000) >> 26;
    uint8_t phy = (val & 0x03e00000) >> 21;
    uint8_t reg = (val & 0x001f0000) >> 16;
    uint16_t data = (val & 0x0000ffff);
    /* Emulation takes no time to finish MDI transaction. */
    val |= (1 << 28);
    logout("val=0x%08x (int=%u, opcode=%u, phy=%u, reg=%u, data=0x%04x\n",
           val, raiseint, opcode, phy, reg, data);
    return val;
}

//~ #define BITS(val, upper, lower) (val & ???)
static void eepro100_write_mdi(EEPRO100State *s, uint32_t val)
{
    uint8_t raiseint = (val & 0x20000000) >> 29;
    uint8_t opcode = (val & 0x0c000000) >> 26;
    uint8_t phy = (val & 0x03e00000) >> 21;
    uint8_t reg = (val & 0x001f0000) >> 16;
    uint16_t data = (val & 0x0000ffff);
    if (phy != 1) {
        /* Unsupported PHY address. */
        //~ logout("phy must be 1 but is %u\n", phy);
        data = 0;
    } else if (opcode != 1 && opcode != 2) {
        /* Unsupported opcode. */
        logout("opcode must be 1 or 2 but is %u\n", opcode);
        data = 0;
    } else {
        logout("val=0x%08x (int=%u, opcode=%u, phy=%u, reg=%u, data=0x%04x\n",
               val, raiseint, opcode, phy, reg, data);
        if (opcode == 1) {
            /* MDI write */
            switch (reg) {
                case 0:
                    if (data & 0x8000) {
                        /* Reset status and control registers to default. */
                        data = s->mdimem[0] = eepro100_mdi_default[0];
                        s->mdimem[1] = eepro100_mdi_default[1];
                    }
                    break;
            }
            s->mdimem[reg] = data;
        } else if (opcode == 2) {
            /* MDI read */
            switch (reg) {
                case 0:
                    if (data & 0x8000) {
                        /* Reset status and control registers to default. */
                        s->mdimem[0] = eepro100_mdi_default[0];
                        s->mdimem[1] = eepro100_mdi_default[1];
                    }
                    break;
                case 1:
                    s->mdimem[reg] |= 0x0020;
                    break;
            }
            data = s->mdimem[reg];
        }
        /* Emulation takes no time to finish MDI transaction.
         * Set MDI bit in SCB status register. */
        s->mem[SCBStatus + 1] |= 0x08;
        val |= (1 << 28);
        if (raiseint) {
            eepro100_interrupt(s, 0x08);
        }
    }
    val = (val & 0xffff0000) + data;
    memcpy(&s->mem[0x10], &val, sizeof(val));
}

#define PORT_SOFTWARE_RESET     0
#define PORT_SELFTEST           1
#define PORT_SELECTIVE_RESET    2
#define PORT_DUMP               3
#define PORT_SELECTION_MASK     3

typedef struct {
    uint32_t st_sign;   /* Self Test Signature */
    uint32_t st_result; /* Self Test Results */
} eepro100_selftest_t __attribute__ ((__packed__));

static uint32_t eepro100_read_port(EEPRO100State *s)
{
    return 0;
}

static void eepro100_write_port(EEPRO100State *s, uint32_t val)
{
    val = le32_to_cpu(val);
    uint32_t address = (val & ~PORT_SELECTION_MASK);
    uint8_t  selection = (val & PORT_SELECTION_MASK);
    switch (selection) {
        case PORT_SOFTWARE_RESET:
            nic_reset(s);
            break;
        case PORT_SELFTEST:
            logout("selftest address=0x%08x\n", address);
            eepro100_selftest_t data;
            cpu_physical_memory_read(address, (uint8_t *)&data, sizeof(data));
            data.st_sign = 0xffffffff;
            data.st_result = 0;
            cpu_physical_memory_write(address, (uint8_t *)&data, sizeof(data));
            break;
        case PORT_SELECTIVE_RESET:
            logout("selective reset, selftest address=0x%08x\n", address);
            nic_selective_reset(s);
            break;
        default:
            logout("val=0x%08x\n", val);
            missing("unknown port selection");
    }
}

static uint8_t eepro100_read1(EEPRO100State *s, uint32_t addr)
{
    uint8_t val;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&val, &s->mem[addr], sizeof(val));
    }

    switch (addr) {
        case SCBStatus:
            //~ val = eepro100_read_status(s);
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            break;
        case SCBStatus + 1:
            //~ val = eepro100_read_status(s);
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            break;
        case SCBCmd:
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            //~ val = eepro100_read_command(s);
            break;
        case SCBIntmask:
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            break;
        case SCBPort + 3:
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            break;
        case SCBeeprom:
            val = eepro100_read_eeprom(s);
            break;
        case 0x1b:      /* power management driver register */
            val = 0;
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            break;
        case 0x1d:      /* general status register */
            /* 100 Mbps full duplex, valid link */
            val = 0x07;
            logout("addr=General Status val=%02x\n", val);
            break;
        default:
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            missing("unknown byte read");
    }
    return val;
}

static uint16_t eepro100_read2(EEPRO100State *s, uint32_t addr)
{
    uint16_t val;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&val, &s->mem[addr], sizeof(val));
    }

    logout("addr=%s val=0x%04x\n", regname(addr), val);

    switch (addr) {
        case SCBStatus:
            //~ val = eepro100_read_status(s);
            break;
        case SCBeeprom:
            val = eepro100_read_eeprom(s);
            break;
        default:
            logout("addr=%s val=0x%04x\n", regname(addr), val);
            missing("unknown word read");
    }
    return val;
}

static uint32_t eepro100_read4(EEPRO100State *s, uint32_t addr)
{
    uint32_t val;
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&val, &s->mem[addr], sizeof(val));
    }

    logout("addr=%s val=0x%08x\n", regname(addr), val);

    switch (addr) {
        case SCBStatus:
            //~ val = eepro100_read_status(s);
            break;
        case SCBPointer:
            //~ val = eepro100_read_pointer(s);
            break;
        case SCBPort:
            val = eepro100_read_port(s);
            break;
        case SCBCtrlMDI:
            val = eepro100_read_mdi(s);
            break;
        default:
            logout("addr=%s val=0x%08x\n", regname(addr), val);
            missing("unknown longword read");
    }
    return val;
}

static void eepro100_write1(EEPRO100State *s, uint32_t addr, uint8_t val)
{
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&s->mem[addr], &val, sizeof(val));
    }

    logout("addr=%s val=0x%02x\n", regname(addr), val);

    switch (addr) {
        case SCBStatus:
            //~ eepro100_write_status(s, val);
            break;
        case SCBStatus + 1:
            break;
        case SCBCmd:
            eepro100_write_command(s, val);
            break;
        case SCBIntmask:
            break;
        case SCBPort + 3:
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            break;
        case SCBeeprom:
            eepro100_write_eeprom(s->eeprom, val);
            break;
        default:
            logout("addr=%s val=0x%02x\n", regname(addr), val);
            missing("unknown byte write");
    }
}

static void eepro100_write2(EEPRO100State *s, uint32_t addr, uint16_t val)
{
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&s->mem[addr], &val, sizeof(val));
    }

    logout("addr=%s val=0x%04x\n", regname(addr), val);

    switch (addr) {
        case SCBStatus:
            //~ eepro100_write_status(s, val);
            break;
        case SCBCmd:
            eepro100_write_command(s, val);
            break;
        case SCBeeprom:
            eepro100_write_eeprom(s->eeprom, val);
            break;
        default:
            logout("addr=%s val=0x%04x\n", regname(addr), val);
            missing("unknown word write");
    }
}

static void eepro100_write4(EEPRO100State *s, uint32_t addr, uint32_t val)
{
    if (addr <= sizeof(s->mem) - sizeof(val)) {
        memcpy(&s->mem[addr], &val, sizeof(val));
    }

    logout("addr=%s val=0x%08x\n", regname(addr), val);

    switch (addr) {
        case SCBPointer:
            eepro100_write_pointer(s, val);
            break;
        case SCBPort:
            eepro100_write_port(s, val);
            break;
        case SCBCtrlMDI:
            eepro100_write_mdi(s, val);
            break;
        default:
            logout("addr=%s val=0x%08x\n", regname(addr), val);
            missing("unknown longword write");
    }
}

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

typedef struct PCIEEPRO100State {
    PCIDevice dev;
    EEPRO100State eepro100;
} PCIEEPRO100State;

static void pci_map(PCIDevice *pci_dev, int region_num,
                       uint32_t addr, uint32_t size, int type)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *)pci_dev;
    EEPRO100State *s = &d->eepro100;

    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);

    assert(region_num == 1);
    register_ioport_write(addr, size, 1, ioport_write1, s);
    register_ioport_read(addr, size, 1, ioport_read1, s);
    register_ioport_write(addr, size, 2, ioport_write2, s);
    register_ioport_read(addr, size, 2, ioport_read2, s);
    register_ioport_write(addr, size, 4, ioport_write4, s);
    register_ioport_read(addr, size, 4, ioport_read4, s);

    s->region[region_num] = addr;
}

static void pci_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write1(s, addr, val);
}

static void pci_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write2(s, addr, val);
}

static void pci_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s val=0x%02x\n", regname(addr), val);
    eepro100_write4(s, addr, val);
}

static uint32_t pci_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read1(s, addr);
}

static uint32_t pci_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read2(s, addr);
}

static uint32_t pci_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    EEPRO100State *s = opaque;
    addr -= s->region[0];
    //~ logout("addr=%s\n", regname(addr));
    return eepro100_read4(s, addr);
}

static CPUWriteMemoryFunc *pci_mmio_write[] = {
    pci_mmio_writeb,
    pci_mmio_writew,
    pci_mmio_writel
};

static CPUReadMemoryFunc *pci_mmio_read[] = {
    pci_mmio_readb,
    pci_mmio_readw,
    pci_mmio_readl
};

static void pci_mmio_map(PCIDevice *pci_dev, int region_num,
                            uint32_t addr, uint32_t size, int type)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *)pci_dev;

    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);

    if (region_num == 0) {
        /* Map control / status registers. */
        cpu_register_physical_memory(addr, size, d->eepro100.mmio_index);
        d->eepro100.region[region_num] = addr;
    }
}

static int nic_load(QEMUFile* f,void* opaque,int version_id)
{
    EEPRO100State *s = (EEPRO100State *)opaque;
    int ret;

    missing("NIC load");

    if (version_id > 3)
        return -EINVAL;

    if (s->pci_dev && version_id >= 3) {
        ret = pci_device_load(s->pci_dev, f);
        if (ret < 0)
            return ret;
    }

    if (version_id >= 2) {
        qemu_get_8s(f, &s->rxcr);
    } else {
        s->rxcr = 0x0c;
    }

    qemu_get_8s(f, &s->cmd);
    qemu_get_be32s(f, &s->start);
    qemu_get_be32s(f, &s->stop);
    qemu_get_8s(f, &s->boundary);
    qemu_get_8s(f, &s->tsr);
    qemu_get_8s(f, &s->tpsr);
    qemu_get_be16s(f, &s->tcnt);
    qemu_get_be16s(f, &s->rcnt);
    qemu_get_be32s(f, &s->rsar);
    qemu_get_8s(f, &s->rsr);
    qemu_get_8s(f, &s->isr);
    qemu_get_8s(f, &s->dcfg);
    qemu_get_8s(f, &s->imr);
    qemu_get_buffer(f, s->phys, 6);
    qemu_get_8s(f, &s->curpag);
    qemu_get_buffer(f, s->mult, 8);
    qemu_get_buffer(f, s->mem, sizeof(s->mem));

    return 0;
}

static void nic_save(QEMUFile* f,void* opaque)
{
    EEPRO100State *s = (EEPRO100State *)opaque;

    missing("NIC save");

    if (s->pci_dev)
        pci_device_save(s->pci_dev, f);

    qemu_put_8s(f, &s->rxcr);

    qemu_put_8s(f, &s->cmd);
    qemu_put_be32s(f, &s->start);
    qemu_put_be32s(f, &s->stop);
    qemu_put_8s(f, &s->boundary);
    qemu_put_8s(f, &s->tsr);
    qemu_put_8s(f, &s->tpsr);
    qemu_put_be16s(f, &s->tcnt);
    qemu_put_be16s(f, &s->rcnt);
    qemu_put_be32s(f, &s->rsar);
    qemu_put_8s(f, &s->rsr);
    qemu_put_8s(f, &s->isr);
    qemu_put_8s(f, &s->dcfg);
    qemu_put_8s(f, &s->imr);
    qemu_put_buffer(f, s->phys, 6);
    qemu_put_8s(f, &s->curpag);
    qemu_put_buffer(f, s->mult, 8);
    qemu_put_buffer(f, s->mem, sizeof(s->mem));
}

static PCIEEPRO100State *nic_init(PCIBus *bus, NICInfo *nd,
                                  const char *name, uint32_t device)
{
    PCIEEPRO100State *d;
    EEPRO100State *s;

    logout("\n");

    d = (PCIEEPRO100State *)pci_register_device(bus, name,
        sizeof(PCIEEPRO100State), -1, NULL, NULL);

    s = &d->eepro100;
    s->device = device;
    s->pci_dev = &d->dev;

    pci_reset(s);

    /* Add 64 * 2 EEPROM. i82557 and i82558 support a 64 word EEPROM,
     * i82559 and later support 64 or 256 word EEPROM. */
    s->eeprom = eeprom9346_new(64);

    /* Handler for memory-mapped I/O */
    d->eepro100.mmio_index =
      cpu_register_io_memory(0, pci_mmio_read, pci_mmio_write, s);

    pci_register_io_region(&d->dev, 0, PCI_MEM_SIZE,
                           PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_MEM_PREFETCH,
                           pci_mmio_map);
    pci_register_io_region(&d->dev, 1, PCI_IO_SIZE,
                           PCI_ADDRESS_SPACE_IO, pci_map);
    pci_register_io_region(&d->dev, 2, PCI_FLASH_SIZE,
                           PCI_ADDRESS_SPACE_MEM, pci_mmio_map);

    memcpy(s->macaddr, nd->macaddr, 6);
    assert(s->region[1] == 0);

    nic_reset(s);

    s->vc = qemu_new_vlan_client(nd->vlan, nic_receive, nic_can_receive, s);

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "eepro100 pci macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             s->macaddr[0],
             s->macaddr[1],
             s->macaddr[2],
             s->macaddr[3],
             s->macaddr[4],
             s->macaddr[5]);

    qemu_register_reset(nic_reset, s);

    /* XXX: instance number ? */
    register_savevm(name, 0, 3, nic_save, nic_load, s);

    return d;
}

void pci_eepro100_init(PCIBus *bus, NICInfo *nd)
{
    /* PCIEEPRO100State *d = */
    nic_init(bus, nd, "eepro100", i82559ER);
}

void pci_i82551_init(PCIBus *bus, NICInfo *nd)
{
    /* PCIEEPRO100State *d = */
    nic_init(bus, nd, "i82551", i82551);
    //~ uint8_t *pci_conf = d->dev.config;
}

#if 0
/windows/D/BUILD/SYS/Sfos/trunk/Src/HalBsp/e100/e100_main.cpp:1734:e100_hw_init(struct e100_private *bdp)

/windows/D/BUILD/SYS/Sfos/trunk/Src/HalBsp/e100/e100_main.cpp:1672:e100_tco_workaround(struct e100_private *bdp)

Nach Reset sollten (PCI-)Interrupts gesperrt sein?
#endif
