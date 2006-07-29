/*

* read EEPROM 6, 7, 8, 9 (PMATCH)
dp83815_mmio_readl addr=0xf2001004 val=0x00000000       CFG
dp83815_mmio_writel addr=0xf200100c val=0x00000004      EELOAD_EN
dp83815_mmio_readl addr=0xf200100c val=0x00000000
dp83815_mmio_readl addr=0xf2001004 val=0x00000000
dp83815_mmio_readl addr=0xf2001040 val=0x00000000       WCSR
dp83815_mmio_readl addr=0xf2001048 val=0x0000000e       RFCR
dp83815_mmio_writel addr=0xf2001048 val=0x00000000      PMATCH 1-0
dp83815_mmio_readw addr=0xf200104c val = 0xffff         RFDR
dp83815_mmio_writel addr=0xf2001048 val=0x00000002      PMATCH 3-2
dp83815_mmio_readw addr=0xf200104c val = 0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x00000004      PMATCH 5-4
dp83815_mmio_readw addr=0xf200104c val = 0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000a      SOPAS
dp83815_mmio_readw addr=0xf200104c val = 0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000c      SOPAS
dp83815_mmio_readw addr=0xf200104c val = 0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000e      SOPAS
dp83815_mmio_readw addr=0xf200104c val = 0xffff
dp83815_mmio_writel addr=CR val=0x00000100              RST
dp83815_mmio_readl addr=0xf2001000 val=0x00000000
dp83815_mmio_readl addr=0xf2001004 val=0x00000000
dp83815_mmio_writel addr=0xf2001004 val=0x00000000
dp83815_mmio_readl addr=0xf2001040 val=0x00000000
dp83815_mmio_writel addr=0xf2001040 val=0x00000000
dp83815_mmio_readl addr=0xf2001048 val=0x0000000e
dp83815_mmio_writel addr=0xf2001048 val=0x00000000      PMATCH
dp83815_mmio_writew addr=0xf200104c val=0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x00000002
dp83815_mmio_writew addr=0xf200104c val=0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x00000004
dp83815_mmio_writew addr=0xf200104c val=0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000a      SOPAS
dp83815_mmio_writew addr=0xf200104c val=0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000c
dp83815_mmio_writew addr=0xf200104c val=0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000e
dp83815_mmio_writew addr=0xf200104c val=0xffff
dp83815_mmio_writel addr=0xf2001048 val=0x0000000e
dp83815_mmio_readw addr=0xf2001080 val = 0xffff
dp83815_mmio_readw addr=0xf2001090 val = 0xffff
dp83815_mmio_readl addr=0xf2001058 val=0x00000505       SRR


test link ready
dp83815_mmio_writew addr=0xf20010cc val=0x0001
dp83815_mmio_readw addr=0xf20010f4 val = 0x1000
dp83815_mmio_writew addr=0xf20010cc val=0x0000
dp83815_mmio_readw addr=0xf2001084 val = 0x7849
dp83815_mmio_readw addr=0xf2001084 val = 0x7849


 * QEMU emulation for National Semiconductor DP83815 / DP83816.
 * 
 * Copyright (c) 2006 Stefan Weil
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
 *
 * Datasheets are available from National Semiconductor, see
 * http://www.national.com/pf/DP/DP83815.html
 * http://www.national.com/pf/DP/DP83816.html
 */

#include "vl.h"

/* EEPROM support is optional. */
#define CONFIG_EEPROM

/* Silicon revisions for the different hardware */
#define DP83815CVNG     0x00000302
#define DP83815DVNG     0x00000403
#define DP83816AVNG     0x00000505

#define SILICON_REVISION DP83816AVNG

#if SILICON_REVISION != DP83816AVNG
# define DP83815
# warning("DP83815")
#endif

/* debug DP83815 card */
#define DEBUG_DP83815

#define MAX_ETH_FRAME_SIZE 1514

#define E8390_CMD	0x00  /* The command register (for all pages) */
/* Page 0 register offsets. */
#define EN0_CLDALO	0x01	/* Low byte of current local dma addr  RD */
#define EN0_STARTPG	0x01	/* Starting page of ring bfr WR */
#define EN0_CLDAHI	0x02	/* High byte of current local dma addr  RD */
#define EN0_STOPPG	0x02	/* Ending page +1 of ring bfr WR */
#define EN0_BOUNDARY	0x03	/* Boundary page of ring bfr RD WR */
#define EN0_TSR		0x04	/* Transmit status reg RD */
#define EN0_TPSR	0x04	/* Transmit starting page WR */
#define EN0_NCR		0x05	/* Number of collision reg RD */
#define EN0_TCNTLO	0x05	/* Low  byte of tx byte count WR */
#define EN0_FIFO	0x06	/* FIFO RD */
#define EN0_TCNTHI	0x06	/* High byte of tx byte count WR */
#define EN0_ISR		0x07	/* Interrupt status reg RD WR */
#define EN0_CRDALO	0x08	/* low byte of current remote dma address RD */
#define EN0_RSARLO	0x08	/* Remote start address reg 0 */
#define EN0_CRDAHI	0x09	/* high byte, current remote dma address RD */
#define EN0_RSARHI	0x09	/* Remote start address reg 1 */
#define EN0_RCNTLO	0x0a	/* Remote byte count reg WR */
#define EN0_RTL8029ID0	0x0a	/* Realtek ID byte #1 RD */
#define EN0_RCNTHI	0x0b	/* Remote byte count reg WR */
#define EN0_RTL8029ID1	0x0b	/* Realtek ID byte #2 RD */
#define EN0_RSR		0x0c	/* rx status reg RD */
#define EN0_RXCR	0x0c	/* RX configuration reg WR */
#define EN0_TXCR	0x0d	/* TX configuration reg WR */
#define EN0_COUNTER0	0x0d	/* Rcv alignment error counter RD */
#define EN0_DCFG	0x0e	/* Data configuration reg WR */
#define EN0_COUNTER1	0x0e	/* Rcv CRC error counter RD */
#define EN0_IMR		0x0f	/* Interrupt mask reg WR */
#define EN0_COUNTER2	0x0f	/* Rcv missed frame error counter RD */

