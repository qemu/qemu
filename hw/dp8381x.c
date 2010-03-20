/*
 * QEMU emulation for National Semiconductor DP83815 / DP83816.
 * 
 * Copyright (C) 2006-2009 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Datasheets are available from National Semiconductor, see
 * http://www.national.com/pf/DP/DP83815.html
 * http://www.national.com/pf/DP/DP83816.html
 *
 * Missing features:
 *      Wake-On-LAN (WOL)
 *      Big-Endian-Mode
 *      many details
 *
 * Tested features (dp83816):
 *      PXE boot (i386) ok
 *      Linux networking (i386, mipsel) ok
 *      big endian target (mips malta) ok
 *
 * Untested features:
 *      big endian host cpu
 *
 * TODO:
 *      Implement save, load VM support.
 */

#include <assert.h>             /* assert */
#include "hw.h"
#include "net.h"
#include "pci.h"
#include "eeprom93xx.h"

/*****************************************************************************
 *
 * Common declarations.
 *
 ****************************************************************************/

#define KiB 1024

/*****************************************************************************
 *
 * Declarations for emulation options and debugging.
 *
 ****************************************************************************/

/* Debug DP8381x card. */
#define DEBUG_DP8381X

#if defined(DEBUG_DP8381X)
# define logout(fmt, ...) fprintf(stderr, "DP8381X %-24s" fmt, __func__, ##__VA_ARGS__)
#else
# define logout(fmt, ...) ((void)0)
#endif

#define missing(text)       assert(!"feature is missing in this emulation: " text)

/* Enable or disable logging categories. */
#define LOG_EEPROM      0
#define LOG_PHY         1
#define LOG_RX          1       /* receive messages */
#define LOG_TX          1       /* transmit messages */

#if defined(DEBUG_DP8381X)
# define TRACE(condition, command) ((condition) ? (command) : (void)0)
#else
# define TRACE(condition, command) ((void)0)
#endif

/* EEPROM support is optional. */
#define CONFIG_EEPROM
#define EEPROM_SIZE     16

/* Silicon revisions for the different hardware */
#define DP83815CVNG     0x00000302
#define DP83815DVNG     0x00000403
#define DP83816AVNG     0x00000505

#define MAX_ETH_FRAME_SIZE 1514

#define DP8381X_IO_SIZE         256
#define DP8381X_MEM_SIZE        4096

static int dp8381x_instance = 0;
static const int dp8381x_version = 20060726;

/*****************************************************************************
 *
 * EEPROM emulation.
 *
 ****************************************************************************/

typedef enum {
    idle,
    active
} state_t;

typedef struct {
    //~ uint8_t cmd;
    //~ uint32_t start;
    //~ uint32_t stop;
    //~ uint8_t boundary;
    //~ uint8_t tsr;
    //~ uint8_t tpsr;
    //~ uint16_t tcnt;
    //~ uint16_t rcnt;
    //~ uint32_t rsar;
    //~ uint8_t rsr;
    //~ uint8_t rxcr;
    //~ uint8_t dcfg;
    //~ uint8_t phys[6]; /* mac address */
    //~ uint8_t curpag;
    //~ uint8_t mult[8]; /* multicast mask array */
    state_t rx_state:8;
    state_t tx_state:8;

    /* Variables for QEMU interface. */
    int io_memory;              /* handle for memory mapped I/O */
    PCIDevice *pci_dev;
    uint32_t region[2];         /* PCI region addresses */
    eeprom_t *eeprom;
    NICState *nic;
    NICConf conf;
    uint8_t mem[DP8381X_IO_SIZE];
    uint8_t filter[1024];
    uint32_t silicon_revision;
} dp8381x_t;

