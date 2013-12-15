/*
 * SMSC 91C111 Ethernet interface emulation
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL
 */

#include "hw/sysbus.h"
#include "net/net.h"
#include "hw/devices.h"
/* For crc32 */
#include <zlib.h>

/* Number of 2k memory pages available.  */
#define NUM_PACKETS 4

#if 0
# define logout(fmt, ...) \
    fprintf(stderr, "SMC91C111\t%-24s %s:%u " fmt, __func__, __FILE__, __LINE__, ##__VA_ARGS__)
#else
# define logout(fmt, ...) (void)0
#endif

typedef enum {
    MII_WAITING,
    MII_IDLE,
    MII_GOT_START,
    MII_READ,
    MII_WRITE
} MII_State;

typedef enum {
    MII_MDOE = 8,
    MII_MCLK = 4,
    MII_MDI = 2,
    MII_MDO = 1
} MII_BITS;

/* PHY Register Addresses (LAN91C111 Internal PHY) */

/* PHY Configuration Register 1 */
#define PHY_CFG1_REG		0x10
#define PHY_CFG1_LNKDIS		0x8000	/* 1=Rx Link Detect Function disabled */
#define PHY_CFG1_XMTDIS		0x4000	/* 1=TP Transmitter Disabled */
#define PHY_CFG1_XMTPDN		0x2000	/* 1=TP Transmitter Powered Down */
#define PHY_CFG1_BYPSCR		0x0400	/* 1=Bypass scrambler/descrambler */
#define PHY_CFG1_UNSCDS		0x0200	/* 1=Unscramble Idle Reception Disable */
#define PHY_CFG1_EQLZR		0x0100	/* 1=Rx Equalizer Disabled */
#define PHY_CFG1_CABLE		0x0080	/* 1=STP(150ohm), 0=UTP(100ohm) */
#define PHY_CFG1_RLVL0		0x0040	/* 1=Rx Squelch level reduced by 4.5db */
#define PHY_CFG1_TLVL_SHIFT	2	/* Transmit Output Level Adjust */
#define PHY_CFG1_TLVL_MASK	0x003C
#define PHY_CFG1_TRF_MASK	0x0003	/* Transmitter Rise/Fall time */

/* PHY Configuration Register 2 */
#define PHY_CFG2_REG		0x11
#define PHY_CFG2_APOLDIS	0x0020	/* 1=Auto Polarity Correction disabled */
#define PHY_CFG2_JABDIS		0x0010	/* 1=Jabber disabled */
#define PHY_CFG2_MREG		0x0008	/* 1=Multiple register access (MII mgt) */
#define PHY_CFG2_INTMDIO	0x0004	/* 1=Interrupt signaled with MDIO pulseo */

/* PHY Status Output (and Interrupt status) Register */
#define PHY_INT_REG		0x12	/* Status Output (Interrupt Status) */
#define PHY_INT_INT		0x8000	/* 1=bits have changed since last read */
#define PHY_INT_LNKFAIL		0x4000	/* 1=Link Not detected */
#define PHY_INT_LOSSSYNC	0x2000	/* 1=Descrambler has lost sync */
#define PHY_INT_CWRD		0x1000	/* 1=Invalid 4B5B code detected on rx */
#define PHY_INT_SSD		0x0800	/* 1=No Start Of Stream detected on rx */
#define PHY_INT_ESD		0x0400	/* 1=No End Of Stream detected on rx */
#define PHY_INT_RPOL		0x0200	/* 1=Reverse Polarity detected */
#define PHY_INT_JAB		0x0100	/* 1=Jabber detected */
#define PHY_INT_SPDDET		0x0080	/* 1=100Base-TX mode, 0=10Base-T mode */
#define PHY_INT_DPLXDET		0x0040	/* 1=Device in Full Duplex */

/* PHY Interrupt/Status Mask Register */
#define PHY_MASK_REG		0x13	/* Interrupt Mask */
/* Uses the same bit definitions as PHY_INT_REG */

typedef struct {
    int mdoe;
    int mclk;
    int mdi;
    int mdo;
    uint32_t data;
    MII_State status;
    unsigned reg;
    uint16_t regs[32];
} MII;


/* Declarations from include/linux/mii.h. */

/* Generic MII registers. */

#define MII_BMCR            0x00        /* Basic mode control register */ // rw
#define MII_BMSR            0x01        /* Basic mode status register  */ // r
#define MII_PHYSID1         0x02        /* PHYS ID 1                   */
#define MII_PHYSID2         0x03        /* PHYS ID 2                   */
#define MII_ADVERTISE       0x04        /* Advertisement control reg   */ // rw
#define MII_LPA             0x05        /* Link partner ability reg    */
#define MII_EXPANSION       0x06        /* Expansion register          */
#define MII_CTRL1000        0x09        /* 1000BASE-T control          */
#define MII_STAT1000        0x0a        /* 1000BASE-T status           */
#define MII_ESTATUS         0x0f        /* Extended Status             */
#define MII_DCOUNTER        0x12        /* Disconnect counter          */
#define MII_FCSCOUNTER      0x13        /* False carrier counter       */
#define MII_NWAYTEST        0x14        /* N-way auto-neg test reg     */
#define MII_RERRCOUNTER     0x15        /* Receive error counter       */
#define MII_SREVISION       0x16        /* Silicon revision            */
#define MII_RESV1           0x17        /* Reserved...                 */
#define MII_LBRERROR        0x18        /* Lpback, rx, bypass error    */
#define MII_PHYADDR         0x19        /* PHY address                 */
#define MII_RESV2           0x1a        /* Reserved...                 */
#define MII_TPISTATUS       0x1b        /* TPI status for 10mbps       */
#define MII_NCONFIG         0x1c        /* Network interface config    */

/* Basic mode control register. */
#define BMCR_RESV               0x003f  /* Unused...                   */
#define BMCR_SPEED1000          0x0040  /* MSB of Speed (1000)         */
#define BMCR_CTST               0x0080  /* Collision test              */
#define BMCR_FULLDPLX           0x0100  /* Full duplex                 */
#define BMCR_ANRESTART          0x0200  /* Auto negotiation restart    */
#define BMCR_ISOLATE            0x0400  /* Disconnect PHY from MII     */
#define BMCR_PDOWN              0x0800  /* Powerdown the               */
#define BMCR_ANENABLE           0x1000  /* Enable auto negotiation     */
#define BMCR_SPEED100           0x2000  /* Select 100Mbps              */
#define BMCR_LOOPBACK           0x4000  /* TXD loopback bits           */
#define BMCR_RESET              0x8000  /* Reset the PHY               */

/* Basic mode status register. */
#define BMSR_ERCAP              0x0001  /* Ext-reg capability          */
#define BMSR_JCD                0x0002  /* Jabber detected             */
#define BMSR_LSTATUS            0x0004  /* Link status                 */
#define BMSR_ANEGCAPABLE        0x0008  /* Able to do auto-negotiation */
#define BMSR_RFAULT             0x0010  /* Remote fault detected       */
#define BMSR_ANEGCOMPLETE       0x0020  /* Auto-negotiation complete   */
#define BMSR_RESV               0x00c0  /* Unused...                   */
#define BMSR_ESTATEN            0x0100  /* Extended Status in R15      */
#define BMSR_100HALF2           0x0200  /* Can do 100BASE-T2 HDX       */
#define BMSR_100FULL2           0x0400  /* Can do 100BASE-T2 FDX       */
#define BMSR_10HALF             0x0800  /* Can do 10mbps, half-duplex  */
#define BMSR_10FULL             0x1000  /* Can do 10mbps, full-duplex  */
#define BMSR_100HALF            0x2000  /* Can do 100mbps, half-duplex */
#define BMSR_100FULL            0x4000  /* Can do 100mbps, full-duplex */
#define BMSR_100BASE4           0x8000  /* Can do 100mbps, 4k packets  */

//~ #define PHY_STAT_CAP_SUPR	0x0040	/* 1=recv mgmt frames with not preamble */

/* Advertisement control register. */
#define ADVERTISE_SLCT          0x001f  /* Selector bits               */
#define ADVERTISE_CSMA          0x0001  /* Only selector supported     */
#define ADVERTISE_10HALF        0x0020  /* Try for 10mbps half-duplex  */
#define ADVERTISE_1000XFULL     0x0020  /* Try for 1000BASE-X full-duplex */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_1000XHALF     0x0040  /* Try for 1000BASE-X half-duplex */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_1000XPAUSE    0x0080  /* Try for 1000BASE-X pause    */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */
#define ADVERTISE_1000XPSE_ASYM 0x0100  /* Try for 1000BASE-X asym pause */
#define ADVERTISE_100BASE4      0x0200  /* Try for 100mbps 4k packets  */
#define ADVERTISE_PAUSE_CAP     0x0400  /* Try for pause               */
#define ADVERTISE_PAUSE_ASYM    0x0800  /* Try for asymetric pause     */
#define ADVERTISE_RESV          0x1000  /* Unused...                   */
#define ADVERTISE_RFAULT        0x2000  /* Say we can detect faults    */
#define ADVERTISE_LPACK         0x4000  /* Ack link partners response  */
#define ADVERTISE_NPAGE         0x8000  /* Next page bit               */

#define ADVERTISE_FULL (ADVERTISE_100FULL | ADVERTISE_10FULL | \
                        ADVERTISE_CSMA)