#define EN1_PHYS        0x11
#define EN1_CURPAG      0x17
#define EN1_MULT        0x18

#define EN2_STARTPG	0x21	/* Starting page of ring bfr RD */
#define EN2_STOPPG	0x22	/* Ending page +1 of ring bfr RD */

#define EN3_CONFIG0	0x33
#define EN3_CONFIG1	0x34
#define EN3_CONFIG2	0x35
#define EN3_CONFIG3	0x36

/*  Register accessed at EN_CMD, the 8390 base addr.  */
#define E8390_STOP	0x01	/* Stop and reset the chip */
#define E8390_START	0x02	/* Start the chip, clear reset */
#define E8390_TRANS	0x04	/* Transmit a frame */
#define E8390_RREAD	0x08	/* Remote read */
#define E8390_RWRITE	0x10	/* Remote write  */
#define E8390_NODMA	0x20	/* Remote DMA */
#define E8390_PAGE0	0x00	/* Select page chip registers */
#define E8390_PAGE1	0x40	/* using the two high-order bits */
#define E8390_PAGE2	0x80	/* Page 3 is invalid. */

/* Bits in EN0_ISR - Interrupt status register */
#define ENISR_RX	0x01	/* Receiver, no error */
#define ENISR_TX	0x02	/* Transmitter, no error */
#define ENISR_RX_ERR	0x04	/* Receiver, with error */
#define ENISR_TX_ERR	0x08	/* Transmitter, with error */
#define ENISR_OVER	0x10	/* Receiver overwrote the ring */
#define ENISR_COUNTERS	0x20	/* Counters need emptying */
#define ENISR_RDC	0x40	/* remote dma complete */
#define ENISR_RESET	0x80	/* Reset completed */
#define ENISR_ALL	0x3f	/* Interrupts we will enable */

/* Bits in received packet status byte and EN0_RSR*/
#define ENRSR_RXOK	0x01	/* Received a good packet */
#define ENRSR_CRC	0x02	/* CRC error */
#define ENRSR_FAE	0x04	/* frame alignment error */
#define ENRSR_FO	0x08	/* FIFO overrun */
#define ENRSR_MPA	0x10	/* missed pkt */
#define ENRSR_PHY	0x20	/* physical/multicast address */
#define ENRSR_DIS	0x40	/* receiver disable. set in monitor mode */
#define ENRSR_DEF	0x80	/* deferring */

/* Transmitted packet status, EN0_TSR. */
#define ENTSR_PTX 0x01	/* Packet transmitted without error */
#define ENTSR_ND  0x02	/* The transmit wasn't deferred. */
#define ENTSR_COL 0x04	/* The transmit collided at least once. */
#define ENTSR_ABT 0x08  /* The transmit collided 16 times, and was deferred. */
#define ENTSR_CRS 0x10	/* The carrier sense was lost. */
#define ENTSR_FU  0x20  /* A "FIFO underrun" occurred during transmit. */
#define ENTSR_CDH 0x40	/* The collision detect "heartbeat" signal was lost. */
#define ENTSR_OWC 0x80  /* There was an out-of-window collision. */

#define DP83815_PMEM_SIZE    (32*1024)
#define DP83815_PMEM_START   (16*1024)
#define DP83815_PMEM_END     (DP83815_PMEM_SIZE+DP83815_PMEM_START)

#define DP83815_IO_SIZE         256
#define DP83815_MEM_SIZE        4096

static int dp8381x_instance = 0;
static const int dp8381x_version = 20060726;

#if defined(CONFIG_EEPROM)
typedef enum {
  eeprom_read  = 0x80,   /* read register xx */
  eeprom_write = 0x40,   /* write register xx */
  eeprom_erase = 0xc0,   /* erase register xx */
  eeprom_ewen  = 0x30,   /* erase / write enable */
  eeprom_ewds  = 0x00,   /* erase / write disable */
  eeprom_eral  = 0x20,   /* erase all registers */
  eeprom_wral  = 0x10,   /* write all registers */
  eeprom_amask = 0x0f,
  eeprom_imask = 0xf0
} eeprom_instruction_t;

typedef enum {
  EEDI  =  1,   /* EEPROM Data In */
  EEDO  =  2,   /* EEPROM Data Out */
  EECLK =  4,   /* EEPROM Serial Clock */
  EESEL =  8,   /* EEPROM Chip Select */
  MDIO  = 16,   /* MII Management Data */
  MDDIR = 32,   /* MII Management Direction */
  MDC   = 64    /* MII Management Clock */
} eeprom_bits_t;

typedef struct {
  eeprom_bits_t state;
  uint16_t command;
  uint16_t data;
  uint8_t  count;
  uint8_t  address;
  uint16_t memory[16];
} eeprom_state_t;
#endif

typedef struct {
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
    int irq;
    int io_memory;      /* handle for memory mapped I/O */
    PCIDevice *pci_dev;
    VLANClientState *vc;
    uint8_t macaddr[6];
    uint8_t mem[DP83815_MEM_SIZE];
    uint32_t silicon_revision;
#if defined(CONFIG_EEPROM)
    eeprom_state_t eeprom_state;
#endif
} DP83815State;

#if defined(CONFIG_EEPROM)

/* Emulation for FM93C46 (NMC9306) 256-Bit Serial EEPROM */

static uint16_t eeprom_map[16] = {
    /* Only 12 words are used. */
    0xd008,
    0x0400,
    0x2cd0,
    0xcf82,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0xa098,
    0x0055
};

/* Code for saving and restoring of EEPROM state. */

static int eeprom_instance = 0;
static const int eeprom_version = 20060726;

