/*
 * QEMU Lance emulation
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "vl.h"

/* debug LANCE card */
//#define DEBUG_LANCE

#ifndef LANCE_LOG_TX_BUFFERS
#define LANCE_LOG_TX_BUFFERS 4
#define LANCE_LOG_RX_BUFFERS 4
#endif

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */


#define LE_CSR0 0
#define LE_CSR1 1
#define LE_CSR2 2
#define LE_CSR3 3
#define LE_MAXREG (LE_CSR3 + 1)

#define LE_RDP  0
#define LE_RAP  1

#define LE_MO_PROM      0x8000  /* Enable promiscuous mode */

#define	LE_C0_ERR	0x8000	/* Error: set if BAB, SQE, MISS or ME is set */
#define	LE_C0_BABL	0x4000	/* BAB:  Babble: tx timeout. */
#define	LE_C0_CERR	0x2000	/* SQE:  Signal quality error */
#define	LE_C0_MISS	0x1000	/* MISS: Missed a packet */
#define	LE_C0_MERR	0x0800	/* ME:   Memory error */
#define	LE_C0_RINT	0x0400	/* Received interrupt */
#define	LE_C0_TINT	0x0200	/* Transmitter Interrupt */
#define	LE_C0_IDON	0x0100	/* IFIN: Init finished. */
#define	LE_C0_INTR	0x0080	/* Interrupt or error */
#define	LE_C0_INEA	0x0040	/* Interrupt enable */
#define	LE_C0_RXON	0x0020	/* Receiver on */
#define	LE_C0_TXON	0x0010	/* Transmitter on */
#define	LE_C0_TDMD	0x0008	/* Transmitter demand */
#define	LE_C0_STOP	0x0004	/* Stop the card */
#define	LE_C0_STRT	0x0002	/* Start the card */
#define	LE_C0_INIT	0x0001	/* Init the card */

#define	LE_C3_BSWP	0x4     /* SWAP */
#define	LE_C3_ACON	0x2	/* ALE Control */
#define	LE_C3_BCON	0x1	/* Byte control */

/* Receive message descriptor 1 */
#define LE_R1_OWN       0x80    /* Who owns the entry */
#define LE_R1_ERR       0x40    /* Error: if FRA, OFL, CRC or BUF is set */
#define LE_R1_FRA       0x20    /* FRA: Frame error */
#define LE_R1_OFL       0x10    /* OFL: Frame overflow */
#define LE_R1_CRC       0x08    /* CRC error */
#define LE_R1_BUF       0x04    /* BUF: Buffer error */
#define LE_R1_SOP       0x02    /* Start of packet */
#define LE_R1_EOP       0x01    /* End of packet */
#define LE_R1_POK       0x03    /* Packet is complete: SOP + EOP */

#define LE_T1_OWN       0x80    /* Lance owns the packet */
#define LE_T1_ERR       0x40    /* Error summary */
#define LE_T1_EMORE     0x10    /* Error: more than one retry needed */
#define LE_T1_EONE      0x08    /* Error: one retry needed */
#define LE_T1_EDEF      0x04    /* Error: deferred */
#define LE_T1_SOP       0x02    /* Start of packet */
#define LE_T1_EOP       0x01    /* End of packet */
#define LE_T1_POK	0x03	/* Packet is complete: SOP + EOP */

#define LE_T3_BUF       0x8000  /* Buffer error */
#define LE_T3_UFL       0x4000  /* Error underflow */
#define LE_T3_LCOL      0x1000  /* Error late collision */
#define LE_T3_CLOS      0x0800  /* Error carrier loss */
#define LE_T3_RTY       0x0400  /* Error retry */
#define LE_T3_TDR       0x03ff  /* Time Domain Reflectometry counter */

#define TX_RING_SIZE			(1 << (LANCE_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK		(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS		((LANCE_LOG_TX_BUFFERS) << 29)

#define RX_RING_SIZE			(1 << (LANCE_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK		(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS		((LANCE_LOG_RX_BUFFERS) << 29)

#define PKT_BUF_SZ		1544
#define RX_BUFF_SIZE            PKT_BUF_SZ
#define TX_BUFF_SIZE            PKT_BUF_SZ