#define ADVERTISE_ALL (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                       ADVERTISE_100HALF | ADVERTISE_100FULL)

/* Link partner ability register. */
#define LPA_SLCT                0x001f  /* Same as advertise selector  */
#define LPA_10HALF              0x0020  /* Can do 10mbps half-duplex   */
#define LPA_1000XFULL           0x0020  /* Can do 1000BASE-X full-duplex */
#define LPA_10FULL              0x0040  /* Can do 10mbps full-duplex   */
#define LPA_1000XHALF           0x0040  /* Can do 1000BASE-X half-duplex */
#define LPA_100HALF             0x0080  /* Can do 100mbps half-duplex  */
#define LPA_1000XPAUSE          0x0080  /* Can do 1000BASE-X pause     */
#define LPA_100FULL             0x0100  /* Can do 100mbps full-duplex  */
#define LPA_1000XPAUSE_ASYM     0x0100  /* Can do 1000BASE-X pause asym*/
#define LPA_100BASE4            0x0200  /* Can do 100mbps 4k packets   */
#define LPA_PAUSE_CAP           0x0400  /* Can pause                   */
#define LPA_PAUSE_ASYM          0x0800  /* Can pause asymetrically     */
#define LPA_RESV                0x1000  /* Unused...                   */
#define LPA_RFAULT              0x2000  /* Link partner faulted        */
#define LPA_LPACK               0x4000  /* Link partner acked us       */
#define LPA_NPAGE               0x8000  /* Next page bit               */