static void eeprom_save(QEMUFile *f, void *opaque)
{
    eeprom_state_t *eeprom = (eeprom_state_t *)opaque;
    /* TODO: support different endianess */
    qemu_put_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
}

int eeprom_load(QEMUFile *f, void *opaque, int version_id)
{
    eeprom_state_t *eeprom = (eeprom_state_t *)opaque;
    int result = 0;
    if (version_id == eeprom_version) {
        /* TODO: support different endianess */
        qemu_get_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
    } else {
        result = -EINVAL;
    }
    return result;
}

/* */

static uint16_t eeprom_action(eeprom_state_t *ee, eeprom_bits_t bits)
{
  uint16_t command = ee->command;
  uint8_t address = ee->address;
  uint8_t *count = &ee->count;
  eeprom_bits_t state = ee->state;

  if (bits == -1) {
    if (command == eeprom_read) {
      if (*count > 25)
        printf("%s: read data = 0x%04x, address = %u, bit = %d, state 0x%04x\n",
          __func__, ee->data, address, 26 - *count, state);
    }
    bits = state;
  } else if (bits & EESEL) {
    /* EEPROM is selected */
    if (!(state & EESEL)) {
      printf("%s: selected, state 0x%04x => 0x%04x\n", __func__, state, bits);
    } else if (!(state & EECLK) && (bits & EECLK)) {
      /* Raising edge of clock. */
      //~ printf("%s: raising clock, state 0x%04x => 0x%04x\n", __func__, state, bits);
      if (*count < 10) {
        ee->data = (ee->data << 1);
        if (bits & EEDI) {
          ee->data++;
        } else if (*count == 1) {
          *count = 0;
        }
        //~ printf("%s:   count = %d, data = 0x%04x\n", __func__, *count, data);
        *count++;
        if (*count == 10) {
          ee->address = address = (ee->data & eeprom_amask);
          ee->command = command = (ee->data & eeprom_imask);
          ee->data = eeprom_map[address];
          printf("%s: count = %d, command = 0x%02x, address = 0x%02x, data = 0x%04x\n", __func__, *count, command, address, ee->data);
        }
      //~ } else if (*count == 1 && !(bits & EEDI)) {
        /* Got start bit. */
      } else if (*count < 10 + 16) {
        if (command == eeprom_read) {
          bits = (bits & ~EEDO);
          if (ee->data & (1 << (25 - *count))) {
            bits += EEDO;
          }
        } else {
          printf("%s:   command = 0x%04x, count = %d, data = 0x%04x\n", __func__, command, *count, ee->data);
        }
        *count++;
      } else {
        printf("%s: ??? state 0x%04x => 0x%04x\n", __func__, state, bits);
      }
    } else {
      //~ printf("%s: state 0x%04x => 0x%04x\n", __func__, state, bits);
    }
  } else {
    printf("%s: not selected, count = %u, state 0x%04x => 0x%04x\n", __func__, *count, state, bits);
    ee->data = 0;
    ee->count = 0;
    ee->address = 0;
    ee->command = 0;
  }
  ee->state = state = bits;
  return state;
}

#endif

static void dp83815_reset(DP83815State *s)
{
    int i;

    s->isr = ENISR_RESET;
    memcpy(s->mem, s->macaddr, 6);
    s->mem[14] = 0x57;
    s->mem[15] = 0x57;

    /* duplicate prom data */
    for(i = 15;i >= 0; i--) {
        s->mem[2 * i] = s->mem[i];
        s->mem[2 * i + 1] = s->mem[i];
    }
}