#if defined(CONFIG_EEPROM)
static const uint16_t eeprom_default[16] = {
    /* Default values for EEPROM. */
    /* Only 12 words are used. Data is in host byte order. */
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
#endif

/*****************************************************************************
 *
 * Register emulation.
 *
 ****************************************************************************/

/* Operational Registers. */

typedef enum {
    /* MAC/BIU Registers */
    DP8381X_CR = 0x00,
    DP8381X_CFG = 0x04,
    DP8381X_MEAR = 0x08,
    DP8381X_PTSCR = 0x0c,
    DP8381X_ISR = 0x10,
    DP8381X_IMR = 0x14,
    DP8381X_IER = 0x18,
    DP8381X_IHR = 0x1c,
    DP8381X_TXDP = 0x20,
    DP8381X_TXCFG = 0x24,
    //~ DP8381X_R = 0x28,
    //~ DP8381X_R = 0x2c,
    DP8381X_RXDP = 0x30,
    DP8381X_RXCFG = 0x34,
    //~ DP8381X_R = 0x38,
    DP8381X_CCSR = 0x3c,
    DP8381X_WCSR = 0x40,
    DP8381X_PCR = 0x44,
    DP8381X_RFCR = 0x48,
    DP8381X_RFDR = 0x4c,
    DP8381X_BRAR = 0x50,
    DP8381X_BRDR = 0x54,
    DP8381X_SRR = 0x58,
    DP8381X_MIBC = 0x5c,
    DP8381X_MIB0 = 0x60,
    DP8381X_MIB1 = 0x64,
    DP8381X_MIB2 = 0x68,
    DP8381X_MIB3 = 0x6c,
    DP8381X_MIB4 = 0x70,
    DP8381X_MIB5 = 0x74,
    DP8381X_MIB6 = 0x78,
    /* Internal Phy Registers */
    DP8381X_BMCR = 0x80,        /* Control Register */
    DP8381X_BMSR = 0x84,        /* Status Register */
    DP8381X_PHYIDR1 = 0x88,     /* PHY Identification Register 1 */
    DP8381X_PHYIDR2 = 0x8c,     /* PHY Identification Register 2 */
    DP8381X_ANAR = 0x90,        /* Auto-Negotiation Advertisment Register */
    DP8381X_ANLPAR = 0x94,      /* Auto-Negotiation Link Partner Ability Register */
    DP8381X_ANER = 0x98,        /* Auto-Negotiation Expansion Register */
    DP8381X_ANPTR = 0x9c,
    DP8381X_PHYSTS = 0xc0,
    DP8381X_MICR = 0xc4,
    DP8381X_MISR = 0xc8,
    DP8381X_PGSEL = 0xcc,
    DP8381X_FCSCR = 0xd0,
    DP8381X_RECR = 0xd4,
    DP8381X_PCSR = 0xd8,
    DP8381X_0xdc = 0xdc,
    DP8381X_PHYCR = 0xe4,
    DP8381X_TBTSCR = 0xe8,
    DP8381X_00EC = 0xec,
    DP8381X_DSPCFG = 0xf4,
    DP8381X_SDCFG = 0xf8,
    DP8381X_TSTDAT = 0xfc,
} dp8381x_register_t;

#define BIT(n) (1 << (n))
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

typedef enum {
    CR_RST = BIT(8),
    CR_SWI = BIT(7),
    CR_RXR = BIT(5),
    CR_TXR = BIT(4),
    CR_RXD = BIT(3),
    CR_RXE = BIT(2),
    CR_TXD = BIT(1),
    CR_TXE = BIT(0),
} cr_bit_t;

typedef enum {
    CFG_LNKSTS = BIT(31),
    CFG_SPEED100 = BIT(30),
    CFG_FDUP = BIT(29),
    CFG_POL = BIT(28),
    CFG_ANEG_DN = BIT(27),
    CFG_PINT_ACEN = BIT(17),
    CFG_ANEG_SEL = BITS(15, 13),
    CFG_EXT_PHY = BIT(12),
    CFG_BEM = BIT(0),
} cfg_bit_t;

typedef enum {
    ISR_TXRCMP = BIT(25),
    ISR_RXRCMP = BIT(24),
    ISR_PHY = BIT(14),
    ISR_SWI = BIT(12),
    ISR_TXIDLE = BIT(9),
    ISR_TXDESC = BIT(7),
    ISR_TXOK = BIT(6),
    ISR_RXORN = BIT(5),
    ISR_RXIDLE = BIT(4),
    ISR_RXDESC = BIT(1),
    ISR_RXOK = BIT(0),
    /* Special values for dp8381x_interrupt. */
    ISR_CLEAR = 0,
    ISR_UPDATE = BITS(31, 0),
} isr_bit_t;

typedef isr_bit_t imr_bit_t;

typedef enum {
    MEAR_MDC = BIT(6),          /* MII Management Clock */
    MEAR_MDDIR = BIT(5),        /* MII Management Direction */
    MEAR_MDIO = BIT(4),         /* MII Management Data */
    MEAR_EESEL = BIT(3),        /* EEPROM Chip Select */
    MEAR_EECLK = BIT(2),        /* EEPROM Serial Clock */
    MEAR_EEDO = BIT(1),         /* EEPROM Data Out */
    MEAR_EEDI = BIT(0),         /* EEPROM Data In */
} mear_bit_t;

typedef enum {
    PTSCR_RBIST_EN = BIT(7),
    PTSCR_RBIST_DONE = BIT(6),
    PTSCR_EELOAD_EN = BIT(2),
    PTSCR_EEBIST_EN = BIT(1),
} ptscr_bit_t;

typedef enum {
    RFCR_RFADDR = BITS(9, 0),
} rfcr_bit_t;

typedef enum {
    MIBC_MIBS = BIT(3),
    MIBC_ACLR = BIT(2),
} mibc_bit_t;

typedef enum {
    MICR_INTEN = BIT(1),
    MICR_TINT = BIT(0),
} micr_bit_t;

typedef enum {
    MISR_MINT = BIT(15),
} misr_bit_t;

static void stl_le_phys(target_phys_addr_t addr, uint32_t val)
{
    val = cpu_to_le32(val);
    cpu_physical_memory_write(addr, (const uint8_t *)&val, sizeof(val));
}

static uint32_t op_reg_read(dp8381x_t * s, uint32_t addr)
{
    assert(addr < 0x80 && !(addr & 3));
    return le32_to_cpu(*(uint32_t *) (&s->mem[addr]));
}

static void op_reg_write(dp8381x_t * s, uint32_t addr, uint32_t value)
{
    assert(addr < 0x80 && !(addr & 3));
    *(uint32_t *) (&s->mem[addr]) = cpu_to_le32(value);
}

static uint16_t phy_reg_read(dp8381x_t * s, uint32_t addr)
{
    assert(addr >= 0x80 && addr < 0x100 && !(addr & 3));
    return le16_to_cpu(*(uint16_t *) (&s->mem[addr]));
}

static void phy_reg_write(dp8381x_t * s, uint32_t addr, uint32_t value)
{
    assert(addr >= 0x80 && addr < 0x100 && !(addr & 3));
    *(uint16_t *) (&s->mem[addr]) = cpu_to_le16(value);
}

static void init_operational_registers(dp8381x_t * s)
{
#define OP_REG(offset, value) op_reg_write(s, offset, value)
    OP_REG(DP8381X_CR, 0x00000000);     /* Command */
    OP_REG(DP8381X_CFG, 0x00000000);    /* Configuration and Media Status */
    OP_REG(DP8381X_MEAR, 0x00000002);   /* EEPROM Access */
    OP_REG(DP8381X_PTSCR, 0x00000000);  /* PCI Test Control */
    OP_REG(DP8381X_ISR, 0x03008000);    /* Interrupt Status */
    OP_REG(DP8381X_IMR, 0x00000000);    /* Interrupt Mask */
    OP_REG(DP8381X_IER, 0x00000000);    /* Interrupt Enable */
    OP_REG(DP8381X_IHR, 0x00000000);    /* Interrupt Holdoff */
    OP_REG(DP8381X_TXDP, 0x00000000);   /* Transmit Descriptor Pointer */
#if defined(DP83815)
    OP_REG(DP8381X_TXCFG, 0x00000102);  /* Transmit Configuration */
#else
    OP_REG(DP8381X_TXCFG, 0x00040102);  /* Transmit Configuration */
#endif
    OP_REG(DP8381X_RXDP, 0x00000000);   /* Receive Descriptor Pointer */
    OP_REG(DP8381X_RXCFG, 0x00000002);  /* Receive Configuration */
    OP_REG(DP8381X_WCSR, 0x00000000);   /* Wake Command/Status */
    OP_REG(DP8381X_PCR, 0x00000000);    /* Pause Control/Status */
    OP_REG(DP8381X_RFCR, 0x00000000);   /* Receive Filter/Match Control */
    OP_REG(DP8381X_RFDR, 0x00000000);   /* Receive Filter Data */
    /* hard reset only */
    OP_REG(DP8381X_BRAR, 0xffffffff);   /* Boot ROM Address */
    OP_REG(DP8381X_SRR, s->silicon_revision);   /* Silicon Revision */
    OP_REG(DP8381X_MIBC, 0x00000002);   /* Management Information Base Control */

#define PHY_REG(offset, value) phy_reg_write(s, offset, value)
    PHY_REG(DP8381X_BMCR, 0x0000);      /* TODO */
    PHY_REG(DP8381X_BMSR, 0x7849);
    PHY_REG(DP8381X_PHYIDR1, 0x2000);
    PHY_REG(DP8381X_PHYIDR2, 0x5c21);
    PHY_REG(DP8381X_ANAR, 0x05e1);
    PHY_REG(DP8381X_ANER, 0x0004);
    PHY_REG(DP8381X_ANPTR, 0x2001);
    PHY_REG(DP8381X_PCSR, 0x0100);
    PHY_REG(DP8381X_PHYCR, 0x003f);
#if defined(DP83815)
    PHY_REG(DP8381X_TBTSCR, 0x0004);
#else
    PHY_REG(DP8381X_TBTSCR, 0x0804);
#endif
}

static void dp8381x_reset(dp8381x_t * s)
{
    unsigned i;
    logout("\n");
    init_operational_registers(s);
    s->rx_state = idle;
    s->tx_state = idle;
    for (i = 0; i < 6; i++) {
        s->filter[2 * i] = s->conf.macaddr.a[i];
    }
}

static void dp8381x_interrupt(dp8381x_t * s, uint32_t bits)
{
    uint32_t isr = op_reg_read(s, DP8381X_ISR);
    uint32_t imr = op_reg_read(s, DP8381X_IMR);
    uint32_t ier = op_reg_read(s, DP8381X_IER);
    if (bits == ISR_CLEAR) {
        uint32_t cfg = op_reg_read(s, DP8381X_CFG);
        if (cfg & CFG_PINT_ACEN) {
            uint16_t misr = phy_reg_read(s, DP8381X_MISR);
            misr &= ~MISR_MINT;
            phy_reg_write(s, DP8381X_MISR, misr);
        }
        isr = 0;
    } else if (bits != ISR_UPDATE) {
        isr |= bits;
    }
    op_reg_write(s, DP8381X_ISR, isr);
    qemu_set_irq(s->pci_dev->irq[0], (ier && (isr & imr)));
}

#define POLYNOMIAL 0x04c11db6

#if 0
/* From FreeBSD */
/* XXX: optimize */
static int compute_mcast_idx(const uint8_t * ep)
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

typedef struct {
    uint32_t link;
    uint32_t cmdsts;
    uint32_t bufptr;
} descriptor_t;

typedef descriptor_t rx_descriptor_t;
typedef descriptor_t tx_descriptor_t;

static int nic_can_receive(VLANClientState *vc)
{
    dp8381x_t *s = DO_UPCAST(NICState, nc, vc)->opaque;

    logout("\n");

    /* TODO: handle queued receive data. */
    return s->rx_state == active;
}

typedef enum {
    CMDSTS_OWN = BIT(31),
    CMDSTS_MORE = BIT(30),
    CMDSTS_INTR = BIT(29),
    CMDSTS_SUPCRC = BIT(28),
    CMDSTS_OK = BIT(27),
    CMDSTS_SIZE = BITS(11, 0),
    /* transmit status bits */
    /* receive status bits */
    CMDSTS_DEST = BITS(24, 23),
    CMDSTS_LONG = BIT(22),
    CMDSTS_RUNT = BIT(21),
} cmdsts_bit_t;

static ssize_t nic_receive(VLANClientState *vc, const uint8_t * buf, size_t size)
{
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    dp8381x_t *s = DO_UPCAST(NICState, nc, vc)->opaque;
#if 0
    uint8_t *p;
    int total_len, next, avail, len, index, mcast_idx;
    uint8_t buf1[60];
#endif

    TRACE(LOG_RX, logout("len=%u\n", (unsigned)size));

    /* TODO: handle queued receive data. */

    if (s->rx_state != active) {
        return -1;
    }
    //~ missing("");

    /* Filter incoming packet. */

    if (0 /*PME!!! */ ) {
        /* Packet filters enabled. */
        missing("mode only used for wake-on-lan");
    } else if (!memcmp(buf, s->conf.macaddr.a, 6)) {
        /* my address */
        TRACE(LOG_RX, logout("my mac address\n"));
    } else if (!memcmp(buf, broadcast_macaddr, 6)) {
        /* broadcast address */
        TRACE(LOG_RX, logout("broadcast address\n"));
    } else if (buf[0] & 0x01) {
        /* multicast */
        TRACE(LOG_RX, logout("multicast address\n"));
    } else {
        /* Frame rejected by filter. */
        TRACE(LOG_RX, logout("unknown mac address\n"));
        //~ return -1;
    }

    rx_descriptor_t rx;
    uint32_t rxdp = op_reg_read(s, DP8381X_RXDP);
    cpu_physical_memory_read(rxdp, (uint8_t *) & rx, sizeof(rx));
    uint32_t rxlink = le32_to_cpu(rx.link);
    uint32_t cmdsts = le32_to_cpu(rx.cmdsts);
    uint32_t bufptr = le32_to_cpu(rx.bufptr);
    uint32_t length = (cmdsts & CMDSTS_SIZE);
    TRACE(LOG_RX, logout("rxdp 0x%08x, link 0x%08x, cmdsts 0x%08x, "
                         "bufptr 0x%08x, length %u\n",
                         rxdp, rxlink, cmdsts, bufptr, length));

    /* Linux subtracts 4 bytes for fcs, so we add it here. */
    size += 4;

    assert(bufptr != 0);
    assert(length >= size);
    if (cmdsts & CMDSTS_OWN) {
        logout("wrong owner flag for receive buffer\n");
    }

    cpu_physical_memory_write(bufptr, buf, size);
    cmdsts &= ~CMDSTS_MORE;
    cmdsts &= ~CMDSTS_SIZE;
    cmdsts |= (size & CMDSTS_SIZE);
    cmdsts |= CMDSTS_OWN;
    cmdsts |= CMDSTS_OK;
    stl_le_phys(rxdp + 4, cmdsts);
    dp8381x_interrupt(s, ISR_RXOK);
    dp8381x_interrupt(s, ISR_RXDESC);
    if (rxlink == 0) {
        s->rx_state = idle;
        dp8381x_interrupt(s, ISR_RXIDLE);
    } else {
    }
        rxdp = rxlink;
        op_reg_write(s, DP8381X_RXDP, rxdp);
    //~ dp8381x_interrupt(s, ISR_RXOVR);
    return size;
}

static void dp8381x_transmit(dp8381x_t * s)
{
    uint8_t buffer[MAX_ETH_FRAME_SIZE + 4];
    uint32_t size = 0;
    tx_descriptor_t tx;
    uint32_t txdp = op_reg_read(s, DP8381X_TXDP);
    TRACE(LOG_TX, logout("txdp 0x%08x\n", txdp));
    while (txdp != 0) {
        cpu_physical_memory_read(txdp, (uint8_t *) & tx, sizeof(tx));
        uint32_t txlink = le32_to_cpu(tx.link);
        uint32_t cmdsts = le32_to_cpu(tx.cmdsts);
        uint32_t bufptr = le32_to_cpu(tx.bufptr);
        uint32_t length = (cmdsts & CMDSTS_SIZE);
        TRACE(LOG_TX, logout("txdp 0x%08x, link 0x%08x, cmdsts 0x%08x, "
                             "bufptr 0x%08x, length %u/%u\n",
                             txdp, txlink, cmdsts, bufptr, length, size));
        if (!(tx.cmdsts & CMDSTS_OWN)) {
            s->tx_state = idle;
            dp8381x_interrupt(s, ISR_TXIDLE);
            break;
        }
        assert(size + length < sizeof(buffer));
        cpu_physical_memory_read(bufptr, buffer + size, length);
        size += length;
        if (cmdsts & CMDSTS_INTR) {
            dp8381x_interrupt(s, ISR_TXDESC);
        }
        cmdsts &= ~CMDSTS_OWN;
        if (cmdsts & CMDSTS_MORE) {
            assert(txlink != 0);
            txdp = txlink;
            stl_le_phys(txdp + 4, cmdsts);
            continue;
        }
        cmdsts |= CMDSTS_OK;
        stl_le_phys(txdp + 4, cmdsts);
        dp8381x_interrupt(s, ISR_TXOK);
        TRACE(LOG_TX, logout("sending\n"));
        qemu_send_packet(&s->nic->nc, buffer, size);
        if (txlink == 0) {
            s->tx_state = idle;
            dp8381x_interrupt(s, ISR_TXIDLE);
            break;
        }
        txdp = txlink;
    }
    op_reg_write(s, DP8381X_TXDP, txdp);
}

/***********************************************************/
/* PCI DP8381X definitions */

typedef struct {
    PCIDevice dev;
    dp8381x_t dp8381x;
} pci_dp8381x_t;

#if defined(DEBUG_DP8381X)
static const char *regnames[] = {
    /* MAC/BIU Registers */
    "CR",                       /* 0x00 */
    "CFG",                      /* 0x04 */
    "MEAR",                     /* 0x08 */
    "PTSCR",                    /* 0x0c */
    "ISR",                      /* 0x10 */
    "IMR",                      /* 0x14 */
    "IER",                      /* 0x18 */
    "IHR",                      /* 0x1c */
    "TXDP",                     /* 0x20 */
    "TXCFG",                    /* 0x24 */
    "0x28",                     /* 0x28 */
    "0x2c",                     /* 0x2c */
    "RXDP",                     /* 0x30 */
    "RXCFG",                    /* 0x34 */
    "0x38",                     /* 0x38 */
    "CCSR",                     /* 0x3c */
    "WCSR",                     /* 0x40 */
    "PCR",                      /* 0x44 */
    "RFCR",                     /* 0x48 */
    "RFDR",                     /* 0x4c */
    "BRAR",                     /* 0x50 */
    "BRDR",                     /* 0x54 */
    "SRR",                      /* 0x58 */
    "MIBC",                     /* 0x5c */
    "MIB0",                     /* 0x60 */
    "MIB1",                     /* 0x64 */
    "MIB2",                     /* 0x68 */
    "MIB3",                     /* 0x6c */
    "MIB4",                     /* 0x70 */
    "MIB5",                     /* 0x74 */
    "MIB6",                     /* 0x78 */
    "0x7c",                     /* 0x7c */
    /* Internal Phy Registers */
    "BMCR",                     /* 0x80 */
    "BMSR",                     /* 0x84 */
    "PHYIDR1",                  /* 0x88 */
    "PHYIDR2",                  /* 0x8c */
    "ANAR",                     /* 0x90 */
    "ANLPAR",                   /* 0x94 */
    "ANER",                     /* 0x98 */
    "ANNPTR",                   /* 0x9c */
    "0xa0",                     /* 0xa0 */
    "0xa4",                     /* 0xa4 */
    "0xa8",                     /* 0xa8 */
    "0xac",                     /* 0xac */
    "0xb0",                     /* 0xb0 */
    "0xb4",                     /* 0xb4 */
    "0xb8",                     /* 0xb8 */
    "0xbc",                     /* 0xbc */
    "PHYSTS",                   /* 0xc0 */
    "MICR",                     /* 0xc4 */
    "MISR",                     /* 0xc8 */
    "PGSEL",                    /* 0xcc */
    "FCSCR",                    /* 0xd0 */
    "RECR",                     /* 0xd4 */
    "PCSR",                     /* 0xd8 */
    "0xdc",                     /* 0xdc */
    "0xe0",                     /* 0xe0 */
    "PHYCR",                    /* 0xe4 */
    //~ PMDCSR = 0xE4,
    "TBTSCR",                   /* 0xe8 */
    "0xec",                     /* 0xec */
    "0xf0",                     /* 0xf0 */
    "DSPCFG",                   /* 0xf4 */
    "SDCFG",                    /* 0xf8 */
    "TSTDAT",                   /* 0xfc */
};

#define num_elements(s) (sizeof(s) / sizeof(*s))

static const char *dp8381x_regname(unsigned addr)
{
    static char name[10];
    const char *p = name;
    if (addr < (num_elements(regnames) * 4) && (addr & 3) == 0) {
        p = regnames[addr / 4];
    } else {
        snprintf(name, sizeof(name), "0x%04x", addr);
    }
    return p;
}
#endif /* DEBUG_DP8381X */

static uint16_t anar_read(dp8381x_t * s)
{
    /* Read operational register 0x90. */
    uint16_t val = phy_reg_read(s, DP8381X_ANAR);
    logout("addr=%s val=0x%04x\n", dp8381x_regname(DP8381X_ANAR), val);
    return val;
}

static uint16_t anlpar_read(dp8381x_t * s)
{
    /* Read operational register 0x94. */
    uint16_t val = phy_reg_read(s, DP8381X_ANLPAR);
#if 1
    val |= BIT(14) | BITS(8, 5);
#endif
    logout("addr=%s val=0x%04x\n", dp8381x_regname(DP8381X_ANLPAR), val);
    return val;
}

static uint16_t bmcr_read(dp8381x_t * s)
{
    const uint32_t addr = DP8381X_BMCR;
    uint16_t val = phy_reg_read(s, addr);
    if (val & BIT(9)) {
        /* TODO: Restart auto-negotiation. */
        phy_reg_write(s, addr, val & ~BIT(9));
#if 1
        dp8381x_interrupt(s, ISR_PHY);
#endif
    }
    logout("addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
    return val;
}

static uint16_t phytst_read(dp8381x_t * s)
{
    /* TODO: reading RECR clears BIT(13). */
    /* TODO: BIT(12) duplicates TBTSCR_BIT4. */
    /* TODO: reading TBTSCR clear BIT(12). */
    /* TODO: reading FCSCR clears BIT(11). */
    /* TODO: BIT(8) duplicates ANER_BIT(page received). */
    /* TODO: reading ANER clears BIT(8). */
    /* TODO: BIT(0) duplicates BMSR_BIT(link status). */
    const uint32_t addr = DP8381X_PHYSTS;
    uint16_t val = phy_reg_read(s, addr);
    uint16_t newval;
    /* Auto-negotiation complete, full duplex, valid link. */
    val |= (BIT(4) | BIT(2) | BIT(0));
    newval = (val & ~BIT(7));
    phy_reg_write(s, addr, newval);
    logout("addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
    return val;
}

static void micr_write(dp8381x_t * s, uint16_t val)
{
    const uint32_t addr = DP8381X_MICR;
    logout("addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
    if (val & MICR_INTEN) {
        /* Enable PHY interrupt. In emulation, we immediately raise one. */
        uint16_t misr = phy_reg_read(s, DP8381X_MISR);
        misr |= MISR_MINT;
        phy_reg_write(s, DP8381X_MISR, misr);
        dp8381x_interrupt(s, ISR_PHY);
    }
    phy_reg_write(s, addr, val);
}

static uint8_t dp8381x_readb(pci_dp8381x_t * d, target_phys_addr_t addr)
{
    dp8381x_t *s = &d->dp8381x;
    uint8_t val = 0xff;
    if (0) {
    } else if (addr == DP8381X_MEAR) {  /* 0x08 */
        /* Needed for Windows. */
        val = op_reg_read(s, addr);
#if defined(CONFIG_EEPROM)
        val &= ~MEAR_EEDO;
        if (eeprom93xx_read(s->eeprom)) {
            val |= MEAR_EEDO;
        }
#else
        val |= MEAR_EEDO;
#endif
        TRACE(LOG_EEPROM,
              logout("addr=%s val=0x%02x\n", dp8381x_regname(addr), val));
    } else if (addr == DP8381X_PHYSTS) {        /* 0xc0 */
        /* Needed for Windows. */
        val = phytst_read(s);
        //~ logging = 0;
    } else if (addr >= 256) {
        logout("??? address too large, addr=%s\n", dp8381x_regname(addr));
        missing("byte access");
    } else {
        val = s->mem[addr];
        logout("??? addr=%s val=0x%02x\n", dp8381x_regname(addr), val);
        missing("byte access");
    }
    return val;
}

static uint16_t dp8381x_readw(pci_dp8381x_t * d, target_phys_addr_t addr)
{
    dp8381x_t *s = &d->dp8381x;
    uint16_t val = 0xffff;
    int logging = 1;
    if ((addr & 1) != 0) {
        logout("??? address not on word boundary, addr=%s\n",
               dp8381x_regname(addr));
        logging = 0;
    } else if (addr == DP8381X_RFDR) {  /* 0x4c */
        uint32_t rfaddr = (op_reg_read(s, DP8381X_RFCR) & RFCR_RFADDR);
        if (rfaddr & 1) {
            missing("odd rfaddr");
        } else {
            assert(rfaddr < sizeof(s->filter));
            val = *(uint16_t *) & s->filter[rfaddr];
        }
    } else if (addr < 0x80) {
        logout("??? addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
        logging = 0;
    } else if (addr >= 256) {
        logout("??? address too large, addr=%s\n", dp8381x_regname(addr));
        logging = 0;
    } else if (addr == DP8381X_BMCR) {          /* 0x80 */
        val = bmcr_read(s);
        logging = 0;
    } else if (addr == DP8381X_BMSR) {          /* 0x84 */
        val = phy_reg_read(s, addr);
#if 1
        val |= BIT(5) | BIT(2);
#endif
    } else if (addr == DP8381X_PHYIDR1) {       /* 0x88 */
        val = phy_reg_read(s, addr);
    } else if (addr == DP8381X_PHYIDR2) {       /* 0x8c */
        val = phy_reg_read(s, addr);
    } else if (addr == DP8381X_ANAR) {          /* 0x90 */
        val = anar_read(s);
        logging = 0;
    } else if (addr == DP8381X_ANLPAR) {        /* 0x94 */
        val = anlpar_read(s);
        logging = 0;
    } else if (addr == DP8381X_PHYSTS) {        /* 0xc0 */
        val = phytst_read(s);
        logging = 0;
    } else if (addr == DP8381X_MISR) {          /* 0xc8 */
        val = phy_reg_read(s, addr);
        phy_reg_write(s, addr, val & ~MISR_MINT);
    } else if (addr == DP8381X_DSPCFG) {        /* 0xf4 */
        val = phy_reg_read(s, addr);
    } else {
        val = phy_reg_read(s, addr);
        logout("??? addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
        logging = 0;
    }
    if (logging) {
        logout("addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
    }
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap16s(&val);
#endif
    return val;
}

static uint32_t dp8381x_readl(pci_dp8381x_t * d, target_phys_addr_t addr)
{
    dp8381x_t *s = &d->dp8381x;
    uint32_t val = 0xffffffffU;
    int logging = 1;
    if ((addr & 3) != 0) {
        logout("??? address not on double word boundary, addr=%s\n",
               dp8381x_regname(addr));
        logging = 0;
    } else if (addr >= 256) {
        logout("??? address too large, addr=%s\n", dp8381x_regname(addr));
    } else if (addr == DP8381X_CR) {    /* 0x00 */
        val = op_reg_read(s, addr);
    } else if (addr == DP8381X_CFG) {   /* 0x04 */
        val = op_reg_read(s, addr);
        val |= CFG_LNKSTS;
        if (val & 0x8000) {
            val |= CFG_SPEED100;
            val |= CFG_FDUP;
        }
#if 1
        val |= (CFG_SPEED100 | CFG_FDUP | CFG_ANEG_DN);
#endif
        //~ logging = 0;
    } else if (addr == DP8381X_MEAR) {  /* 0x08 */
        val = op_reg_read(s, addr);
#if defined(CONFIG_EEPROM)
        val &= ~MEAR_EEDO;
        if (eeprom93xx_read(s->eeprom)) {
            val |= MEAR_EEDO;
        }
#else
        val |= MEAR_EEDO;
#endif
        logging = LOG_EEPROM;
    } else if (addr == DP8381X_PTSCR) { /* 0x0c */
        /* TODO: emulate timing. */
        uint32_t newval = val = op_reg_read(s, addr);
        if (val & PTSCR_RBIST_EN) {
            newval |= PTSCR_RBIST_DONE;
        }
        if (val & PTSCR_EELOAD_EN) {
            /* EEPROM load takes 1500 us. */
            newval &= ~PTSCR_EELOAD_EN;
        }
        if (val & PTSCR_EEBIST_EN) {
            newval &= ~PTSCR_EEBIST_EN;
        }
        op_reg_write(s, addr, newval);
        //~ logging = 0;
    } else if (addr == DP8381X_ISR) {   /* 0x10 */
        val = op_reg_read(s, addr);
        dp8381x_interrupt(s, ISR_CLEAR);
    } else if (addr == DP8381X_IER) {   /* 0x18 */
        val = op_reg_read(s, addr);
    } else if (addr == DP8381X_CCSR) {  /* 0x3c */
        val = op_reg_read(s, addr);
    } else if (addr == DP8381X_WCSR) {  /* 0x40 */
        /* TODO: set bits on arp, unicast, wake-on-lan and other packets */
        val = op_reg_read(s, addr);
        //~ logging = 0;
    } else if (addr == DP8381X_RFCR) {  /* 0x48 */
        val = op_reg_read(s, addr);
        //~ logging = 0;
        //~ } else if (addr == DP8381X_RFDR) {      /* 0x4c */
        //~ val = op_reg_read(s, addr);
    } else if (addr == DP8381X_SRR) {   /* 0x58 */
        val = op_reg_read(s, addr);
    } else if (addr >= DP8381X_MIB0 && addr <= DP8381X_MIB6) {  /* 0x60 ... 0x78 */
        /* TODO: statistics counters. */
        val = op_reg_read(s, addr);
    /* TODO: check following cases for big endian target. */
    } else if (addr == DP8381X_BMCR) {  /* 0x80 */
        val = bmcr_read(s);
        logging = 0;
    } else if (addr == DP8381X_BMSR) {  /* 0x84 */
        val = dp8381x_readw(d, addr);
        logging = 0;
    } else if (addr == DP8381X_ANAR) {  /* 0x90 */
        /* Needed for Windows. */
        val = anar_read(s);
        logging = 0;
    } else if (addr == DP8381X_ANLPAR) {        /* 0x94 */
        /* Needed for Windows. */
        val = anlpar_read(s);
        logging = 0;
    } else if (addr == DP8381X_PHYSTS) {        /* 0xc0 */
        /* Needed for Windows. */
        val = phytst_read(s);
        logging = 0;
    } else {
        val = op_reg_read(s, addr);
        logging = 0;
        logout("??? addr=%s val=0x%08x\n", dp8381x_regname(addr), val);
    }
    if (logging) {
        logout("addr=%s val=0x%08x\n", dp8381x_regname(addr), val);
    }
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap32s(&val);
#endif
    return val;
}

static void QEMU_NORETURN dp8381x_writeb(pci_dp8381x_t * d,
                                         target_phys_addr_t addr, uint8_t val);
static void QEMU_NORETURN dp8381x_ioport_writeb(void *opaque,
                                                uint32_t addr, uint32_t val);
static void QEMU_NORETURN dp8381x_mmio_writeb(void *opaque,
                                              target_phys_addr_t addr,
                                              uint32_t val);

static void dp8381x_writeb(pci_dp8381x_t * d, target_phys_addr_t addr,
                           uint8_t val)
{
    if (0) {
    } else if (addr >= 256) {
        logout("??? address too large, addr=%s val=0x%08x\n",
               dp8381x_regname(addr), val);
    } else {
        logout("??? addr=%s val=0x%02x\n", dp8381x_regname(addr), val);
    }
    missing("byte access");
}

static void dp8381x_writew(pci_dp8381x_t * d, target_phys_addr_t addr,
                           uint16_t val)
{
    dp8381x_t *s = &d->dp8381x;
    int logging = 1;
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap16s(&val);
#endif
    if ((addr & 1) != 0) {
        logout("??? address not on word boundary, addr=%s val=0x%08x\n",
               dp8381x_regname(addr), val);
    } else if (addr == DP8381X_RFDR) {  /* 0x4c */
        uint32_t rfaddr = (op_reg_read(s, DP8381X_RFCR) & RFCR_RFADDR);
        if (rfaddr & 1) {
            missing("odd rfaddr");
        } else {
            assert(rfaddr < sizeof(s->filter));
            *(uint16_t *) & s->filter[rfaddr] = val;
        }
        //~ op_reg_write(s, addr, val);
    } else if (addr < 0x80) {
        logout("??? addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
        logging = 0;
    } else if (addr >= 256) {
        logout("??? address too large, addr=%s val=0x%08x\n",
               dp8381x_regname(addr), val);
        logging = 0;
    } else if (addr == DP8381X_BMCR) {  /* 0x80 */
        if (val & BIT(15)) {
            /* Reset PHY. */
            logout("reset PHY\n");
            val &= ~BIT(15);
        }
        phy_reg_write(s, addr, val);
        logging = 0;
    } else if (addr == DP8381X_MICR) {  /* 0xc4 */
        micr_write(s, val);
        logging = 0;
    } else if (addr == DP8381X_PGSEL) { /* 0xcc */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_PHYCR) { /* 0xe4 */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_DSPCFG) {        /* 0xf4 */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_SDCFG) { /* 0xf8 */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_TSTDAT) {        /* 0xfc */
        phy_reg_write(s, addr, val);
    } else {
        logout("??? addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
        phy_reg_write(s, addr, val);
        logging = 0;
    }
    if (logging) {
        logout("addr=%s val=0x%08x\n", dp8381x_regname(addr), val);
    }
}

static void dp8381x_writel(pci_dp8381x_t * d, target_phys_addr_t addr,
                           uint32_t val)
{
    dp8381x_t *s = &d->dp8381x;
    int logging = 1;
#if defined(TARGET_WORDS_BIGENDIAN)
    bswap32s(&val);
#endif
    if ((addr & 3) != 0) {
        logout("??? address not on double word boundary, addr=%s val=0x%08x\n",
               dp8381x_regname(addr), val);
        logging = 0;
    } else if (addr >= 256) {
        logout("??? address too large, addr=%s val=0x%08x\n",
               dp8381x_regname(addr), val);
        logging = 0;
    } else if (addr == DP8381X_CR) {    /* 0x00 */
        if (val & CR_RST) {
            dp8381x_reset(s);
        } else {
            if (val & CR_SWI) {
                dp8381x_interrupt(s, ISR_SWI);
            }
            if (val & CR_RXR) {
                s->rx_state = idle;
            }
            if (val & CR_TXR) {
                s->tx_state = idle;
            }
            if (val & CR_RXD) {
                val &= ~CR_RXE;
                s->rx_state = idle;
            } else if (val & CR_RXE) {
                s->rx_state = active;
                /* TODO: handle queued receive data. */
            }
            if (val & CR_TXD) {
                val &= ~CR_TXE;
                s->tx_state = idle;
            } else if (val & CR_TXE) {
                s->tx_state = active;
                dp8381x_transmit(s);
            }
            val &= ~(CR_RXR | CR_TXR | CR_RXD | CR_TXD);
            op_reg_write(s, addr, val);
        }
    } else if (addr == DP8381X_CFG) {   /* 0x04 */
        if (val & CFG_BEM) {
            missing("big endian mode");
        }
        val &= ~(CFG_LNKSTS | CFG_SPEED100 | CFG_FDUP | CFG_POL);
        if (val & BIT(13)) {
            /* Auto-negotiation enabled. */
            val |= CFG_ANEG_DN;
#if 0
            dp8381x_interrupt(s, ISR_PHY);
#endif
        }
        op_reg_write(s, addr, val);
    } else if (addr == DP8381X_MEAR) {  /* 0x08 */
#if defined(CONFIG_EEPROM)
        int eecs = ((val & MEAR_EESEL) != 0);
        int eesk = ((val & MEAR_EECLK) != 0);
        int eedi = ((val & MEAR_EEDI) != 0);
        eeprom93xx_write(s->eeprom, eecs, eesk, eedi);
#endif
        op_reg_write(s, addr, val);
        if (val & 0x000000f0) {
            missing("MII access");
        }
    } else if (addr == DP8381X_PTSCR) { /* 0x0c */
        if (val & PTSCR_EELOAD_EN) {
            val &= ~PTSCR_EELOAD_EN;
        }
        if (val != 0) {
            missing("test control");
        }
        op_reg_write(s, addr, val);
        logging = LOG_EEPROM;
    } else if (addr == DP8381X_IMR) {   /* 0x14 */
        op_reg_write(s, addr, val);
        dp8381x_interrupt(s, ISR_UPDATE);
    } else if (addr == DP8381X_IER) {   /* 0x18 */
        op_reg_write(s, addr, val);
        dp8381x_interrupt(s, ISR_UPDATE);
    } else if (addr == DP8381X_TXDP) {  /* 0x20 */
        /* Transmit descriptor must be lword aligned. */
        assert(!(val & 3));
        op_reg_write(s, addr, val);
        //~ TODO: Clear CTDD.
        //~ s->txdp = val;
    } else if (addr == DP8381X_TXCFG) { /* 0x24 */
        /* TODO. */
        op_reg_write(s, addr, val);
    } else if (addr == DP8381X_RXDP) {  /* 0x30 */
        /* Receive descriptor must be lword aligned. */
        assert(!(val & 3));
        op_reg_write(s, addr, val);
        //~ s->rxdp = val;
    } else if (addr == DP8381X_RXCFG) { /* 0x34 */
        /* TODO: set flags for receive. */
        op_reg_write(s, addr, val);
    } else if (addr == DP8381X_CCSR) {  /* 0x3c */
        /* TODO. */
        op_reg_write(s, addr, val);
    } else if (addr == DP8381X_WCSR) {  /* 0x40 */
        op_reg_write(s, addr, val);
        if (val != 0) {
            missing("wake on lan");
        }
    } else if (addr == DP8381X_PCR) {   /* 0x44 */
        val &= ~BIT(16);
        op_reg_write(s, addr, val);
    } else if (addr == DP8381X_RFCR) {  /* 0x48 */
        /* TODO: enable packet filters */
        op_reg_write(s, addr, val);
        /* RFCR_RFADDR must be even. */
        assert(!(val & 1));
    } else if (addr == DP8381X_RFDR) {  /* 0x4c */
        /* TODO. */
        uint32_t rfaddr = (op_reg_read(s, DP8381X_RFCR) & RFCR_RFADDR);
        if (rfaddr & 1) {
            missing("odd rfaddr");
        } else {
            assert(rfaddr < sizeof(s->filter));
            *(uint16_t *) & s->filter[rfaddr] = val;
        }
        //~ op_reg_write(s, addr, val);
    } else if (addr == DP8381X_MIBC) {  /* 0x5c */
        if (val & MIBC_MIBS) {
            val &= ~MIBC_MIBS;
            missing("MIB Counter Stroke");
        }
        if (val & MIBC_ACLR) {
            /* Clear all counters. */
            uint32_t offset;
            val &= ~MIBC_ACLR;
            for (offset = DP8381X_MIB0; offset <= DP8381X_MIB6; offset += 4) {
                op_reg_write(s, offset, 0);
            }
        }
        /* TODO: handle MIBC_WRN. */
        op_reg_write(s, addr, val);
    } else if (addr == DP8381X_MICR) {  /* 0xc4 */
        /* Needed for Windows. */
        micr_write(s, val);
        logging = 0;
    } else if (addr == DP8381X_MISR) {  /* 0xc8 */
        /* Needed for Windows. */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_PGSEL) { /* 0xcc */
        /* Needed for Windows. */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_PHYCR) { /* 0xe4 */
        /* Needed for Windows. */
        phy_reg_write(s, addr, val);
    } else if (addr == DP8381X_00EC) {  /* 0xec */
        /* Needed for Windows. */
        phy_reg_write(s, addr, val);
    } else {
        op_reg_write(s, addr, val);
        logout("??? addr=%s val=0x%08x\n", dp8381x_regname(addr), val);
        logging = 0;
    }
    if (logging) {
        logout("addr=%s val=0x%08x\n", dp8381x_regname(addr), val);
    }
}

/*****************************************************************************
 *
 * Port mapped I/O.
 *
 ****************************************************************************/

static uint32_t dp8381x_ioport_readb(void *opaque, uint32_t addr)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[0];
    logout("addr=%s\n", dp8381x_regname(addr));
    return dp8381x_readb(d, addr);
}

static uint32_t dp8381x_ioport_readw(void *opaque, uint32_t addr)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[0];
    logout("addr=%s\n", dp8381x_regname(addr));
    return dp8381x_readw(d, addr);
}

static uint32_t dp8381x_ioport_readl(void *opaque, uint32_t addr)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[0];
    logout("addr=%s\n", dp8381x_regname(addr));
    return dp8381x_readl(d, addr);
}

static void dp8381x_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[0];
    logout("addr=%s val=0x%02x\n", dp8381x_regname(addr), val);
    dp8381x_writeb(d, addr, val);
}

static void dp8381x_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[0];
    logout("addr=%s val=0x%04x\n", dp8381x_regname(addr), val);
    dp8381x_writew(d, addr, val);
}

static void dp8381x_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[0];
    logout("addr=%s val=0x%08x\n", dp8381x_regname(addr), val);
    dp8381x_writel(d, addr, val);
}

static void dp8381x_io_map(PCIDevice * pci_dev, int region_num,
                           pcibus_t addr, pcibus_t size, int type)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) pci_dev;
    dp8381x_t *s = &d->dp8381x;

    logout("region %d, addr 0x%08" FMT_PCIBUS ", size 0x%08" FMT_PCIBUS "\n",
           region_num, addr, size);
    assert(region_num == 0);
    s->region[region_num] = addr;

    register_ioport_read(addr, size, 1, dp8381x_ioport_readb, d);
    register_ioport_read(addr, size, 2, dp8381x_ioport_readw, d);
    register_ioport_read(addr, size, 4, dp8381x_ioport_readl, d);
    register_ioport_write(addr, size, 1, dp8381x_ioport_writeb, d);
    register_ioport_write(addr, size, 2, dp8381x_ioport_writew, d);
    register_ioport_write(addr, size, 4, dp8381x_ioport_writel, d);
}

/*****************************************************************************
 *
 * Memory mapped I/O.
 *
 ****************************************************************************/

static uint32_t dp8381x_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[1];
    logout("addr 0x" TARGET_FMT_plx "\n", addr);
    return dp8381x_readb(d, addr);
}

