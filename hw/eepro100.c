/*
 * QEMU i82559 (EEPRO100) emulation
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
 */
#include "vl.h"

#define PCI_VENDOR_ID           0x00    /* 16 bits */
#define PCI_DEVICE_ID           0x02    /* 16 bits */
#define PCI_COMMAND             0x04    /* 16 bits */

#define PCI_REVISION            0x08    /* 8 bits  */
#define PCI_CLASS_CODE          0x0b    /* 8 bits */
#define PCI_SUBCLASS_CODE       0x0a    /* 8 bits */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits */

#define PCI_BASE_ADDRESS_0      0x10    /* 32 bits */
#define PCI_BASE_ADDRESS_1      0x14    /* 32 bits */
#define PCI_BASE_ADDRESS_2      0x18    /* 32 bits */
#define PCI_BASE_ADDRESS_3      0x1c    /* 32 bits */
#define PCI_BASE_ADDRESS_4      0x20    /* 32 bits */
#define PCI_BASE_ADDRESS_5      0x24    /* 32 bits */

/* debug EEPRO100 card */
#define DEBUG_EEPRO100

#define logout(fmt, args...) printf("EEPRO100 %-24s" fmt, __func__, ##args)

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

#define EEPRO100_PMEM_SIZE    (32*1024)
#define EEPRO100_PMEM_START   (16*1024)
#define EEPRO100_PMEM_END     (EEPRO100_PMEM_SIZE+EEPRO100_PMEM_START)
#define EEPRO100_MEM_SIZE     EEPRO100_PMEM_END

#define KiB 1024

#define PCI_MEM_SIZE            (4 * KiB)
#define PCI_IO_SIZE             64
#define PCI_FLASH_SIZE          (128 * KiB)

typedef struct EEPRO100State {
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
    int mmio_index;
    PCIDevice *pci_dev;
    VLANClientState *vc;
    uint8_t macaddr[6];
    uint8_t mem[EEPRO100_MEM_SIZE];
} EEPRO100State;

static void eepro100_update_irq(EEPRO100State *s)
{
    int isr;
    isr = (s->isr & s->imr) & 0x7f;
#if defined(DEBUG_EEPRO100)
    logout("Set IRQ line %d to %d (%02x %02x)\n",
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

static int eepro100_buffer_full(EEPRO100State *s)
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

static int eepro100_can_receive(void *opaque)
{
    EEPRO100State *s = opaque;
    
    if (s->cmd & E8390_STOP)
        return 1;
    return !eepro100_buffer_full(s);
}

#define MIN_BUF_SIZE 60

static void eepro100_receive(void *opaque, const uint8_t *buf, int size)
{
    EEPRO100State *s = opaque;
    uint8_t *p;
    int total_len, next, avail, len, index, mcast_idx;
    uint8_t buf1[60];
    static const uint8_t broadcast_macaddr[6] = 
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    
#if defined(DEBUG_EEPRO100)
    logout("received len=%d\n", size);
#endif

    if (s->cmd & E8390_STOP || eepro100_buffer_full(s))
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

    /* now we can signal we have receive something */
    s->isr |= ENISR_RX;
    eepro100_update_irq(s);
}

static void ioport_write4(void *opaque, uint32_t addr, uint32_t val)
{
    //~ EEPRO100State *s = opaque;

#ifdef DEBUG_EEPRO100
    logout("write addr=0x%x val=0x%08x\n", addr, val);
#endif
}

static uint32_t ioport_read4(void *opaque, uint32_t addr)
{
    //~ EEPRO100State *s = opaque;
    int ret = 0xffffffff;

#ifdef DEBUG_EEPRO100
    logout("read addr=0x%x val=%08x\n", addr, ret);
#endif
    return ret;
}

static void ioport_write2(void *opaque, uint32_t addr, uint32_t val)
{
    //~ EEPRO100State *s = opaque;

#ifdef DEBUG_EEPRO100
    logout("write addr=0x%x val=0x%04x\n", addr, val);
#endif
}

static uint32_t ioport_read2(void *opaque, uint32_t addr)
{
    //~ EEPRO100State *s = opaque;
    int ret = 0xffffffff;

#ifdef DEBUG_EEPRO100
    logout("read addr=0x%x val=%04x\n", addr, ret);
#endif
    return ret;
}

static void ioport_write1(void *opaque, uint32_t addr, uint32_t val)
{
    //~ EEPRO100State *s = opaque;
    //~ int offset, page, index;

    //~ addr &= 0xf;
#ifdef DEBUG_EEPRO100
    logout("write addr=0x%x val=0x%02x\n", addr, val);
#endif
}

static uint32_t ioport_read1(void *opaque, uint32_t addr)
{
    //~ EEPRO100State *s = opaque;
    //~ int offset, page;
    int ret = 0xffffffff;

    //~ addr &= 0xf;
#ifdef DEBUG_EEPRO100
    logout("read addr=0x%x val=%02x\n", addr, ret);
#endif
    return ret;
}

static inline void eepro100_mem_writeb(EEPRO100State *s, uint32_t addr, 
                                     uint32_t val)
{
    if (addr < 32 || 
        (addr >= EEPRO100_PMEM_START && addr < EEPRO100_MEM_SIZE)) {
        s->mem[addr] = val;
    }
}

static inline void eepro100_mem_writew(EEPRO100State *s, uint32_t addr, 
                                     uint32_t val)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= EEPRO100_PMEM_START && addr < EEPRO100_MEM_SIZE)) {
        *(uint16_t *)(s->mem + addr) = cpu_to_le16(val);
    }
}