static void dp83815_update_irq(DP83815State *s)
{
    int isr;
    isr = (s->isr & s->imr) & 0x7f;
#if defined(DEBUG_DP83815)
    printf("DP83815: Set IRQ line %d to %d (%02x %02x)\n",
           s->irq, isr ? 1 : 0, s->isr, s->imr);
#endif
    if (s->irq == 16) {
        /* PCI irq */
        pci_set_irq(s->pci_dev, 0, (isr != 0));
    } else {
        /* ISA irq */
        pic_set_irq(s->irq, (isr != 0));
    }
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

static int dp83815_buffer_full(DP83815State *s)
{
    int avail, index, boundary;

    index = s->curpag << 8;
    boundary = s->boundary << 8;
    if (index <= boundary)
        avail = boundary - index;
    else
        avail = (s->stop - s->start) - (index - boundary);
    if (avail < (MAX_ETH_FRAME_SIZE + 4))
        return 1;
    return 0;
}

static int dp83815_can_receive(void *opaque)
{
    DP83815State *s = opaque;
    
    if (s->cmd & E8390_STOP)
        return 1;
    return !dp83815_buffer_full(s);
}

#define MIN_BUF_SIZE 60

static void dp83815_receive(void *opaque, const uint8_t *buf, int size)
{
    DP83815State *s = opaque;
    uint8_t *p;
    int total_len, next, avail, len, index, mcast_idx;
    uint8_t buf1[60];
    static const uint8_t broadcast_macaddr[6] = 
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    
#if defined(DEBUG_DP83815)
    printf("DP83815: received len=%d\n", size);
#endif

    if (s->cmd & E8390_STOP || dp83815_buffer_full(s))
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
    s->rsr = ENRSR_RXOK; /* receive status */
    /* XXX: check this */
    if (buf[0] & 0x01)
        s->rsr |= ENRSR_PHY;
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

    /* now we can signal we have received something */
    s->isr |= ENISR_RX;
    dp83815_update_irq(s);
}

static void dp83815_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    DP83815State *s = opaque;
    int offset, page, index;

    addr &= 0xf;
#ifdef DEBUG_DP83815
    printf("DP83815: write addr=0x%x val=0x%02x\n", addr, val);
#endif
    if (addr == E8390_CMD) {
        /* control register */
        s->cmd = val;
        if (!(val & E8390_STOP)) { /* START bit makes no sense on RTL8029... */
            s->isr &= ~ENISR_RESET;
            /* test specific case: zero length transfert */
            if ((val & (E8390_RREAD | E8390_RWRITE)) &&
                s->rcnt == 0) {
                s->isr |= ENISR_RDC;
                dp83815_update_irq(s);
            }
            if (val & E8390_TRANS) {
                index = (s->tpsr << 8);
                /* XXX: next 2 lines are a hack to make netware 3.11 work */ 
                if (index >= DP83815_PMEM_END)
                    index -= DP83815_PMEM_SIZE;
                /* fail safe: check range on the transmitted length  */
                if (index + s->tcnt <= DP83815_PMEM_END) {
                    qemu_send_packet(s->vc, s->mem + index, s->tcnt);
                }
                /* signal end of transfert */
                s->tsr = ENTSR_PTX;
                s->isr |= ENISR_TX;
                s->cmd &= ~E8390_TRANS; 
                dp83815_update_irq(s);
            }
        }
    } else {
        page = s->cmd >> 6;
        offset = addr | (page << 4);
        switch(offset) {
        case EN0_STARTPG:
            s->start = val << 8;
            break;
        case EN0_STOPPG:
            s->stop = val << 8;
            break;
        case EN0_BOUNDARY:
            s->boundary = val;
            break;
        case EN0_IMR:
            s->imr = val;
            dp83815_update_irq(s);
            break;
        case EN0_TPSR:
            s->tpsr = val;
            break;
        case EN0_TCNTLO:
            s->tcnt = (s->tcnt & 0xff00) | val;
            break;
        case EN0_TCNTHI:
            s->tcnt = (s->tcnt & 0x00ff) | (val << 8);
            break;
        case EN0_RSARLO:
            s->rsar = (s->rsar & 0xff00) | val;
            break;
        case EN0_RSARHI:
            s->rsar = (s->rsar & 0x00ff) | (val << 8);
            break;
        case EN0_RCNTLO:
            s->rcnt = (s->rcnt & 0xff00) | val;
            break;
        case EN0_RCNTHI:
            s->rcnt = (s->rcnt & 0x00ff) | (val << 8);
            break;
        case EN0_RXCR:
            s->rxcr = val;
            break;
        case EN0_DCFG:
            s->dcfg = val;
            break;
        case EN0_ISR:
            s->isr &= ~(val & 0x7f);
            dp83815_update_irq(s);
            break;
        case EN1_PHYS ... EN1_PHYS + 5:
            s->phys[offset - EN1_PHYS] = val;
            break;
        case EN1_CURPAG:
            s->curpag = val;
            break;
        case EN1_MULT ... EN1_MULT + 7:
            s->mult[offset - EN1_MULT] = val;
            break;
        }
    }
}

static uint32_t dp83815_ioport_read(void *opaque, uint32_t addr)
{
    DP83815State *s = opaque;
    int offset, page, ret;

    addr &= 0xffff;
    if (addr == E8390_CMD) {
        ret = s->cmd;
    } else {
        page = s->cmd >> 6;
        offset = addr | (page << 4);
        switch(offset) {
        case EN0_TSR:
            ret = s->tsr;
            break;
        case EN0_BOUNDARY:
            ret = s->boundary;
            break;
        case EN0_ISR:
            ret = s->isr;
            break;
        case EN0_RSARLO:
            ret = s->rsar & 0x00ff;
            break;
        case EN0_RSARHI:
            ret = s->rsar >> 8;
            break;
        case EN1_PHYS ... EN1_PHYS + 5:
            ret = s->phys[offset - EN1_PHYS];
            break;
        case EN1_CURPAG:
            ret = s->curpag;
            break;
        case EN1_MULT ... EN1_MULT + 7:
            ret = s->mult[offset - EN1_MULT];
            break;
        case EN0_RSR:
            ret = s->rsr;
            break;
        case EN2_STARTPG:
            ret = s->start >> 8;
            break;
        case EN2_STOPPG:
            ret = s->stop >> 8;
            break;
        case EN0_RTL8029ID0:
            ret = 0x50;
            break;
        case EN0_RTL8029ID1:
            ret = 0x43;
            break;
        case EN3_CONFIG0:
            ret = 0;		/* 10baseT media */
            break;
        case EN3_CONFIG2:
            ret = 0x40;		/* 10baseT active */
            break;
        case EN3_CONFIG3:
            ret = 0x40;		/* Full duplex */
            break;
        default:
            ret = 0x00;
            break;
        }
    }
#ifdef DEBUG_DP83815
    printf("DP83815: read addr=0x%x val=%02x\n", addr, ret);
#endif
    return ret;
}

static inline void dp83815_mem_writeb(DP83815State *s, uint32_t addr, 
                                     uint32_t val)
{
    if (addr < 32 || 
        (addr >= DP83815_PMEM_START && addr < DP83815_MEM_SIZE)) {
        s->mem[addr] = val;
    }
}

static inline void dp83815_mem_writew(DP83815State *s, uint32_t addr, 
                                     uint32_t val)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= DP83815_PMEM_START && addr < DP83815_MEM_SIZE)) {
        *(uint16_t *)(s->mem + addr) = cpu_to_le16(val);
    }
}

static inline void dp83815_mem_writel(DP83815State *s, uint32_t addr, 
                                     uint32_t val)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= DP83815_PMEM_START && addr < DP83815_MEM_SIZE)) {
        cpu_to_le32wu((uint32_t *)(s->mem + addr), val);
    }
}

static inline uint32_t dp83815_mem_readb(DP83815State *s, uint32_t addr)
{
    if (addr < 32 || 
        (addr >= DP83815_PMEM_START && addr < DP83815_MEM_SIZE)) {
        return s->mem[addr];
    } else {
        return 0xff;
    }
}