static uint32_t dp8381x_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[1];
    logout("addr 0x" TARGET_FMT_plx "\n", addr);
    return dp8381x_readw(d, addr);
}

static uint32_t dp8381x_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[1];
    logout("addr 0x" TARGET_FMT_plx "\n", addr);
    return dp8381x_readl(d, addr);
}

static void dp8381x_mmio_writeb(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[1];
    logout("addr 0x" TARGET_FMT_plx "\n", addr);
    dp8381x_writeb(d, addr, val);
}

static void dp8381x_mmio_writew(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[1];
    logout("addr 0x" TARGET_FMT_plx "\n", addr);
    dp8381x_writew(d, addr, val);
}

static void dp8381x_mmio_writel(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    dp8381x_t *s = &d->dp8381x;
    addr -= s->region[1];
    logout("addr 0x" TARGET_FMT_plx "\n", addr);
    dp8381x_writel(d, addr, val);
}

static void dp8381x_mem_map(PCIDevice * pci_dev, int region_num,
                            pcibus_t addr, pcibus_t size, int type)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) pci_dev;
    dp8381x_t *s = &d->dp8381x;

    logout("region %d, addr 0x%08" FMT_PCIBUS ", size 0x%08" FMT_PCIBUS "\n",
           region_num, addr, size);
    assert(region_num == 1);
    s->region[region_num] = addr;

    cpu_register_physical_memory(addr, DP8381X_MEM_SIZE, s->io_memory);
}