static inline void eepro100_mem_writel(EEPRO100State *s, uint32_t addr, 
                                     uint32_t val)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= EEPRO100_PMEM_START && addr < EEPRO100_MEM_SIZE)) {
        cpu_to_le32wu((uint32_t *)(s->mem + addr), val);
    }
}

static inline uint32_t eepro100_mem_readb(EEPRO100State *s, uint32_t addr)
{
    if (addr < 32 || 
        (addr >= EEPRO100_PMEM_START && addr < EEPRO100_MEM_SIZE)) {
        return s->mem[addr];
    } else {
        return 0xff;
    }
}

static inline uint32_t eepro100_mem_readw(EEPRO100State *s, uint32_t addr)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= EEPRO100_PMEM_START && addr < EEPRO100_MEM_SIZE)) {
        return le16_to_cpu(*(uint16_t *)(s->mem + addr));
    } else {
        return 0xffff;
    }
}

static inline uint32_t eepro100_mem_readl(EEPRO100State *s, uint32_t addr)
{
    addr &= ~1; /* XXX: check exact behaviour if not even */
    if (addr < 32 || 
        (addr >= EEPRO100_PMEM_START && addr < EEPRO100_MEM_SIZE)) {
        return le32_to_cpupu((uint32_t *)(s->mem + addr));
    } else {
        return 0xffffffff;
    }
}

static inline void eepro100_dma_update(EEPRO100State *s, int len)
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
        eepro100_update_irq(s);
    } else {
        s->rcnt -= len;
    }
}

static void nic_save(QEMUFile* f,void* opaque)
{
        EEPRO100State* s=(EEPRO100State*)opaque;

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
        qemu_put_be32s(f, &s->irq);
        qemu_put_buffer(f, s->mem, EEPRO100_MEM_SIZE);
}

static int nic_load(QEMUFile* f,void* opaque,int version_id)
{
        EEPRO100State* s=(EEPRO100State*)opaque;
        int ret;

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
        qemu_get_be32s(f, &s->irq);
        qemu_get_buffer(f, s->mem, EEPRO100_MEM_SIZE);

        return 0;
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

#if defined(DEBUG_EEPRO100)
    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);
#endif

    register_ioport_write(addr, size, 1, ioport_write1, s);
    register_ioport_read(addr, size, 1, ioport_read1, s);
    register_ioport_write(addr, size, 2, ioport_write2, s);
    register_ioport_read(addr, size, 2, ioport_read2, s);
    register_ioport_write(addr, size, 4, ioport_write4, s);
    register_ioport_read(addr, size, 4, ioport_read4, s);
}

static char *regname(target_phys_addr_t addr)
{
  static char buf[16];
  sprintf(buf, "0x%08x", addr);
  return buf;
}

static void pci_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#if defined(DEBUG_EEPRO100)
    logout("addr=%s val=0x%02x\n", regname(addr), val);
#endif
    //~ pci_io_writeb(opaque, addr & 0xFF, val);
}

static void pci_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#if defined(DEBUG_EEPRO100)
    logout("addr=%s val=0x%02x\n", regname(addr), val);
#endif
    //~ pci_io_writew(opaque, addr & 0xFF, val);
}

static void pci_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#if defined(DEBUG_EEPRO100)
    logout("addr=%s val=0x%02x\n", regname(addr), val);
#endif
    //~ pci_io_writel(opaque, addr & 0xFF, val);
}

static uint32_t pci_mmio_readb(void *opaque, target_phys_addr_t addr)
{
#if defined(DEBUG_EEPRO100)
    logout("addr=%s\n", regname(addr));
#endif
    return 0; // pci_io_readb(opaque, addr & 0xFF);
}

static uint32_t pci_mmio_readw(void *opaque, target_phys_addr_t addr)
{
#if defined(DEBUG_EEPRO100)
    logout("addr=%s\n", regname(addr));
#endif
    return 0; // pci_io_readw(opaque, addr & 0xFF);
}