static inline uint32_t dp83815_mem_readw(DP83815State *s, uint32_t addr)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= DP83815_PMEM_START && addr < DP83815_MEM_SIZE)) {
        return le16_to_cpu(*(uint16_t *)(s->mem + addr));
    } else {
        return 0xffff;
    }
}

static inline uint32_t dp83815_mem_readl(DP83815State *s, uint32_t addr)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= DP83815_PMEM_START && addr < DP83815_MEM_SIZE)) {
        return le32_to_cpupu((uint32_t *)(s->mem + addr));
    } else {
        return 0xffffffff;
    }
}

static inline void dp83815_dma_update(DP83815State *s, int len)
{
    s->rsar += len;
    /* wrap */
    /* XXX: check what to do if rsar > stop */
    if (s->rsar == s->stop)
        s->rsar = s->start;

    if (s->rcnt <= len) {
        s->rcnt = 0;
        /* signal end of transfert */
        s->isr |= ENISR_RDC;
        dp83815_update_irq(s);
    } else {
        s->rcnt -= len;
    }
}

static void dp83815_reset_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    /* nothing to do (end of reset pulse) */
}

static uint32_t dp83815_reset_ioport_read(void *opaque, uint32_t addr)
{
    DP83815State *s = opaque;
    dp83815_reset(s);
    return 0;
}

/***********************************************************/
/* PCI DP83815 definitions */

typedef struct PCIDP83815State {
    PCIDevice dev;
    DP83815State dp83815;
} PCIDP83815State;

static void dp83815_map(PCIDevice *pci_dev, int region_num, 
                       uint32_t addr, uint32_t size, int type)
{
    PCIDP83815State *d = (PCIDP83815State *)pci_dev;
    DP83815State *s = &d->dp83815;

#if defined(DEBUG_DP83815)
    printf("%s, region %d, size 0x%08x\n", __func__, region_num, size);
#endif

    register_ioport_write(addr, size, 1, dp83815_ioport_write, s);
    register_ioport_read(addr, size, 1, dp83815_ioport_read, s);
    register_ioport_write(addr, size, 2, dp83815_ioport_write, s);
    register_ioport_read(addr, size, 2, dp83815_ioport_read, s);
    register_ioport_write(addr, size, 4, dp83815_ioport_write, s);
    register_ioport_read(addr, size, 4, dp83815_ioport_read, s);

#define OP_REG(offset, value) (*(uint32_t *)(s->mem + (offset)) = cpu_to_le32(value))
    OP_REG(0x00, 0x00000000);   /* Command */
    // EEPROM Bits 16, 15-13!!!
    OP_REG(0x04, 0x00000000);   /* Configuration and Media Status */
    OP_REG(0x08, 0x00000002);   /* EEPROM Access */
    OP_REG(0x10, 0x03008000);   /* ISR, Interrupt Status */
#if defined(DP83815)
    OP_REG(0x24, 0x00000102);   /* Transmit Configuration */
#else
    OP_REG(0x24, 0x00040102);   /* Transmit Configuration */
#endif
    OP_REG(0x34, 0x00000002);   /* Receive Configuration */
    OP_REG(0x50, 0xffffffff);   /* Boot ROM Address */
#if defined(DP83815)
    OP_REG(0x58, 0x00000302);   /* SRR, Silicon Revision */
#else
    /* DP83816AVNG */
    OP_REG(0x58, 0x00000505);   /* SRR, Silikon Revision */
#endif
    OP_REG(0x5c, 0x00000002);   /* Management Information Base Control */
    OP_REG(0x00, 0x00000000);
    OP_REG(0x00, 0x00000000);
    OP_REG(0x00, 0x00000000);
#define PHY_REG(offset, value) (*(uint16_t *)(s->mem + (offset)) = cpu_to_le16(value))
    PHY_REG(0x80, 0x0000);      /* TODO */
    PHY_REG(0x84, 0x7849);
    PHY_REG(0x88, 0x2000);
    PHY_REG(0x8c, 0x5c21);
    PHY_REG(0x90, 0x05e1);
    PHY_REG(0x98, 0x0004);
    PHY_REG(0x9c, 0x2001);
    PHY_REG(0xd8, 0x0100);
    PHY_REG(0xe4, 0x003f);
#if defined(DP83815)
    PHY_REG(0xe8, 0x0004);
#else
    PHY_REG(0xe8, 0x0804);
#endif

    //~ register_ioport_write(addr, 16, 1, dp83815_ioport_write, s);
    //~ register_ioport_read(addr, 16, 1, dp83815_ioport_read, s);

    //~ register_ioport_write(addr + 0x1f, 1, 1, dp83815_reset_ioport_write, s);
    //~ register_ioport_read(addr + 0x1f, 1, 1, dp83815_reset_ioport_read, s);
}

static void dp83815_mmio_map(PCIDevice *pci_dev, int region_num, 
                            uint32_t addr, uint32_t size, int type)
{
    PCIDP83815State *d = (PCIDP83815State *)pci_dev;

#if defined(DEBUG_DP83815)
    printf("%s region %d, addr=0x%08x 0x%08x\n", __func__, region_num, addr, size);
#endif

    cpu_register_physical_memory(addr, DP83815_MEM_SIZE, d->dp83815.io_memory);
}