static CPUReadMemoryFunc *dp8381x_mmio_read[] = {
    dp8381x_mmio_readb,
    dp8381x_mmio_readw,
    dp8381x_mmio_readl
};

static CPUWriteMemoryFunc *dp8381x_mmio_write[] = {
    dp8381x_mmio_writeb,
    dp8381x_mmio_writew,
    dp8381x_mmio_writel
};

static void nic_cleanup(VLANClientState *vc)
{
    pci_dp8381x_t *d = DO_UPCAST(NICState, nc, vc)->opaque;

    unregister_savevm("dp8381x", d);

#if 0
    qemu_del_timer(d->poll_timer);
    qemu_free_timer(d->poll_timer);
#endif
}

static int dp8381x_load(QEMUFile * f, void *opaque, int version_id)
{
    pci_dp8381x_t *d = opaque;
    int result = 0;
    logout("\n");
    if (version_id == dp8381x_version) {
        result = pci_device_load(&d->dev, f);
    } else {
        result = -EINVAL;
    }
    return result;
}

static void nic_reset(void *opaque)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
    logout("%p\n", d);
}

static void dp8381x_save(QEMUFile * f, void *opaque)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *) opaque;
#if 0
    dp8381x_t *s = &d->dp8381x;
#endif
    logout("\n");
    pci_device_save(&d->dev, f);
    /* TODO: support different endianness */
    qemu_put_buffer(f, (uint8_t *) d, sizeof(*d));
}