struct lance_rx_desc {
	unsigned short rmd0;        /* low address of packet */
	unsigned char  rmd1_bits;   /* descriptor bits */
	unsigned char  rmd1_hadr;   /* high address of packet */
	short    length;    	    /* This length is 2s complement (negative)!
				     * Buffer length
				     */
	unsigned short mblength;    /* This is the actual number of bytes received */
};

struct lance_tx_desc {
	unsigned short tmd0;        /* low address of packet */
	unsigned char  tmd1_bits;   /* descriptor bits */
	unsigned char  tmd1_hadr;   /* high address of packet */
	short length;          	    /* Length is 2s complement (negative)! */
	unsigned short misc;
};

/* The LANCE initialization block, described in databook. */
/* On the Sparc, this block should be on a DMA region     */
struct lance_init_block {
	unsigned short mode;		/* Pre-set mode (reg. 15) */
	unsigned char phys_addr[6];     /* Physical ethernet address */
	unsigned filter[2];		/* Multicast filter. */

	/* Receive and transmit ring base, along with extra bits. */
	unsigned short rx_ptr;		/* receive descriptor addr */
	unsigned short rx_len;		/* receive len and high addr */
	unsigned short tx_ptr;		/* transmit descriptor addr */
	unsigned short tx_len;		/* transmit len and high addr */
    
	/* The Tx and Rx ring entries must aligned on 8-byte boundaries. */
	struct lance_rx_desc brx_ring[RX_RING_SIZE];
	struct lance_tx_desc btx_ring[TX_RING_SIZE];
    
	char   tx_buf [TX_RING_SIZE][TX_BUFF_SIZE];
	char   pad[2];			/* align rx_buf for copy_and_sum(). */
	char   rx_buf [RX_RING_SIZE][RX_BUFF_SIZE];
};

#define LEDMA_REGS 4
#if 0
/* Structure to describe the current status of DMA registers on the Sparc */
struct sparc_dma_registers {
    uint32_t cond_reg;	/* DMA condition register */
    uint32_t st_addr;	/* Start address of this transfer */
    uint32_t cnt;	/* How many bytes to transfer */
    uint32_t dma_test;	/* DMA test register */
};
#endif

typedef struct LEDMAState {
    uint32_t addr;
    uint32_t regs[LEDMA_REGS];
} LEDMAState;

typedef struct LANCEState {
    uint32_t paddr;
    NetDriverState *nd;
    uint32_t leptr;
    uint16_t addr;
    uint16_t regs[LE_MAXREG];
    uint8_t phys[6]; /* mac address */
    int irq;
    LEDMAState *ledma;
} LANCEState;

static unsigned int rxptr, txptr;

static void lance_send(void *opaque);

static void lance_reset(LANCEState *s)
{
    memcpy(s->phys, s->nd->macaddr, 6);
    rxptr = 0;
    txptr = 0;
    s->regs[LE_CSR0] = LE_C0_STOP;
}

static uint32_t lance_mem_readw(void *opaque, target_phys_addr_t addr)
{
    LANCEState *s = opaque;
    uint32_t saddr;

    saddr = addr - s->paddr;
    switch (saddr >> 1) {
    case LE_RDP:
	return s->regs[s->addr];
    case LE_RAP:
	return s->addr;
    default:
	break;
    }
    return 0;
}

