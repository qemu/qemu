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

#define logout(fmt, args...) printf("DP8381X %-24s" fmt, __func__, ##args)

/* Silicon revisions for the different hardware */
#define DP83815CVNG     0x00000302
#define DP83815DVNG     0x00000403
#define DP83816AVNG     0x00000505

#define SILICON_REVISION DP83816AVNG

#define PCI_INTERRUPT   16

#if SILICON_REVISION != DP83816AVNG
# define DP83815
# warning("DP83815")
#endif

/* debug DP83815 card */
#define DEBUG_DP83815

#define MAX_ETH_FRAME_SIZE 1514

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

static int eeprom_load(QEMUFile *f, void *opaque, int version_id)
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
        logout("read data = 0x%04x, address = %u, bit = %d, state 0x%04x\n",
          ee->data, address, 26 - *count, state);
    }
    bits = state;
  } else if (bits & EESEL) {
    /* EEPROM is selected */
    if (!(state & EESEL)) {
      logout("selected, state 0x%04x => 0x%04x\n", state, bits);
    } else if (!(state & EECLK) && (bits & EECLK)) {
      /* Raising edge of clock. */
      //~ logout("raising clock, state 0x%04x => 0x%04x\n", state, bits);
      if (*count < 10) {
        ee->data = (ee->data << 1);
        if (bits & EEDI) {
          ee->data++;
        } else if (*count == 1) {
          *count = 0;
        }
        //~ logout("   count = %d, data = 0x%04x\n", *count, data);
        *count++;
        if (*count == 10) {
          ee->address = address = (ee->data & eeprom_amask);
          ee->command = command = (ee->data & eeprom_imask);
          ee->data = eeprom_map[address];
          logout("count = %d, command = 0x%02x, address = 0x%02x, data = 0x%04x\n",
            *count, command, address, ee->data);
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
          logout("   command = 0x%04x, count = %d, data = 0x%04x\n",
            command, *count, ee->data);
        }
        *count++;
      } else {
        logout("??? state 0x%04x => 0x%04x\n", state, bits);
      }
    } else {
      //~ logout("state 0x%04x => 0x%04x\n", state, bits);
    }
  } else {
    logout("not selected, count = %u, state 0x%04x => 0x%04x\n", *count, state, bits);
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
#if defined(DEBUG_DP83815)
    logout("???\n");
#endif

#if 0
    s->isr = ENISR_RESET;
    memcpy(s->mem, s->macaddr, 6);
    s->mem[14] = 0x57;
    s->mem[15] = 0x57;

    /* duplicate prom data */
    for(i = 15;i >= 0; i--) {
        s->mem[2 * i] = s->mem[i];
        s->mem[2 * i + 1] = s->mem[i];
    }
#endif
}

static void dp83815_update_irq(DP83815State *s)
{
    int isr;
    isr = (s->isr & s->imr) & 0x7f;
#if defined(DEBUG_DP83815)
    logout("set IRQ line %d to %d (%02x %02x)\n",
           s->irq, isr ? 1 : 0, s->isr, s->imr);
#endif
    if (s->irq == PCI_INTERRUPT) {
        pci_set_irq(s->pci_dev, 0, (isr != 0));
    }
}

#define POLYNOMIAL 0x04c11db6

#if 0
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
#endif

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
    
#if defined(DEBUG_DP83815)
    logout("???\n");
#endif

    return !dp83815_buffer_full(s);
}

#define MIN_BUF_SIZE 60

static void dp83815_receive(void *opaque, const uint8_t *buf, int size)
{
#if 0
    DP83815State *s = opaque;
    uint8_t *p;
    int total_len, next, avail, len, index, mcast_idx;
    uint8_t buf1[60];
    static const uint8_t broadcast_macaddr[6] = 
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif

#if defined(DEBUG_DP83815)
    logout("received len=%d\n", size);
#endif

#if 0
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
#endif
}

static void dp83815_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
#if 0
    DP83815State *s = opaque;
    int offset, page, index;
    addr &= 0xf;