typedef enum {
  /* MAC/BIU Registers */
  DP8315_CR = 0x00,
  DP8315_CFG = 0x04,
  DP8315_MEAR = 0x08,
  DP8315_PTSCR = 0x0c,
  DP8315_ISR = 0x10,
  DP8315_IMR = 0x14,
  DP8315_IER = 0x18,
  DP8315_IHR = 0x1c,
  DP8315_TXDP = 0x20,
  DP8315_TXCFG = 0x24,
  //~ DP8315_R = 0x28,
  //~ DP8315_R = 0x2c,
  DP8315_RXDP = 0x30,
  DP8315_RXCFG = 0x34,
  //~ DP8315_R = 0x38,
  DP8315_CCSR = 0x3c,
  DP8315_WCSR = 0x40,
  DP8315_PCR = 0x44,
  DP8315_RFCR = 0x48,
  DP8315_RFDR = 0x4c,
  DP8315_BRAR = 0x50,
  DP8315_BRDR = 0x54,
  DP8315_SRR = 0x58,
  DP8315_MIBC = 0x5c,
  DP8315_MIB0 = 0x60,
  DP8315_MIB1 = 0x64,
  DP8315_MIB2 = 0x68,
  DP8315_MIB3 = 0x6c,
  DP8315_MIB4 = 0x70,
  DP8315_MIB5 = 0x74,
  DP8315_MIB6 = 0x78,
  /* Internal Phy Registers */
  BMCR = 0x80,
} dp8315_register_t;

typedef enum {
  /* DP8315_CR */
  DP8315_RST = 0x100,
  /* PTSCR */
  EELOAD_EN = 1 << 2,
} dp83815_bit_t;




static void dp83815_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIDP83815State *d = opaque;
#if defined(DEBUG_DP83815)
    printf("%s addr=0x%08x val=0x%02x\n", __func__, addr, val);
#endif
        //~ dp83815_aprom_writeb(d, addr & 0x0f, val);
}

static uint32_t dp83815_mmio_readb(void *opaque, target_phys_addr_t addr) 
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    uint32_t val = -1;
    if (0) {
    } else if (1) {
      val = s->mem[offset];
    }
    //~ if (!(addr & 0x10))
        //~ val = dp83815_aprom_readb(d, addr & 0x0f);
#if defined(DEBUG_DP83815)
    printf("%s addr=0x%08x val=0x%02x\n", __func__, addr, val & 0xff);
#endif
    return val;
}

static void dp83815_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    if ((offset & 1) != 0) {
      /* address not on word boundary */
      printf("%s ??? addr=0x%08x val=0x%08x\n", __func__, addr, val);
    } else if (1) {
#if defined(DEBUG_DP83815)
      printf("%s addr=0x%08x val=0x%04x\n", __func__, addr, val);
#endif
      *(uint16_t *)&s->mem[offset] = val;
    }
    //~ if (addr & 0x10)
        //~ dp83815_ioport_writew(d, addr & 0x0f, val);
    //~ else {
        //~ addr &= 0x0f;
        //~ dp83815_aprom_writeb(d, addr, val & 0xff);
        //~ dp83815_aprom_writeb(d, addr+1, (val & 0xff00) >> 8);
    //~ }
}

static uint32_t dp83815_mmio_readw(void *opaque, target_phys_addr_t addr) 
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    uint32_t val = -1;
    if (0) {
    } else if ((offset & 1) == 0) {
      val = *(uint16_t *)&s->mem[offset];
    }
    //~ if (addr & 0x10)
        //~ val = dp83815_ioport_readw(d, addr & 0x0f);
    //~ else {
        //~ addr &= 0x0f;
        //~ val = dp83815_aprom_readb(d, addr+1);
        //~ val <<= 8;
        //~ val |= dp83815_aprom_readb(d, addr);
    //~ }
#if defined(DEBUG_DP83815)
    printf("%s addr=0x%08x val = 0x%04x\n", __func__, addr, val & 0xffff);
#endif
    return val;
}

static void dp83815_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
#if defined(DEBUG_DP83815)
    //~ printf("%s addr=0x%08x val=0x%08x\n", __func__, addr, val);
#endif
    if ((offset & 3) != 0) {
      /* address not on word boundary */
      printf("%s ??? addr=0x%08x val=0x%08x\n", __func__, addr, val);
    } else if (offset == DP8315_CR) {
      printf("%s addr=CR val=0x%08x\n", __func__, val);
      if (val & DP8315_RST) {
        val ^= DP8315_RST;
      }
      *(uint32_t *)&s->mem[offset] = val;
    } else if (offset == DP8315_MEAR) {
#if defined(CONFIG_EEPROM)
      eeprom_action(&s->eeprom_state, val);
#else
      printf("%s addr=MEAR val=0x%08x\n", __func__, val);
#endif
    } else if (offset == DP8315_PTSCR) {
      printf("%s addr=0x%08x val=0x%08x\n", __func__, addr, val);
      if (val & EELOAD_EN) {
        val ^= EELOAD_EN;
      }
      *(uint32_t *)&s->mem[offset] = val;
    } else if (offset == DP8315_RFCR) {
      printf("%s addr=0x%08x val=0x%08x\n", __func__, addr, val);
      *(uint32_t *)&s->mem[offset] = val;
    } else if (offset == DP8315_RFDR) {
      printf("%s addr=0x%08x val=0x%08x\n", __func__, addr, val);
      *(uint32_t *)&s->mem[offset] = val;
    } else {
      *(uint32_t *)&s->mem[offset] = val;
#if defined(DEBUG_DP83815)
      printf("%s addr=0x%08x val=0x%08x\n", __func__, addr, val);
#endif
    }
    //~ if (addr & 0x10)
        //~ dp83815_ioport_writel(d, addr & 0x0f, val);
    //~ else {
        //~ addr &= 0x0f;
        //~ dp83815_aprom_writeb(d, addr, val & 0xff);
        //~ dp83815_aprom_writeb(d, addr+1, (val & 0xff00) >> 8);
        //~ dp83815_aprom_writeb(d, addr+2, (val & 0xff0000) >> 16);
        //~ dp83815_aprom_writeb(d, addr+3, (val & 0xff000000) >> 24);
    //~ }
}