#define LPA_DUPLEX              (LPA_10FULL | LPA_100FULL)
#define LPA_100                 (LPA_100FULL | LPA_100HALF | LPA_100BASE4)

/* End of declarations from include/linux/mii.h. */

/* SMC 91C111 specific MII register. */

/* 0x06 ... 0x0f are reserved. */
#define MII_CONFIGURATION1  0x10        /* Configuration 1             */
#define MII_CONFIGURATION2  0x11        /* Configuration 2             */
#define MII_STATUSOUTPUT    0x12        /* Status Output               */
    //~ PHY_FCSCR = 0x13,   /* Mask */
/* 0x14 is reserved. */
/* 0x15 ... 0x1f are unused. */


/* Default values for MII management registers (see IEEE 802.3-2008 22.2.4). */

static const uint16_t mii_regs_default[] = {
    [MII_BMCR] = BMCR_ANENABLE | BMCR_SPEED100,
    [MII_BMSR] = BMSR_100FULL | BMSR_100HALF | BMSR_10FULL | BMSR_10HALF |
                 BMSR_ANEGCAPABLE | BMSR_LSTATUS | BMSR_ERCAP,
    [MII_ADVERTISE] = ADVERTISE_PAUSE_CAP | ADVERTISE_ALL | ADVERTISE_CSMA,
};

#define TYPE_SMC91C111 "smc91c111"
#define SMC91C111(obj) OBJECT_CHECK(smc91c111_state, (obj), TYPE_SMC91C111)

typedef struct {
    SysBusDevice parent_obj;
    MII mii;
    NICState *nic;
    NICConf conf;
    uint16_t tcr;
    uint16_t rcr;
    uint16_t cr;
    uint16_t ctr;
    uint16_t gpr;
    uint16_t ptr;
    uint16_t ercv;
    qemu_irq irq;
    int bank;
    int packet_num;
    int tx_alloc;
    /* Bitmask of allocated packets.  */
    int allocated;
    int tx_fifo_len;
    int tx_fifo[NUM_PACKETS];
    int rx_fifo_len;
    int rx_fifo[NUM_PACKETS];
    int tx_fifo_done_len;
    int tx_fifo_done[NUM_PACKETS];
    /* Packet buffer memory.  */
    uint8_t data[NUM_PACKETS][2048];
    uint8_t int_level;
    uint8_t int_mask;
    MemoryRegion mmio;
} smc91c111_state;

static const VMStateDescription vmstate_smc91c111 = {
    .name = "smc91c111",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT16(tcr, smc91c111_state),
        VMSTATE_UINT16(rcr, smc91c111_state),
        VMSTATE_UINT16(cr, smc91c111_state),
        VMSTATE_UINT16(ctr, smc91c111_state),
        VMSTATE_UINT16(gpr, smc91c111_state),
        VMSTATE_UINT16(ptr, smc91c111_state),
        VMSTATE_UINT16(ercv, smc91c111_state),
        VMSTATE_INT32(bank, smc91c111_state),
        VMSTATE_INT32(packet_num, smc91c111_state),
        VMSTATE_INT32(tx_alloc, smc91c111_state),
        VMSTATE_INT32(allocated, smc91c111_state),
        VMSTATE_INT32(tx_fifo_len, smc91c111_state),
        VMSTATE_INT32_ARRAY(tx_fifo, smc91c111_state, NUM_PACKETS),
        VMSTATE_INT32(rx_fifo_len, smc91c111_state),
        VMSTATE_INT32_ARRAY(rx_fifo, smc91c111_state, NUM_PACKETS),
        VMSTATE_INT32(tx_fifo_done_len, smc91c111_state),
        VMSTATE_INT32_ARRAY(tx_fifo_done, smc91c111_state, NUM_PACKETS),
        VMSTATE_BUFFER_UNSAFE(data, smc91c111_state, 0, NUM_PACKETS * 2048),
        VMSTATE_UINT8(int_level, smc91c111_state),
        VMSTATE_UINT8(int_mask, smc91c111_state),
        VMSTATE_END_OF_LIST()
    }
};

#define RCR_SOFT_RST  0x8000
#define RCR_STRIP_CRC 0x0200
#define RCR_RXEN      0x0100