#if defined(CONFIG_EEPROM)
/* SWAP_BITS is needed for buggy Linux driver. */
#define SWAP_BITS(x)	( (((x) & 0x0001) << 15) | (((x) & 0x0002) << 13) \
			| (((x) & 0x0004) << 11) | (((x) & 0x0008) << 9)  \
			| (((x) & 0x0010) << 7)  | (((x) & 0x0020) << 5)  \
			| (((x) & 0x0040) << 3)  | (((x) & 0x0080) << 1)  \
			| (((x) & 0x0100) >> 1)  | (((x) & 0x0200) >> 3)  \
			| (((x) & 0x0400) >> 5)  | (((x) & 0x0800) >> 7)  \
			| (((x) & 0x1000) >> 9)  | (((x) & 0x2000) >> 11) \
			| (((x) & 0x4000) >> 13) | (((x) & 0x8000) >> 15) )

static void eeprom_init(dp8381x_t * s)
{
#if 0
    uint8_t *pci_conf = s->pci_dev->config;
#endif
    uint8_t i;
    uint16_t *eeprom_contents = eeprom93xx_data(s->eeprom);

    logout("\n");

    memcpy(eeprom_contents, eeprom_default, sizeof(eeprom_default));

    /* Patch MAC address into EEPROM data. */
    eeprom_contents[6] =
        (eeprom_contents[6] & 0x7fff) + ((s->conf.macaddr.a[0] & 1) << 15);
    eeprom_contents[7] =
        (s->conf.macaddr.a[0] >> 1) + (s->conf.macaddr.a[1] << 7) +
        ((s->conf.macaddr.a[2] & 1) << 15);
    eeprom_contents[8] =
        (s->conf.macaddr.a[2] >> 1) + (s->conf.macaddr.a[3] << 7) +
        ((s->conf.macaddr.a[4] & 1) << 15);
    eeprom_contents[9] =
        (s->conf.macaddr.a[4] >> 1) + (s->conf.macaddr.a[5] << 7) +
        (eeprom_contents[9] & 0x8000);

    /* The Linux driver natsemi.c is buggy because it reads the bits from
     * EEPROM in wrong order (low to high). So we must reverse the bit order
     * to get the correct mac address. */
    for (i = 6; i < 10; i++) {
        eeprom_contents[i] = SWAP_BITS(eeprom_contents[i]);
    }

    /* Fix EEPROM checksum. */
    uint8_t sum = 0;
    for (i = 0; i < 11; i++) {
        sum += (eeprom_contents[i] & 255);
        sum += (eeprom_contents[i] >> 8);
    }
    sum += 0x55;
    sum = -sum;
    eeprom_contents[i] = (sum << 8) + 0x55;

#if 0
    // EEPROM Bit 20 NCPEN!!!
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM |
                              PCI_STATUS_FAST_BACK | PCI_STATUS_CAP_LIST);
    // EEPROM Bits 16...31!!!
    // TODO Split using PCI_CONFIG8.
    pci_set_long(pci_conf + PCI_INTERRUPT_LINE, 0x340b0100);    // MNGNT = 11, MXLAT = 52, IPIN = 0
    // EEPROM Bits 31...27, 21!!!
    pci_set_long(pci_conf + 0x40, 0xff820001);    /* Power Management Capabilities */
    // EEPROM Bit 8!!!
    pci_set_long(pci_conf + 0x44, 0x00000000);    /* Power Management Control and Status */

    // EEPROM Bits 16, 15-13!!!
    OP_REG(DP8381X_CFG, 0x00000000);    /* Configuration and Media Status */