static uint32_t pci_mmio_readl(void *opaque, target_phys_addr_t addr)
{
#if defined(DEBUG_EEPRO100)
    logout("addr=%s\n", regname(addr));
#endif
    return 0; // pci_io_readl(opaque, addr & 0xFF);
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

#if defined(DEBUG_EEPRO100)
    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);
#endif

    cpu_register_physical_memory(addr, PCI_MEM_SIZE, d->eepro100.mmio_index);
}

static void nic_reset(void *opaque)
{
    PCIEEPRO100State *d = (PCIEEPRO100State *)opaque;
#if defined(DEBUG_EEPRO100)
    logout("%p\n", d);
#endif
}

void pci_eepro100_init(PCIBus *bus, NICInfo *nd)
{
    PCIEEPRO100State *d;
    EEPRO100State *s;
    uint8_t *pci_conf;
    
#if defined(DEBUG_EEPRO100)
    logout("\n");
#endif

    d = (PCIEEPRO100State *)pci_register_device(bus, "EEPRO100",
        sizeof(PCIEEPRO100State), -1, NULL, NULL);
    pci_conf = d->dev.config;
#define PCI_CONFIG_8(offset, value) \
    (pci_conf[offset] = (value))
#define PCI_CONFIG_16(offset, value) \
    (*(uint16_t *)&pci_conf[offset] = cpu_to_le16(value))
#define PCI_CONFIG_32(offset, value) \
    (*(uint32_t *)&pci_conf[offset] = cpu_to_le32(value))
    /* PCI Vendor ID */
    PCI_CONFIG_16(PCI_VENDOR_ID, 0x8086);
    /* PCI Device ID */
    PCI_CONFIG_16(PCI_DEVICE_ID, 0x1209);
    /* PCI Command */
    PCI_CONFIG_16(PCI_COMMAND, 0x0000);
    /* PCI Status */
    PCI_CONFIG_16(0x06, 0x2800);
    /* PCI Class code / PCI Revision ID */
    PCI_CONFIG_8(PCI_REVISION, 0x08);
    PCI_CONFIG_8(0x09, 0x00);
    PCI_CONFIG_8(PCI_SUBCLASS_CODE, 0x00); // ethernet network controller 
    PCI_CONFIG_8(PCI_CLASS_CODE, 0x02); // network controller
    /* bist / PCI Hader Type / PCI Latency Timer / PCI Cache Line Size */
    /* check cache line size!!! */
    //~ PCI_CONFIG_8(0x0c, 0x00);
    PCI_CONFIG_8(0x0d, 0x20); // latency timer = 32 clocks
    /* CSR Memory Mapped Base Address */
    PCI_CONFIG_32(PCI_BASE_ADDRESS_0, 0x00000000);
    /* CSR I/O Mapped Base Address */
    PCI_CONFIG_32(PCI_BASE_ADDRESS_1, 0x00000001);
    /* Flash Memory Mapped Base Address */
    PCI_CONFIG_32(PCI_BASE_ADDRESS_2, 0xfffe0000);
    /* Expansion ROM Base Address (depends on boot disable!!!) */
    PCI_CONFIG_32(0x30, 0x00000000);
    /* Capability Pointer */
    PCI_CONFIG_8(0x34, 0xdc);
    /* Interrupt Pin */
    PCI_CONFIG_8(0x3d, 1); // interrupt pin 0
    PCI_CONFIG_8(0x3e, 0x08); // minimum grant
    PCI_CONFIG_8(0x3f, 0x18); // maximum latency
    /* Power Management Capabilities / Next Item Pointer / Capability ID */
    PCI_CONFIG_32(0xdc, 0x7e210001);

    /* Handler for memory-mapped I/O */
    d->eepro100.mmio_index =
      cpu_register_io_memory(0, pci_mmio_read, pci_mmio_write, d);

    pci_register_io_region(&d->dev, 0, PCI_MEM_SIZE,
                           PCI_ADDRESS_SPACE_MEM, pci_mmio_map);
    pci_register_io_region(&d->dev, 1, PCI_IO_SIZE,
                           PCI_ADDRESS_SPACE_IO, pci_map);
    pci_register_io_region(&d->dev, 2, PCI_FLASH_SIZE,
                           PCI_ADDRESS_SPACE_MEM, pci_mmio_map);

    s = &d->eepro100;
    s->irq = 16; // PCI interrupt
    s->pci_dev = &d->dev;
    memcpy(s->macaddr, nd->macaddr, 6);

    nic_reset(d);

    s->vc = qemu_new_vlan_client(nd->vlan, eepro100_receive,
                                 eepro100_can_receive, s);

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "eepro100 pci macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             s->macaddr[0],
             s->macaddr[1],
             s->macaddr[2],
             s->macaddr[3],
             s->macaddr[4],
             s->macaddr[5]);

    qemu_register_reset(nic_reset, d);

    /* XXX: instance number ? */
    register_savevm("eepro100", 0, 3, nic_save, nic_load, s);
}