#endif
#ifdef DEBUG_DP83815
    logout("io write addr=0x%x val=0x%02x\n", addr, val);
#endif
}

static uint32_t dp83815_ioport_read(void *opaque, uint32_t addr)
{
    int ret = 0;
#ifdef DEBUG_DP83815
    logout("io read addr=0x%x val=%02x\n", addr, ret);
#endif
    return ret;
}

#if 0
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
#endif

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
    logout("region %d, size 0x%08x\n", region_num, size);
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
    logout("region %d, addr=0x%08x 0x%08x\n", region_num, addr, size);
#endif

    cpu_register_physical_memory(addr, DP83815_MEM_SIZE, d->dp83815.io_memory);
}



typedef enum {
  /* MAC/BIU Registers */
  DP83815_CR = 0x00,
  DP83815_CFG = 0x04,
  DP83815_MEAR = 0x08,
  DP83815_PTSCR = 0x0c,
  DP83815_ISR = 0x10,
  DP83815_IMR = 0x14,
  DP83815_IER = 0x18,
  DP83815_IHR = 0x1c,
  DP83815_TXDP = 0x20,
  DP83815_TXCFG = 0x24,
  //~ DP83815_R = 0x28,
  //~ DP83815_R = 0x2c,
  DP83815_RXDP = 0x30,
  DP83815_RXCFG = 0x34,
  //~ DP83815_R = 0x38,
  DP83815_CCSR = 0x3c,
  DP83815_WCSR = 0x40,
  DP83815_PCR = 0x44,
  DP83815_RFCR = 0x48,
  DP83815_RFDR = 0x4c,
  DP83815_BRAR = 0x50,
  DP83815_BRDR = 0x54,
  DP83815_SRR = 0x58,
  DP83815_MIBC = 0x5c,
  DP83815_MIB0 = 0x60,
  DP83815_MIB1 = 0x64,
  DP83815_MIB2 = 0x68,
  DP83815_MIB3 = 0x6c,
  DP83815_MIB4 = 0x70,
  DP83815_MIB5 = 0x74,
  DP83815_MIB6 = 0x78,
  /* Internal Phy Registers */
  DP83815_BMCR = 0x80,          /* Control Register */
  DP83815_BMSR = 0x84,          /* Status Register */
  DP83815_PHYIDR1 = 0x88,       /* PHY Identification Register 1 */
  DP83815_PHYIDR2 = 0x8c,       /* PHY Identification Register 2 */
  DP83815_ANAR = 0x90,          /* Auto-Negotiation Advertisment Register */
  DP83815_ANLPAR = 0x94,        /* Auto-Negotiation Link Partner Ability Register */
  DP83815_ANER = 0x98,          /* Auto-Negotiation Expansion Register */
  DP83815_ANPTR = 0x9c,
  DP83815_PHYSTS = 0xc0,
  DP83815_MICR = 0xc4,
  DP83815_MISR = 0xc8,
  DP83815_FCSCR = 0xd0,
  DP83815_RECR = 0xd4,
  DP83815_PCSR = 0xd8,
  DP83815_0xdc,
  DP83815_PHYCR = 0xe4,
  DP83815_TBTSCR = 0xe8,
  DP83815_0xf4,
  DP83815_0xf8,
  DP83815_0xfc,
} DP83815_register_t;

typedef enum {
  /* DP83815_CR */
  DP83815_RST = 0x100,
  /* DP83815_CFG */
  DP83815_LNKSTS = 1 << 31,
  DP83815_SPEED100 = 1 << 30,
  DP83815_FDUP = 1 << 29,
  DP83815_ANEG_DN = 1 << 27,
  /* PTSCR */
  EELOAD_EN = 1 << 2,
} dp83815_bit_t;