#endif
}
#endif

static NetClientInfo net_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = nic_can_receive,
    .receive = nic_receive,
    .cleanup = nic_cleanup,
};

static int pci_dp8381x_init(PCIDevice *pci_dev, uint32_t silicon_revision)
{
    pci_dp8381x_t *d = (pci_dp8381x_t *)pci_dev;
    dp8381x_t *s = &d->dp8381x;
    uint8_t *pci_conf = pci_dev->config;

    logout("silicon revision = 0x%08x\n", silicon_revision);

    /* National Semiconductor DP83815, DP83816 */
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_NS);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_NS_83815);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM |
                              PCI_STATUS_FAST_BACK | PCI_STATUS_CAP_LIST);
    /* ethernet network controller */
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);
    /* Address registers are set by pci_register_bar. */
    /* Capabilities Pointer, CLOFS */
    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x00000040);
    /* 0x38 reserved, returns 0 */
    /* MNGNT = 11, MXLAT = 52, IPIN = 0 */
    // TODO Split using PCI_CONFIG8.
    pci_set_long(pci_conf + PCI_INTERRUPT_LINE, 0x340b0100);
    /* Power Management Capabilities */
    pci_set_long(pci_conf + 0x40, 0xff820001);
    /* Power Management Control and Status */
    //~ pci_set_long(pci_conf + 0x44, 0x00000000);
    /* 0x48...0xff reserved, returns 0 */

    s->silicon_revision = silicon_revision;

    /* Handler for memory-mapped I/O */
    s->io_memory =
        cpu_register_io_memory(dp8381x_mmio_read, dp8381x_mmio_write, d);

    logout("io_memory = 0x%08x\n", s->io_memory);

    pci_register_bar(&d->dev, 0, DP8381X_IO_SIZE,
                     PCI_BASE_ADDRESS_SPACE_IO, dp8381x_io_map);
    pci_register_bar(&d->dev, 1, DP8381X_MEM_SIZE,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, dp8381x_mem_map);

    s->pci_dev = &d->dev;
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    dp8381x_reset(s);