static void lance_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LANCEState *s = opaque;
    uint32_t saddr;
    uint16_t reg;

    saddr = addr - s->paddr;
    switch (saddr >> 1) {
    case LE_RDP:
	switch(s->addr) {
	case LE_CSR0:
	    if (val & LE_C0_STOP) {
		s->regs[LE_CSR0] = LE_C0_STOP;
		break;
	    }

	    reg = s->regs[LE_CSR0];

	    // 1 = clear for some bits
	    reg &= ~(val & 0x7f00);

	    // generated bits
	    reg &= ~(LE_C0_ERR | LE_C0_INTR);
	    if (reg & 0x7100)
		reg |= LE_C0_ERR;
	    if (reg & 0x7f00)
		reg |= LE_C0_INTR;

	    // direct bit
	    reg &= ~LE_C0_INEA;
	    reg |= val & LE_C0_INEA;

	    // exclusive bits
	    if (val & LE_C0_INIT) {
		reg |= LE_C0_IDON | LE_C0_INIT;
		reg &= ~LE_C0_STOP;
	    }
	    else if (val & LE_C0_STRT) {
		reg |= LE_C0_STRT | LE_C0_RXON | LE_C0_TXON;
		reg &= ~LE_C0_STOP;
	    }

	    s->regs[LE_CSR0] = reg;

	    // trigger bits
	    //if (val & LE_C0_TDMD)

	    if ((s->regs[LE_CSR0] & LE_C0_INTR) && (s->regs[LE_CSR0] & LE_C0_INEA))
		pic_set_irq(s->irq, 1);
	    break;
	case LE_CSR1:
	    s->leptr = (s->leptr & 0xffff0000) | (val & 0xffff);
	    s->regs[s->addr] = val;
	    break;
	case LE_CSR2:
	    s->leptr = (s->leptr & 0xffff) | ((val & 0xffff) << 16);
	    s->regs[s->addr] = val;
	    break;
	case LE_CSR3:
	    s->regs[s->addr] = val;
	    break;
	}
	break;
    case LE_RAP:
	if (val < LE_MAXREG)
	    s->addr = val;
	break;
    default:
	break;
    }
    lance_send(s);
}

static CPUReadMemoryFunc *lance_mem_read[3] = {
    lance_mem_readw,
    lance_mem_readw,
    lance_mem_readw,
};

static CPUWriteMemoryFunc *lance_mem_write[3] = {
    lance_mem_writew,
    lance_mem_writew,
    lance_mem_writew,
};


/* return the max buffer size if the LANCE can receive more data */
static int lance_can_receive(void *opaque)
{
    LANCEState *s = opaque;
    void *dmaptr = (void *) (s->leptr + s->ledma->regs[3]);
    struct lance_init_block *ib;
    int i;
    uint16_t temp;

    if ((s->regs[LE_CSR0] & LE_C0_STOP) == LE_C0_STOP)
	return 0;

    ib = (void *) iommu_translate(dmaptr);

    for (i = 0; i < RX_RING_SIZE; i++) {
	cpu_physical_memory_read(&ib->brx_ring[i].rmd1_bits, (void *) &temp, 1);
	temp &= 0xff;
	if (temp == (LE_R1_OWN)) {
#ifdef DEBUG_LANCE
	    fprintf(stderr, "lance: can receive %d\n", RX_BUFF_SIZE);
#endif
	    return RX_BUFF_SIZE;
	}
    }
#ifdef DEBUG_LANCE
    fprintf(stderr, "lance: cannot receive\n");
#endif
    return 0;
}

#define MIN_BUF_SIZE 60

static void lance_receive(void *opaque, const uint8_t *buf, int size)
{
    LANCEState *s = opaque;
    void *dmaptr = (void *) (s->leptr + s->ledma->regs[3]);
    struct lance_init_block *ib;
    unsigned int i, old_rxptr, j;
    uint16_t temp;

    if ((s->regs[LE_CSR0] & LE_C0_STOP) == LE_C0_STOP)
	return;

    ib = (void *) iommu_translate(dmaptr);

    old_rxptr = rxptr;
    for (i = rxptr; i != ((old_rxptr - 1) & RX_RING_MOD_MASK); i = (i + 1) & RX_RING_MOD_MASK) {
	cpu_physical_memory_read(&ib->brx_ring[i].rmd1_bits, (void *) &temp, 1);
	if (temp == (LE_R1_OWN)) {
	    rxptr = (rxptr + 1) & RX_RING_MOD_MASK;
	    temp = size;
	    bswap16s(&temp);
	    cpu_physical_memory_write(&ib->brx_ring[i].mblength, (void *) &temp, 2);
#if 0
	    cpu_physical_memory_write(&ib->rx_buf[i], buf, size);
#else
	    for (j = 0; j < size; j++) {
		cpu_physical_memory_write(((void *)&ib->rx_buf[i]) + j, &buf[j], 1);
	    }
#endif
	    temp = LE_R1_POK;
	    cpu_physical_memory_write(&ib->brx_ring[i].rmd1_bits, (void *) &temp, 1);
	    s->regs[LE_CSR0] |= LE_C0_RINT | LE_C0_INTR;
	    if ((s->regs[LE_CSR0] & LE_C0_INTR) && (s->regs[LE_CSR0] & LE_C0_INEA))
		pic_set_irq(s->irq, 1);
#ifdef DEBUG_LANCE
	    fprintf(stderr, "lance: got packet, len %d\n", size);
#endif
	    return;
	}
    }
}