static uint32_t dp83815_mmio_readl(void *opaque, target_phys_addr_t addr) 
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    uint32_t val = 0xffffffffU;
    if (offset == DP8315_MEAR) {
#if defined(CONFIG_EEPROM)
      val = eeprom_action(&s->eeprom_state, -1);
#else
# error "missing"
#endif
    } else if ((offset & 3) == 0) {
      val = *(uint32_t *)&s->mem[offset];
#if defined(DEBUG_DP83815)
      printf("%s addr=0x%08x val=0x%08x\n", __func__, addr, val);
#endif
    } else {
      printf("%s ??? addr=0x%08x val=0x%08x\n", __func__, addr, val);
    }
    //~ if (addr & 0x10)
        //~ val = dp83815_ioport_readl(d, addr & 0x0f);
    //~ else {
        //~ addr &= 0x0f;
        //~ val = dp83815_aprom_readb(d, addr+3);
        //~ val <<= 8;
        //~ val |= dp83815_aprom_readb(d, addr+2);
        //~ val <<= 8;
        //~ val |= dp83815_aprom_readb(d, addr+1);
        //~ val <<= 8;
        //~ val |= dp83815_aprom_readb(d, addr);
    //~ }
    return val;
}







static CPUWriteMemoryFunc *dp83815_mmio_write[] = {
    dp83815_mmio_writeb,
    dp83815_mmio_writew,
    dp83815_mmio_writel
};

static CPUReadMemoryFunc *dp83815_mmio_read[] = {
    dp83815_mmio_readb,
    dp83815_mmio_readw,
    dp83815_mmio_readl
};

int dp8381x_load(QEMUFile *f, void *opaque, int version_id)
{
    PCIDP83815State *d = (PCIDP83815State *)opaque;
    DP83815State *s = &d->dp83815;
    int result = 0;
    if (version_id == dp8381x_version) {
        generic_pci_load(f, &d->dev, 1);
        eeprom_load(f, &s->eeprom_state, eeprom_version);
        /* TODO: support different endianess */
        //~ qemu_get_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
    } else {
        result = -EINVAL;
    }
    return result;
}

static void dp8381x_save(QEMUFile *f, void *opaque)
{
    PCIDP83815State *d = (PCIDP83815State *)opaque;
    DP83815State *s = &d->dp83815;
    generic_pci_save(f, &d->dev);
    eeprom_save(f, &s->eeprom_state);
    /* TODO: support different endianess */
    qemu_put_buffer(f, (uint8_t *)d, sizeof(*d));
}

void pci_dp83815_init(PCIBus *bus, NICInfo *nd)
{
    PCIDP83815State *d;
    DP83815State *s;
    uint8_t *pci_conf;

    uint32_t silicon_revision = DP83816AVNG;

#if defined(DEBUG_DP83815)
    printf("%s, silicon revision = 0x%08x\n", __func__, silicon_revision);
#endif

    d = (PCIDP83815State *)pci_register_device(bus, "DP83815",
                                               sizeof(PCIDP83815State),
                                               -1, NULL, NULL);
    pci_conf = d->dev.config;
#define PCI_CONF(offset, value) (*(uint32_t *)(pci_conf + (offset)) = cpu_to_le32(value))
    PCI_CONF(0x00, 0x0020100b); // National Semiconductor DP 83815
    // EEPROM Bit 20 NCPEN!!!
    PCI_CONF(0x04, 0x02900000); /* Configuration Command and Status */
    PCI_CONF(0x08, 0x02000000); // ethernet network controller
    PCI_CONF(0x0c, 0x00000000); // header_type
    PCI_CONF(0x10, 0x00000001); // IOIND, IOSIZE
    PCI_CONF(0x14, 0x00000000);
    /* 0x18...0x28 reserved, returns 0 */
    // EEPROM!!!
    PCI_CONF(0x2c, 0x00000000); /* Configuration Subsystem Identification */
    PCI_CONF(0x30, 0x00000000); /* Boot ROM Configuration */
    PCI_CONF(0x34, 0x00000040); /* Capabilities Pointer, CLOFS */
    /* 0x38 reserved, returns 0 */
    // EEPROM Bits 16...31!!!
    PCI_CONF(0x3c, 0x340b0100); // MNGNT = 11, MXLAT = 52, IPIN = 0
    // EEPROM Bits 31...27, 21!!!
    PCI_CONF(0x40, 0xff820001); /* Power Management Capabilities */
    // EEPROM Bit 8!!!
    PCI_CONF(0x44, 0x00000000); /* Power Management Control and Status */
    /* 0x48...0xff reserved, returns 0 */

    s = &d->dp83815;
    s->silicon_revision = silicon_revision;

    /* Handler for memory-mapped I/O */
    s->io_memory =
      cpu_register_io_memory(0, dp83815_mmio_read, dp83815_mmio_write, d);

    printf("%s: io_memory = 0x%08x\n", __func__, s->io_memory);

    pci_register_io_region(&d->dev, 0, DP83815_IO_SIZE, 
                           PCI_ADDRESS_SPACE_IO, dp83815_map);
    pci_register_io_region(&d->dev, 1, DP83815_MEM_SIZE, 
                           PCI_ADDRESS_SPACE_MEM, dp83815_mmio_map);

    s->irq = 16; // PCI interrupt
    s->pci_dev = (PCIDevice *)d;
    memcpy(s->macaddr, nd->macaddr, 6);
    dp83815_reset(s);
    s->vc = qemu_new_vlan_client(nd->vlan, dp83815_receive,
                                 dp83815_can_receive, s);

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "dp83815 pci macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             s->macaddr[0],
             s->macaddr[1],
             s->macaddr[2],
             s->macaddr[3],
             s->macaddr[4],
             s->macaddr[5]);
             
    register_savevm("dp8381x", dp8381x_instance, dp8381x_version,
                    dp8381x_save, dp8381x_load, d);
}

#if 0