static const char *regnames[] = {
    /* MAC/BIU Registers */
    "CR",       /* 0x00 */
    "CFG",      /* 0x04 */
    "MEAR",     /* 0x08 */
    "PTSCR",    /* 0x0c */
    "ISR",      /* 0x10 */
    "IMR",      /* 0x14 */
    "IER",      /* 0x18 */
    "IHR",      /* 0x1c */
    "TXDP",     /* 0x20 */
    "TXCFG",    /* 0x24 */
    "0x28",     /* 0x28 */
    "0x2c",     /* 0x2c */
    "RXDP",     /* 0x30 */
    "RXCFG",    /* 0x34 */
    "0x38",     /* 0x38 */
    "CCSR",     /* 0x3c */
    "WCSR",     /* 0x40 */
    "PCR",      /* 0x44 */
    "RFCR",     /* 0x48 */
    "RFDR",     /* 0x4c */
    "BRAR",     /* 0x50 */
    "BRDR",     /* 0x54 */
    "SRR",      /* 0x58 */
    "MIBC",     /* 0x5c */
    "MIB0",     /* 0x60 */
    "MIB1",     /* 0x64 */
    "MIB2",     /* 0x68 */
    "MIB3",     /* 0x6c */
    "MIB4",     /* 0x70 */
    "MIB5",     /* 0x74 */
    "MIB6",     /* 0x78 */
    "0x7c",     /* 0x7c */
    /* Internal Phy Registers */
    "BMCR",     /* 0x80 */
    "BMSR",     /* 0x84 */
    "PHYIDR1",  /* 0x88 */
    "PHYIDR2",  /* 0x8c */
    "ANAR",     /* 0x90 */
    "ANLPAR",   /* 0x94 */
    "ANER",     /* 0x98 */
    "ANNPTR",   /* 0x9c */
    "0xa0",     /* 0xa0 */
    "0xa4",     /* 0xa4 */
    "0xa8",     /* 0xa8 */
    "0xac",     /* 0xac */
    "PHYSTS",   /* 0xc0 */
    "MICR",     /* 0xc4 */
    "MISR",     /* 0xc8 */
    "0xcc",     /* 0xcc */
    "FCSCR",    /* 0xd0 */
    "RECR",     /* 0xd4 */
    "PCSR",     /* 0xd8 */
    "0xdc",     /* 0xdc */
    "0xe0",     /* 0xe0 */
    "PHYCR",    /* 0xe4 */
    "TBTSCR",   /* 0xe8 */
};

#define num_elements(s) (sizeof(s) / sizeof(*s))

static const char *dp83815_regname(target_phys_addr_t addr)
{
  static char name[10];
  uint16_t offset = (addr & 0xfff);
  const char *p = name;
  if (offset < (num_elements(regnames) * 4) && (offset & 3) == 0) {
    p = regnames[offset / 4];
  } else {
    snprintf(name, sizeof(name), "0x%04x", offset);
  }
  return p;
}

static void dp83815_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    //~ PCIDP83815State *d = opaque;
#if defined(DEBUG_DP83815)
    logout("??? addr=%s val=0x%02x\n", dp83815_regname(addr), val);
#endif
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
#if defined(DEBUG_DP83815)
    logout("addr=%s val=0x%02x\n", dp83815_regname(addr), (uint8_t)val);
#endif
    return val;
}

static void dp83815_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    if ((offset & 1) != 0) {
      logout("error, address not on word boundary, addr=%s val=0x%08x\n", dp83815_regname(addr), val);
    } else if (1) {
#if defined(DEBUG_DP83815)
      logout("addr=%s val=0x%04x\n", dp83815_regname(addr), val);
#endif
      *(uint16_t *)&s->mem[offset] = val;
    }
}

static uint32_t dp83815_mmio_readw(void *opaque, target_phys_addr_t addr) 
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    uint32_t val = -1;
    if ((offset & 1) != 0) {
      logout("error, address not on word boundary, addr=%s\n", dp83815_regname(addr));
    } else if (offset == DP83815_ANAR) {        /* 0x90 */
      /* TODO: ??? */
      val = *(uint16_t *)&s->mem[offset];
    } else {
      val = *(uint16_t *)&s->mem[offset];
#if defined(DEBUG_DP83815)
      logout("addr=%s val=0x%04x\n", dp83815_regname(addr), (uint16_t)val);
#endif
    }
    return val;
}