#define TCR_EPH_LOOP  0x2000
#define TCR_NOCRC     0x0100
#define TCR_PAD_EN    0x0080
#define TCR_FORCOL    0x0004
#define TCR_LOOP      0x0002
#define TCR_TXEN      0x0001

#define INT_MD        0x80
#define INT_ERCV      0x40
#define INT_EPH       0x20
#define INT_RX_OVRN   0x10
#define INT_ALLOC     0x08
#define INT_TX_EMPTY  0x04
#define INT_TX        0x02
#define INT_RCV       0x01

#define CTR_AUTO_RELEASE  0x0800
#define CTR_RELOAD        0x0002
#define CTR_STORE         0x0001

#define RS_ALGNERR      0x8000
#define RS_BRODCAST     0x4000
#define RS_BADCRC       0x2000
#define RS_ODDFRAME     0x1000
#define RS_TOOLONG      0x0800
#define RS_TOOSHORT     0x0400
#define RS_MULTICAST    0x0001

static void mii_write(MII *mii, uint32_t value)
{
/*
A timing diagram for a Ml serial port frame is shown in Figure 7.1. The Ml serial port is idle when at
least 32 continuous 1's are detected on MDIO and remains idle as long as continuous 1's are detected.
During idle, MDIO is in the high impedance state. When the Ml serial port is in the idle state, a 01
pattern on the MDIO initiates a serial shift cycle. Data on MDIO is then shifted in on the next 14 rising
edges of MDC (MDIO is high impedance). If the register access mode is not enabled, on the next 16
rising edges of MDC, data is either shifted in or out on MDIO, depending on whether a write or read
cycle was selected with the bits READ and WRITE. After the 32 MDC cycles have been completed,
one complete register has been read/written, the serial shift process is halted, data is latched into the
device, and MDIO goes into high impedance state. Another serial shift cycle cannot be initiated until
the idle condition (at least 32 continuous 1's) is detected.
*/
    bool clock = !mii->mclk && (value & 4);

    assert((value & 0xfffffff0) == 0x30);

    if (clock) {
        /* Raising clock. */
        mii->mdi = value & MII_MDI;
        mii->mdoe = value & MII_MDOE;
        mii->mdo = value & MII_MDO;
        mii->data = (mii->data << 1) + (value & 1);
        logout("%u%u%u%u %08x\n",
               mii->mdoe ? 1 : 0, mii->mclk ? 1 : 0,
               mii->mdi ? 1 : 0, mii->mdo ? 1 : 0,
               mii->data);
        switch (mii->status) {
            case MII_WAITING:
                if (mii->data == 0xffffffff) {
                    logout("mii is idle\n");
                    mii->mdi = MII_MDI;
                    mii->status = MII_IDLE;
                }
                break;
            case MII_IDLE:
                if (mii->data == 0xfffffffd) {
                    logout("mii got start bit pattern 01\n");
                    mii->status = MII_GOT_START;
                }
                break;
            case MII_GOT_START:
                if ((mii->data & 0xffffc000) == 0xffff4000) {
                    unsigned op = (mii->data >> 12) & 0x3;
                    unsigned phy = (mii->data >> 7) & 0x1f;
                    unsigned reg = (mii->data >> 2) & 0x1f;
                    logout("mii got 14 bits (op=%u, phy=%u, reg=%u, ta=%u)\n",
                           op, phy, reg, mii->data & 0x3);
                    mii->reg = reg;
                    if (phy != 0) {
                        logout("Wrong phy %u, wait for idle\n", phy);
                        mii->status = MII_WAITING;
                    } else if (reg >= ARRAY_SIZE(mii->regs)) {
                        logout("Wrong reg %u, wait for idle\n", reg);
                        mii->status = MII_WAITING;
                    } else if (op == 1) {
                        mii->status = MII_WRITE;
                    } else if (op == 2) {
                        mii->data = mii->regs[reg];
                        mii->mdi = 0;
                        mii->status = MII_READ;
                    } else {
                        logout("Illegal MII operation %u, wait for idle\n", op);
                        mii->status = MII_WAITING;
                    }
                }
                break;
            case MII_WRITE:
                if ((mii->data & 0xc0000000) == 0x40000000) {
                    uint16_t data = mii->data & 0xffff;
                    logout("mii wrote 16 bits (data=0x%04x=%u)\n", data, data);
                    mii->regs[mii->reg] = data;
                    mii->status = MII_WAITING;
                }
                break;
            case MII_READ:
                if ((mii->data & 0x0000ffff) == 0x0000ffff) {
                    logout("mii read 16 bits\n");
                    mii->mdi = MII_MDI;
                    mii->status = MII_WAITING;
                } else {
                    mii->data |= 1;
                    mii->mdi = (mii->data & 0x00010000) ? MII_MDI : 0;
                }
                break;
        }
    } /* clock */
    mii->mclk = value & MII_MCLK;
    //~ assert(mii->mdoe);
}

static void mii_reset(MII *mii)
{
    memcpy(mii->regs, mii_regs_default, sizeof(mii->regs));
}