#if defined(CONFIG_EEPROM)
    /* Add EEPROM (16 x 16 bit). */
    s->eeprom = eeprom93xx_new(EEPROM_SIZE);
    eeprom_init(s);
#endif

    s->nic = qemu_new_nic(&net_info, &s->conf,
                          pci_dev->qdev.info->name, pci_dev->qdev.id, s);

    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);

    qemu_register_reset(nic_reset, d);

    // TODO: use &s->nic->nc->model or d->name instead of "dp8381x".
    register_savevm("dp8381x", dp8381x_instance, dp8381x_version,
                    dp8381x_save, dp8381x_load, d);

    return 0;
}

static int dp8381x_init(PCIDevice *pci_dev)
{
    logout("\n");
#if defined(DP83815)
    return pci_dp8381x_init(pci_dev, DP83815DVNG);
#else
    return pci_dp8381x_init(pci_dev, DP83816AVNG);
#endif
}

#if defined(DP83815)
static PCIDeviceInfo dp8381x_info = {
    .qdev.name = "dp83815",
    .qdev.desc = "National Semiconductor DP83815",
    .qdev.size = sizeof(dp8381x_t),
    .init      = dp8381x_init,
};
#else
static PCIDeviceInfo dp8381x_info = {
    .qdev.name = "dp83816",
    .qdev.desc = "National Semiconductor DP83816",
    .qdev.size = sizeof(dp8381x_t),
    .init      = dp8381x_init,
};
#endif

static void dp8381x_register_devices(void)
{
    pci_qdev_register(&dp8381x_info);
}

device_init(dp8381x_register_devices)

/* eof */