static void dp83815_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    int logging = 1;
    if ((offset & 3) != 0) {
      logout("error, address not on double word boundary, addr=%s val=0x%08x\n",
        dp83815_regname(addr), val);
      logging = 0;
    } else if (offset == DP83815_CR) {          /* 0x00 */
      if (val & DP83815_RST) {
        val ^= DP83815_RST;
      }
      *(uint32_t *)&s->mem[offset] = val;
    } else if (offset == DP83815_MEAR) {        /* 0x08 */
#if defined(CONFIG_EEPROM)
      eeprom_action(&s->eeprom_state, val);
      logging = 0;
#endif
    } else if (offset == DP83815_PTSCR) {
      if (val & EELOAD_EN) {
        val ^= EELOAD_EN;
      }
      *(uint32_t *)&s->mem[offset] = val;
    } else if (offset == DP83815_RFCR) {        /* 0x48 */
      /* TODO: enable packet filters */
      *(uint32_t *)&s->mem[offset] = val;
    } else if (offset == DP83815_RFDR) {        /* 0x4c */
      *(uint32_t *)&s->mem[offset] = val;
    } else {
      *(uint32_t *)&s->mem[offset] = val;
    }
#if defined(DEBUG_DP83815)
    if (logging)
      logout("addr=%s val=0x%08x\n", dp83815_regname(addr), val);
#endif
}

static uint32_t dp83815_mmio_readl(void *opaque, target_phys_addr_t addr) 
{
    PCIDP83815State *d = opaque;
    DP83815State *s = &d->dp83815;
    uint8_t offset = (addr & 0xff);
    uint32_t val = 0xffffffffU;
    int logging = 1;
    if ((offset & 3) != 0) {
      logout("error, address not on double word boundary, addr=%s\n",
        dp83815_regname(addr));
      logging = 0;
    } else if (offset == DP83815_CFG) {         /* 0x04 */
      val = (*(uint32_t *)&s->mem[offset] | DP83815_LNKSTS | DP83815_SPEED100 | DP83815_FDUP | DP83815_ANEG_DN);
      logging = 0;
    } else if (offset == DP83815_MEAR) {        /* 0x08 */
#if defined(CONFIG_EEPROM)
      val = eeprom_action(&s->eeprom_state, -1);
      logging = 0;
#else
# error "missing"
#endif
    } else if (offset == DP83815_ISR) {         /* 0x10 */
      /* TODO: reset interrupt bits after read */
      val = *(uint32_t *)&s->mem[offset];
    } else if (offset == DP83815_BMCR) {        /* 0x80 */
      /* TODO: ??? */
      val = *(uint32_t *)&s->mem[offset];
    } else if (offset == DP83815_PTSCR) {
      /* TODO: ??? */
      val = *(uint32_t *)&s->mem[offset];
      logging = 0;
    } else if (offset == DP83815_WCSR) {        /* 0x40 */
      /* TODO: set bits on arp, unicast, wake-on-lan and other packets */
      val = *(uint32_t *)&s->mem[offset];
      logging = 0;
    } else if (offset == DP83815_RFCR) {        /* 0x48 */
      val = *(uint32_t *)&s->mem[offset];
      logging = 0;
    } else if (offset == DP83815_RFDR) {        /* 0x4c */
      val = *(uint32_t *)&s->mem[offset];
    } else if (offset == DP83815_SRR) {         /* 0x58 */
      /* TODO: ??? */
      val = *(uint32_t *)&s->mem[offset];
    } else {
      val = *(uint32_t *)&s->mem[offset];
    }
#if defined(DEBUG_DP83815)
    if (logging) {
      logout("addr=%s val=0x%08x\n", dp83815_regname(addr), val);
    }
#endif
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
        result = pci_device_load(&d->dev, f);
        eeprom_load(f, &s->eeprom_state, eeprom_version);
        /* TODO: support different endianess */
        //~ qemu_get_buffer(f, (uint8_t *)eeprom, sizeof(*eeprom));
    } else {
        result = -EINVAL;
    }
    return result;
}