/* Update interrupt status.  */
static void smc91c111_update(smc91c111_state *s)
{
    int level;

    if (s->tx_fifo_len == 0)
        s->int_level |= INT_TX_EMPTY;
    if (s->tx_fifo_done_len != 0)
        s->int_level |= INT_TX;
    level = (s->int_level & s->int_mask) != 0;
    qemu_set_irq(s->irq, level);
}

/* Try to allocate a packet.  Returns 0x80 on failure.  */
static int smc91c111_allocate_packet(smc91c111_state *s)
{
    int i;
    if (s->allocated == (1 << NUM_PACKETS) - 1) {
        return 0x80;
    }

    for (i = 0; i < NUM_PACKETS; i++) {
        if ((s->allocated & (1 << i)) == 0)
            break;
    }
    s->allocated |= 1 << i;
    return i;
}


/* Process a pending TX allocate.  */
static void smc91c111_tx_alloc(smc91c111_state *s)
{
    s->tx_alloc = smc91c111_allocate_packet(s);
    if (s->tx_alloc == 0x80)
        return;
    s->int_level |= INT_ALLOC;
    smc91c111_update(s);
}

/* Remove and item from the RX FIFO.  */
static void smc91c111_pop_rx_fifo(smc91c111_state *s)
{
    int i;

    s->rx_fifo_len--;
    if (s->rx_fifo_len) {
        for (i = 0; i < s->rx_fifo_len; i++)
            s->rx_fifo[i] = s->rx_fifo[i + 1];
        s->int_level |= INT_RCV;
    } else {
        s->int_level &= ~INT_RCV;
    }
    smc91c111_update(s);
}

/* Remove an item from the TX completion FIFO.  */
static void smc91c111_pop_tx_fifo_done(smc91c111_state *s)
{
    int i;

    if (s->tx_fifo_done_len == 0)
        return;
    s->tx_fifo_done_len--;
    for (i = 0; i < s->tx_fifo_done_len; i++)
        s->tx_fifo_done[i] = s->tx_fifo_done[i + 1];
}

/* Release the memory allocated to a packet.  */
static void smc91c111_release_packet(smc91c111_state *s, int packet)
{
    s->allocated &= ~(1 << packet);
    if (s->tx_alloc == 0x80)
        smc91c111_tx_alloc(s);
    qemu_flush_queued_packets(qemu_get_queue(s->nic));
}

/* Flush the TX FIFO.  */
static void smc91c111_do_tx(smc91c111_state *s)
{
    int i;
    int len;
    int control;
    int packetnum;
    uint8_t *p;

    if ((s->tcr & TCR_TXEN) == 0)
        return;
    if (s->tx_fifo_len == 0)
        return;
    for (i = 0; i < s->tx_fifo_len; i++) {
        packetnum = s->tx_fifo[i];
        p = &s->data[packetnum][0];
        /* Set status word.  */
        *(p++) = 0x01;
        *(p++) = 0x40;
        len = *(p++);
        len |= ((int)*(p++)) << 8;
        len -= 6;
        control = p[len + 1];
        if (control & 0x20)
            len++;
        /* ??? This overwrites the data following the buffer.
           Don't know what real hardware does.  */
        if (len < 64 && (s->tcr & TCR_PAD_EN)) {
            memset(p + len, 0, 64 - len);
            len = 64;
        }
#if 0
        {
            int add_crc;

            /* The card is supposed to append the CRC to the frame.
               However none of the other network traffic has the CRC
               appended.  Suspect this is low level ethernet detail we
               don't need to worry about.  */
            add_crc = (control & 0x10) || (s->tcr & TCR_NOCRC) == 0;
            if (add_crc) {
                uint32_t crc;

                crc = crc32(~0, p, len);
                memcpy(p + len, &crc, 4);
                len += 4;
            }
        }
#endif
        if (s->ctr & CTR_AUTO_RELEASE)
            /* Race?  */
            smc91c111_release_packet(s, packetnum);
        else if (s->tx_fifo_done_len < NUM_PACKETS)
            s->tx_fifo_done[s->tx_fifo_done_len++] = packetnum;
        qemu_send_packet(qemu_get_queue(s->nic), p, len);
    }
    s->tx_fifo_len = 0;
    smc91c111_update(s);
}

/* Add a packet to the TX FIFO.  */
static void smc91c111_queue_tx(smc91c111_state *s, int packet)
{
    if (s->tx_fifo_len == NUM_PACKETS)
        return;
    s->tx_fifo[s->tx_fifo_len++] = packet;
    smc91c111_do_tx(s);
}

static void smc91c111_reset(DeviceState *dev)
{
    smc91c111_state *s = SMC91C111(dev);

    s->bank = 0;
    s->tx_fifo_len = 0;
    s->tx_fifo_done_len = 0;
    s->rx_fifo_len = 0;
    s->allocated = 0;
    s->packet_num = 0;
    s->tx_alloc = 0;
    s->tcr = 0;
    s->rcr = 0;
    s->cr = 0xa0b1;
    s->ctr = 0x1210;
    s->ptr = 0;
    s->ercv = 0x1f;
    s->int_level = INT_TX_EMPTY;
    s->int_mask = 0;
    mii_reset(&s->mii);
    smc91c111_update(s);
}