dp83815_map, region 0, size 0x00000100
dp83815_mmio_map addr=0xf2001000 0x00001000
dp83815_mmio_writel addr=0xf2001018 val=0x00000000
dp83815_mmio_readl addr=0xf2001058 val=0x00000505
eeprom_action: not selected, count = 0, state 0x0000 => 0x0000
eeprom_action: not selected, count = 0, state 0x0000 => 0x0004
eeprom_action: selected, state 0x0004 => 0x0009
eeprom_action: count = 10, command = 0x00, address = 0x0c, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 10, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 11, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 12, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 13, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 14, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 15, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 16, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 17, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 18, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 19, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 20, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 21, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 22, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 23, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 24, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action: not selected, count = 25, state 0x000c => 0x0000
eeprom_action: not selected, count = 0, state 0x0000 => 0x0004
eeprom_action: selected, state 0x0004 => 0x0009
eeprom_action: count = 10, command = 0x00, address = 0x0e, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 10, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 11, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 12, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 13, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 14, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 15, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 16, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 17, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 18, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 19, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 20, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 21, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 22, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 23, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0000, count = 24, data = 0x0000
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action: not selected, count = 25, state 0x000c => 0x0000
eeprom_action: not selected, count = 0, state 0x0000 => 0x0004
eeprom_action: selected, state 0x0004 => 0x0009
eeprom_action: count = 10, command = 0x10, address = 0x00, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 10, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 11, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 12, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 13, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 14, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 15, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 16, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 17, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 18, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 19, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 20, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 21, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 22, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 23, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 24, data = 0xd008
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action: not selected, count = 25, state 0x000c => 0x0000
eeprom_action: not selected, count = 0, state 0x0000 => 0x0004
eeprom_action: selected, state 0x0004 => 0x0009
eeprom_action: count = 10, command = 0x10, address = 0x02, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 10, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 11, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 12, data = 0x2cd0
dp83815_mmio_redp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 18, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 19, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 20, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 21, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 22, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 23, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
eeprom_action:   command = 0x0010, count = 24, data = 0x2cd0
dp83815_mmio_readb addr=0xf2001008 val=0x02
dp83815_mmio_readl addr=0xf20010c0 val=0x00000000
dp83815_mmio_readl addr=0xf2001080 val=0x00000000
dp83815_mmio_readl addr=0xf20010c0 val=0x00000000
dp83815_mmio_writel addr=0xf2001004 val=0x0007e400
dp83815_mmio_writel addr=0xf2001004 val=0x0007e000
dp83815_mmio_readl addr=0xf2001080 val=0x00000000
dp83815_mmio_writel addr=0xf20010cc val=0x00000001
dp83815_mmio_writel addr=0xf20010ec val=0x000080b7
dp83815_mmio_writel addr=0xf20010cc val=0x00000000
dp83815_mmio_writel addr=0xf20010cc val=0x00000001
dp83815_mmio_writel addr=0xf20010e4 val=0x0000189c
dp83815_mmio_writel addr=0xf20010cc val=0x00000000
dp83815_mmio_writel addr=CR val=0x00000100
dp83815_mmio_readl addr=0xf2001000 val=0x00000000
dp83815_mmio_writel addr=0xf2001004 val=0x0007e000
dp83815_mmio_writel addr=0xf200103c val=0x00008000
dp83815_mmio_writel addr=0xf2001040 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000000
dp83815_mmio_writel addr=0xf200104c val=0x0000ffff
dp83815_mmio_writel addr=0xf2001048 val=0x00000002
dp83815_mmio_writel addr=0xf200104c val=0x0000ffff
dp83815_mmio_writel addr=0xf2001048 val=0x00000004
dp83815_mmio_writel addr=0xf200104c val=0x0000ffff
dp83815_mmio_readl addr=0xf20010c0 val=0x00000000
dp83815_mmio_writel addr=0xf2001024 val=0x10740930
dp83815_mmio_writel addr=0xf2001044 val=0x00000000
dp83815_mmio_writel addr=0xf2001034 val=0x00700020
dp83815_mmio_writel addr=0xf2001020 val=0x01062000
dp83815_mmio_writel addr=0xf2001030 val=0x01063000
dp83815_mmio_writel addr=0xf2001048 val=0x00000200
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000202
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000204
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000206
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000208
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000210
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000212
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000214
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000216
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000218
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000220
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000222
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000224
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000226
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000228
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000230
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000232
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000234
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000236
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000238
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x80000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x80000000
dp83815_mmio_writel addr=0xf200105c val=0x00000004
dp83815_mmio_writel addr=CR val=0x00000004
dp83815_mmio_writel addr=0xf20010c8 val=0xffffbfff
dp83815_mmio_writel addr=0xf20010c4 val=0x00000002
dp83815_mmio_writel addr=0xf2001014 val=0x00004955
dp83815_mmio_writel addr=0xf2001018 val=0x00000001
dp83815_mmio_readl addr=0xf20010c0 val=0x00000000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0xc8200000
dp83815_mmio_writel addr=0xf2001048 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x80000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0xc8200000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000200
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000202
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000204
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000206
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000208
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020e
dp83815_mmio_writel addr=0xf200104c val=0x00000010
dp83815_mmio_writel addr=0xf2001048 val=0x00000210
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000212
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000214
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000216
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000218
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000220
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000222
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000224
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000226
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000228
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000230
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000232
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000234
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000236
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000238
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0xc8200000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000200
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000202
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000204
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000206
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000208
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000020e
dp83815_mmio_writel addr=0xf200104c val=0x00000010
dp83815_mmio_writel addr=0xf2001048 val=0x00000210
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000212
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000214
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000216
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000218
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000021e
dp83815_mmio_writel addr=0xf200104c val=0x00008000
dp83815_mmio_writel addr=0xf2001048 val=0x00000220
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000222
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000224
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000226
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000228
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000022e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000230
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000232
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000234
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000236
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x00000238
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023a
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023c
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0x0000023e
dp83815_mmio_writel addr=0xf200104c val=0x00000000
dp83815_mmio_writel addr=0xf2001048 val=0xc8200000
dp83815_mmio_readl addr=0xf2001060 val=0x00000000
#endif