static void nic_reset(void *opaque)
{
    PCIDP83815State *d = (PCIDP83815State *)opaque;
#if defined(DEBUG_DP83815)
    logout("%p\n", d);
#endif
}

static void dp8381x_save(QEMUFile *f, void *opaque)
{
    PCIDP83815State *d = (PCIDP83815State *)opaque;
    DP83815State *s = &d->dp83815;
    pci_device_save(&d->dev, f);
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
    logout("silicon revision = 0x%08x\n", silicon_revision);
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

    logout("io_memory = 0x%08x\n", s->io_memory);

    pci_register_io_region(&d->dev, 0, DP83815_IO_SIZE, 
                           PCI_ADDRESS_SPACE_IO, dp83815_map);
    pci_register_io_region(&d->dev, 1, DP83815_MEM_SIZE, 
                           PCI_ADDRESS_SPACE_MEM, dp83815_mmio_map);

    s->irq = PCI_INTERRUPT;
    s->pci_dev = &d->dev;
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
             
    qemu_register_reset(nic_reset, d);

    register_savevm("dp8381x", dp8381x_instance, dp8381x_version,
                    dp8381x_save, dp8381x_load, d);
}

#if 0
dp83815_map             region 0, size 0x00000100
dp83815_mmio_map        region 1, addr=0xf2001000 0x00001000
eeprom_action           selected, state 0x0000 => 0x0008
eeprom_action           not selected, count = 0, state 0x0008 => 0x0000
eeprom_action           selected, state 0x0000 => 0x0008
eeprom_action           not selected, count = 0, state 0x0008 => 0x0000
eeprom_action           selected, state 0x0000 => 0x0008
eeprom_action           not selected, count = 0, state 0x0008 => 0x0000
eeprom_action           selected, state 0x0000 => 0x0008
eeprom_action           not selected, count = 0, state 0x0008 => 0x0000
dp83815_mmio_readl      addr=CFG val=0x00000000
dp83815_mmio_writel     addr=PTSCR val=0x00000000
dp83815_mmio_readl      addr=PTSCR val=0x00000000
dp83815_mmio_readl      addr=CFG val=0x00000000
dp83815_mmio_readl      addr=WCSR val=0x00000000
dp83815_mmio_readl      addr=RFCR val=0x00000000
dp83815_mmio_writel     addr=RFCR val=0x00000000
dp83815_mmio_readw      addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x00000002
dp83815_mmio_readw      addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x00000004
dp83815_mmio_readw      addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000a
dp83815_mmio_readw      addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000c
dp83815_mmio_readw      addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000e
dp83815_mmio_readw      addr=RFDR val=0x0000
dp83815_mmio_writel     addr=CR val=0x00000000
dp83815_mmio_readl      addr=CR val=0x00000000
dp83815_mmio_readl      addr=CFG val=0x00000000
dp83815_mmio_writel     addr=CFG val=0x00000000
dp83815_mmio_readl      addr=WCSR val=0x00000000
dp83815_mmio_writel     addr=WCSR val=0x00000000
dp83815_mmio_readl      addr=RFCR val=0x0000000e
dp83815_mmio_writel     addr=RFCR val=0x00000000
dp83815_mmio_writew     addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x00000002
dp83815_mmio_writew     addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x00000004
dp83815_mmio_writew     addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000a
dp83815_mmio_writew     addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000c
dp83815_mmio_writew     addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000e
dp83815_mmio_writew     addr=RFDR val=0x0000
dp83815_mmio_writel     addr=RFCR val=0x0000000e
dp83815_mmio_readw      addr=BMCR val=0x0000
dp83815_mmio_readw      addr=ANAR val=0x05e1
dp83815_mmio_readl      addr=SRR val=0x00000505

dp83815_mmio_writew     addr=0xdc val=0x0001
dp83815_mmio_readw      addr=0x00f4 val=0x1000
dp83815_mmio_writew     addr=0xdc val=0x0000
dp83815_mmio_readw      addr=BMSR val=0x7849
dp83815_mmio_readw      addr=BMSR val=0x7849

#endif