#define SET_LOW(name, val) s->name = (s->name & 0xff00) | val
#define SET_HIGH(name, val) s->name = (s->name & 0xff) | (val << 8)

static void smc91c111_writeb(void *opaque, hwaddr offset,
                             uint32_t value)
{
    smc91c111_state *s = (smc91c111_state *)opaque;

    offset = offset & 0xf;
    if (offset == 14) {
        s->bank = value;
        return;
    }
    if (offset == 15)
        return;
    switch (s->bank) {
    case 0:
        switch (offset) {
        case 0: /* TCR */
            SET_LOW(tcr, value);
            return;
        case 1:
            SET_HIGH(tcr, value);
            return;
        case 4: /* RCR */
            SET_LOW(rcr, value);
            return;
        case 5:
            SET_HIGH(rcr, value);
            if (s->rcr & RCR_SOFT_RST) {
                smc91c111_reset(DEVICE(s));
            }
            return;
        case 10: case 11: /* RPCR */
            /* Ignored */
            return;
        case 12: case 13: /* Reserved */
            return;
        default:
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
        }
        break;

    case 1:
        switch (offset) {
        case 0: /* CONFIG */
            SET_LOW(cr, value);
            return;
        case 1:
            SET_HIGH(cr,value);
            return;
        case 2: case 3: /* BASE */
        case 4: case 5: case 6: case 7: case 8: case 9: /* IA */
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
            return;
        case 10: /* General Purpose */
            SET_LOW(gpr, value);
            return;
        case 11:
            SET_HIGH(gpr, value);
            return;
        case 12: /* Control */
            if (value & 1)
                fprintf(stderr, "smc91c111:EEPROM store not implemented\n");
            if (value & 2)
                fprintf(stderr, "smc91c111:EEPROM reload not implemented\n");
            value &= ~3;
            SET_LOW(ctr, value);
            return;
        case 13:
            SET_HIGH(ctr, value);
            return;
        default:
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
        }
        break;

    case 2:
        switch (offset) {
        case 0: /* MMU Command */
            switch (value >> 5) {
            case 0: /* no-op */
                break;
            case 1: /* Allocate for TX.  */
                s->tx_alloc = 0x80;
                s->int_level &= ~INT_ALLOC;
                smc91c111_update(s);
                smc91c111_tx_alloc(s);
                break;
            case 2: /* Reset MMU.  */
                s->allocated = 0;
                s->tx_fifo_len = 0;
                s->tx_fifo_done_len = 0;
                s->rx_fifo_len = 0;
                s->tx_alloc = 0;
                break;
            case 3: /* Remove from RX FIFO.  */
                smc91c111_pop_rx_fifo(s);
                break;
            case 4: /* Remove from RX FIFO and release.  */
                if (s->rx_fifo_len > 0) {
                    smc91c111_release_packet(s, s->rx_fifo[0]);
                }
                smc91c111_pop_rx_fifo(s);
                break;
            case 5: /* Release.  */
                smc91c111_release_packet(s, s->packet_num);
                break;
            case 6: /* Add to TX FIFO.  */
                smc91c111_queue_tx(s, s->packet_num);
                break;
            case 7: /* Reset TX FIFO.  */
                s->tx_fifo_len = 0;
                s->tx_fifo_done_len = 0;
                break;
            default:
                logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
            }
            return;
        case 1:
            /* Ignore.  */
            return;
        case 2: /* Packet Number Register */
            s->packet_num = value;
            return;
        case 3: case 4: case 5:
            /* Should be readonly, but linux writes to them anyway. Ignore.  */
            return;
        case 6: /* Pointer */
            SET_LOW(ptr, value);
            return;
        case 7:
            SET_HIGH(ptr, value);
            return;
        case 8: case 9: case 10: case 11: /* Data */
            {
                int p;
                int n;

                if (s->ptr & 0x8000)
                    n = s->rx_fifo[0];
                else
                    n = s->packet_num;
                p = s->ptr & 0x07ff;
                if (s->ptr & 0x4000) {
                    s->ptr = (s->ptr & 0xf800) | ((s->ptr + 1) & 0x7ff);
                } else {
                    p += (offset & 3);
                }
                s->data[n][p] = value;
            }
            return;
        case 12: /* Interrupt ACK.  */
            s->int_level &= ~(value & 0xd6);
            if (value & INT_TX)
                smc91c111_pop_tx_fifo_done(s);
            smc91c111_update(s);
            return;
        case 13: /* Interrupt mask.  */
            s->int_mask = value;
            smc91c111_update(s);
            return;
        default:
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
        }
        break;

    case 3:
        switch (offset) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            /* Multicast table.  */
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
            return;
        case 8: /* Management Interface (low). */
            mii_write(&s->mii, value);
            return;
        case 9: /* Management Interface (high). */
            if (value != 0x33) {
                /* MSK_CRS100 not implemented. */
                logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
            }
            return;
        case 12: /* Early receive.  */
            s->ercv = value & 0x1f;
            return;
        case 13:
            /* Ignore.  */
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
            return;
        default:
            logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
        }
        break;
    default:
        logout(TARGET_FMT_plx "= %02" PRIx32 "\n", offset, value);
    }
    hw_error("smc91c111_write: Bad reg %d:%x\n", s->bank, (int)offset);
}