static void lance_send(void *opaque)
{
    LANCEState *s = opaque;
    void *dmaptr = (void *) (s->leptr + s->ledma->regs[3]);
    struct lance_init_block *ib;
    unsigned int i, old_txptr, j;
    uint16_t temp;
    char pkt_buf[PKT_BUF_SZ];

    if ((s->regs[LE_CSR0] & LE_C0_STOP) == LE_C0_STOP)
	return;

    ib = (void *) iommu_translate(dmaptr);

    old_txptr = txptr;
    for (i = txptr; i != ((old_txptr - 1) & TX_RING_MOD_MASK); i = (i + 1) & TX_RING_MOD_MASK) {
	cpu_physical_memory_read(&ib->btx_ring[i].tmd1_bits, (void *) &temp, 1);
	if (temp == (LE_T1_POK|LE_T1_OWN)) {
	    cpu_physical_memory_read(&ib->btx_ring[i].length, (void *) &temp, 2);
	    bswap16s(&temp);
	    temp = (~temp) + 1;
#if 0
	    cpu_physical_memory_read(&ib->tx_buf[i], pkt_buf, temp);
#else
	    for (j = 0; j < temp; j++) {
		cpu_physical_memory_read(((void *)&ib->tx_buf[i]) + j, &pkt_buf[j], 1);
	    }
#endif

#ifdef DEBUG_LANCE
	    fprintf(stderr, "lance: sending packet, len %d\n", temp);
#endif
	    qemu_send_packet(s->nd, pkt_buf, temp);
	    temp = LE_T1_POK;
	    cpu_physical_memory_write(&ib->btx_ring[i].tmd1_bits, (void *) &temp, 1);
	    txptr = (txptr + 1) & TX_RING_MOD_MASK;
	    s->regs[LE_CSR0] |= LE_C0_TINT | LE_C0_INTR;
	}
    }
}

static uint32_t ledma_mem_readl(void *opaque, target_phys_addr_t addr)
{
    LEDMAState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    if (saddr < LEDMA_REGS)
	return s->regs[saddr];
    else
	return 0;
}

static void ledma_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LEDMAState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    if (saddr < LEDMA_REGS)
	s->regs[saddr] = val;
}

static CPUReadMemoryFunc *ledma_mem_read[3] = {
    ledma_mem_readl,
    ledma_mem_readl,
    ledma_mem_readl,
};

static CPUWriteMemoryFunc *ledma_mem_write[3] = {
    ledma_mem_writel,
    ledma_mem_writel,
    ledma_mem_writel,
};

void lance_init(NetDriverState *nd, int irq, uint32_t leaddr, uint32_t ledaddr)
{
    LANCEState *s;
    LEDMAState *led;
    int lance_io_memory, ledma_io_memory;

    s = qemu_mallocz(sizeof(LANCEState));
    if (!s)
        return;

    s->paddr = leaddr;
    s->nd = nd;
    s->irq = irq;

    lance_io_memory = cpu_register_io_memory(0, lance_mem_read, lance_mem_write, s);
    cpu_register_physical_memory(leaddr, 8, lance_io_memory);

    led = qemu_mallocz(sizeof(LEDMAState));
    if (!led)
        return;

    s->ledma = led;
    led->addr = ledaddr;
    ledma_io_memory = cpu_register_io_memory(0, ledma_mem_read, ledma_mem_write, led);
    cpu_register_physical_memory(ledaddr, 16, ledma_io_memory);

    lance_reset(s);
    qemu_add_read_packet(nd, lance_can_receive, lance_receive, s);
}