static uint32_t smc91c111_readb(void *opaque, hwaddr offset)
{
    smc91c111_state *s = (smc91c111_state *)opaque;

    offset = offset & 0xf;
    if (offset == 14) {
        return s->bank;
    }
    if (offset == 15)
        return 0x33;
    switch (s->bank) {
    case 0:
        switch (offset) {
        case 0: /* TCR */
            return s->tcr & 0xff;
        case 1:
            return s->tcr >> 8;
        case 2: /* EPH Status */
            return 0;
        case 3:
            return 0x40;
        case 4: /* RCR */
            return s->rcr & 0xff;
        case 5:
            return s->rcr >> 8;
        case 6: /* Counter */
        case 7:
            logout(TARGET_FMT_plx "\n", offset);
            return 0;
        case 8: /* Memory size.  */
            return NUM_PACKETS;
        case 9: /* Free memory available.  */
            {
                int i;
                int n;
                n = 0;
                for (i = 0; i < NUM_PACKETS; i++) {
                    if (s->allocated & (1 << i))
                        n++;
                }
                return n;
            }
        case 10: case 11: /* RPCR */
            logout(TARGET_FMT_plx "\n", offset);
            return 0;
        case 12: case 13: /* Reserved */
            return 0;
        default:
            logout(TARGET_FMT_plx "\n", offset);
        }
        break;

    case 1:
        switch (offset) {
        case 0: /* CONFIG */
            return s->cr & 0xff;
        case 1:
            return s->cr >> 8;
        case 2: case 3: /* BASE */
            logout(TARGET_FMT_plx "\n", offset);
            return 0;
        case 4: case 5: case 6: case 7: case 8: case 9: /* IA */
            return s->conf.macaddr.a[offset - 4];
        case 10: /* General Purpose */
            return s->gpr & 0xff;
        case 11:
            return s->gpr >> 8;
        case 12: /* Control */
            return s->ctr & 0xff;
        case 13:
            return s->ctr >> 8;
        default:
            logout(TARGET_FMT_plx "\n", offset);
        }
        break;

    case 2:
        switch (offset) {
        case 0: case 1: /* MMUCR Busy bit.  */
            return 0;
        case 2: /* Packet Number.  */
            return s->packet_num;
        case 3: /* Allocation Result.  */
            return s->tx_alloc;
        case 4: /* TX FIFO */
            if (s->tx_fifo_done_len == 0)
                return 0x80;
            else
                return s->tx_fifo_done[0];
        case 5: /* RX FIFO */
            if (s->rx_fifo_len == 0)
                return 0x80;
            else
                return s->rx_fifo[0];
        case 6: /* Pointer */
            return s->ptr & 0xff;
        case 7:
            return (s->ptr >> 8) & 0xf7;
        case 8: case 9: case 10: case 11: /* Data */
            {
                int p;
                int n;

                if (s->ptr & 0x8000)
                    n = s->rx_fifo[0];
                else
                    n = s->packet_num;
                p = s->ptr & 0x07ff;
                if (s->ptr & 0x4000) {
                    s->ptr = (s->ptr & 0xf800) | ((s->ptr + 1) & 0x07ff);
                } else {
                    p += (offset & 3);
                }
                return s->data[n][p];
            }
        case 12: /* Interrupt status.  */
            return s->int_level;
        case 13: /* Interrupt mask.  */
            return s->int_mask;
        default:
            logout(TARGET_FMT_plx "\n", offset);
        }
        break;

    case 3:
        switch (offset) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            /* Multicast table.  */
            logout(TARGET_FMT_plx "\n", offset);
            return 0;
        case 8: /* Management Interface (low). */
            logout(TARGET_FMT_plx "\n", offset);
            return 0x30 + s->mii.mdi;
        case 9: /* Management Interface (high). */
            return 0x33;
        case 10: /* Revision.  */
            return 0x91;
        case 11:
            return 0x33;
        case 12:
            return s->ercv;
        case 13:
            return 0;
        default:
            logout(TARGET_FMT_plx "\n", offset);
        }
        break;
    default:
        logout(TARGET_FMT_plx "\n", offset);
    }
    hw_error("smc91c111_read: Bad reg %d:%x\n", s->bank, (int)offset);
    return 0;
}

static void smc91c111_writew(void *opaque, hwaddr offset,
                             uint32_t value)
{
    smc91c111_writeb(opaque, offset, value & 0xff);
    smc91c111_writeb(opaque, offset + 1, value >> 8);
}

static void smc91c111_writel(void *opaque, hwaddr offset,
                             uint32_t value)
{
    /* 32-bit writes to offset 0xc only actually write to the bank select
       register (offset 0xe)  */
    if (offset != 0xc)
        smc91c111_writew(opaque, offset, value & 0xffff);
    smc91c111_writew(opaque, offset + 2, value >> 16);
}

static uint32_t smc91c111_readw(void *opaque, hwaddr offset)
{
    uint32_t val;
    val = smc91c111_readb(opaque, offset);
    val |= smc91c111_readb(opaque, offset + 1) << 8;
    return val;
}

static uint32_t smc91c111_readl(void *opaque, hwaddr offset)
{
    uint32_t val;
    val = smc91c111_readw(opaque, offset);
    val |= smc91c111_readw(opaque, offset + 2) << 16;
    return val;
}

static int smc91c111_can_receive(NetClientState *nc)
{
    smc91c111_state *s = qemu_get_nic_opaque(nc);

    if ((s->rcr & RCR_RXEN) == 0 || (s->rcr & RCR_SOFT_RST))
        return 1;
    if (s->allocated == (1 << NUM_PACKETS) - 1)
        return 0;
    return 1;
}

static ssize_t smc91c111_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    smc91c111_state *s = qemu_get_nic_opaque(nc);
    int status;
    int packetsize;
    uint32_t crc;
    int packetnum;
    uint8_t *p;

    if ((s->rcr & RCR_RXEN) == 0 || (s->rcr & RCR_SOFT_RST))
        return -1;
    /* Short packets are padded with zeros.  Receiving a packet
       < 64 bytes long is considered an error condition.  */
    if (size < 64)
        packetsize = 64;
    else
        packetsize = (size & ~1);
    packetsize += 6;
    crc = (s->rcr & RCR_STRIP_CRC) == 0;
    if (crc)
        packetsize += 4;
    /* TODO: Flag overrun and receive errors.  */
    if (packetsize > 2048)
        return -1;
    packetnum = smc91c111_allocate_packet(s);
    if (packetnum == 0x80)
        return -1;
    s->rx_fifo[s->rx_fifo_len++] = packetnum;

    p = &s->data[packetnum][0];
    /* ??? Multicast packets?  */
    status = 0;
    if (size > 1518)
        status |= RS_TOOLONG;
    if (size & 1)
        status |= RS_ODDFRAME;
    *(p++) = status & 0xff;
    *(p++) = status >> 8;
    *(p++) = packetsize & 0xff;
    *(p++) = packetsize >> 8;
    memcpy(p, buf, size & ~1);
    p += (size & ~1);
    /* Pad short packets.  */
    if (size < 64) {
        int pad;

        if (size & 1)
            *(p++) = buf[size - 1];
        pad = 64 - size;
        memset(p, 0, pad);
        p += pad;
        size = 64;
    }
    /* It's not clear if the CRC should go before or after the last byte in
       odd sized packets.  Linux disables the CRC, so that's no help.
       The pictures in the documentation show the CRC aligned on a 16-bit
       boundary before the last odd byte, so that's what we do.  */
    if (crc) {
        crc = crc32(~0, buf, size);
        *(p++) = crc & 0xff; crc >>= 8;
        *(p++) = crc & 0xff; crc >>= 8;
        *(p++) = crc & 0xff; crc >>= 8;
        *(p++) = crc & 0xff;
    }
    if (size & 1) {
        *(p++) = buf[size - 1];
        *p = 0x60;
    } else {
        *(p++) = 0;
        *p = 0x40;
    }
    /* TODO: Raise early RX interrupt?  */
    s->int_level |= INT_RCV;
    smc91c111_update(s);

    return size;
}

static const MemoryRegionOps smc91c111_mem_ops = {
    /* The special case for 32 bit writes to 0xc means we can't just
     * set .impl.min/max_access_size to 1, unfortunately
     */
    .old_mmio = {
        .read = { smc91c111_readb, smc91c111_readw, smc91c111_readl, },
        .write = { smc91c111_writeb, smc91c111_writew, smc91c111_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void smc91c111_cleanup(NetClientState *nc)
{
    smc91c111_state *s = qemu_get_nic_opaque(nc);

    s->nic = NULL;
}

static NetClientInfo net_smc91c111_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = smc91c111_can_receive,
    .receive = smc91c111_receive,
    .cleanup = smc91c111_cleanup,
};

static int smc91c111_init1(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    smc91c111_state *s = SMC91C111(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &smc91c111_mem_ops, s,
                          "smc91c111-mmio", 16);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_smc91c111_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
    /* ??? Save/restore.  */
    return 0;
}

static Property smc91c111_properties[] = {
    DEFINE_NIC_PROPERTIES(smc91c111_state, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void smc91c111_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = smc91c111_init1;
    dc->reset = smc91c111_reset;
    dc->vmsd = &vmstate_smc91c111;
    dc->props = smc91c111_properties;
}

static const TypeInfo smc91c111_info = {
    .name          = TYPE_SMC91C111,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(smc91c111_state),
    .class_init    = smc91c111_class_init,
};

static void smc91c111_register_types(void)
{
    type_register_static(&smc91c111_info);
}

/* Legacy helper function.  Should go away when machine config files are
   implemented.  */
void smc91c111_init(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(nd, "smc91c111");
    dev = qdev_create(NULL, TYPE_SMC91C111);
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

type_init(smc91c111_register_types)
