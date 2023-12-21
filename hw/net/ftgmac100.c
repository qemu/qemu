/*
 * Faraday FTGMAC100 Gigabit Ethernet
 *
 * Copyright (C) 2016-2017, IBM Corporation.
 *
 * Based on Coldfire Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/net/ftgmac100.h"
#include "sysemu/dma.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "net/checksum.h"
#include "net/eth.h"
#include "hw/net/mii.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

/* For crc32 */
#include <zlib.h>

/*
 * FTGMAC100 registers
 */
#define FTGMAC100_ISR             0x00
#define FTGMAC100_IER             0x04
#define FTGMAC100_MAC_MADR        0x08
#define FTGMAC100_MAC_LADR        0x0c
#define FTGMAC100_MATH0           0x10
#define FTGMAC100_MATH1           0x14
#define FTGMAC100_NPTXPD          0x18
#define FTGMAC100_RXPD            0x1C
#define FTGMAC100_NPTXR_BADR      0x20
#define FTGMAC100_RXR_BADR        0x24
#define FTGMAC100_HPTXPD          0x28
#define FTGMAC100_HPTXR_BADR      0x2c
#define FTGMAC100_ITC             0x30
#define FTGMAC100_APTC            0x34
#define FTGMAC100_DBLAC           0x38
#define FTGMAC100_REVR            0x40
#define FTGMAC100_FEAR1           0x44
#define FTGMAC100_RBSR            0x4c
#define FTGMAC100_TPAFCR          0x48

#define FTGMAC100_MACCR           0x50
#define FTGMAC100_MACSR           0x54
#define FTGMAC100_PHYCR           0x60
#define FTGMAC100_PHYDATA         0x64
#define FTGMAC100_FCR             0x68

/*
 * Interrupt status register & interrupt enable register
 */
#define FTGMAC100_INT_RPKT_BUF    (1 << 0)
#define FTGMAC100_INT_RPKT_FIFO   (1 << 1)
#define FTGMAC100_INT_NO_RXBUF    (1 << 2)
#define FTGMAC100_INT_RPKT_LOST   (1 << 3)
#define FTGMAC100_INT_XPKT_ETH    (1 << 4)
#define FTGMAC100_INT_XPKT_FIFO   (1 << 5)
#define FTGMAC100_INT_NO_NPTXBUF  (1 << 6)
#define FTGMAC100_INT_XPKT_LOST   (1 << 7)
#define FTGMAC100_INT_AHB_ERR     (1 << 8)
#define FTGMAC100_INT_PHYSTS_CHG  (1 << 9)
#define FTGMAC100_INT_NO_HPTXBUF  (1 << 10)

/*
 * Automatic polling timer control register
 */
#define FTGMAC100_APTC_RXPOLL_CNT(x)        ((x) & 0xf)
#define FTGMAC100_APTC_RXPOLL_TIME_SEL      (1 << 4)
#define FTGMAC100_APTC_TXPOLL_CNT(x)        (((x) >> 8) & 0xf)
#define FTGMAC100_APTC_TXPOLL_TIME_SEL      (1 << 12)

/*
 * DMA burst length and arbitration control register
 */
#define FTGMAC100_DBLAC_RXBURST_SIZE(x)     (((x) >> 8) & 0x3)
#define FTGMAC100_DBLAC_TXBURST_SIZE(x)     (((x) >> 10) & 0x3)
#define FTGMAC100_DBLAC_RXDES_SIZE(x)       ((((x) >> 12) & 0xf) * 8)
#define FTGMAC100_DBLAC_TXDES_SIZE(x)       ((((x) >> 16) & 0xf) * 8)
#define FTGMAC100_DBLAC_IFG_CNT(x)          (((x) >> 20) & 0x7)
#define FTGMAC100_DBLAC_IFG_INC             (1 << 23)

/*
 * PHY control register
 */
#define FTGMAC100_PHYCR_MIIRD               (1 << 26)
#define FTGMAC100_PHYCR_MIIWR               (1 << 27)

#define FTGMAC100_PHYCR_DEV(x)              (((x) >> 16) & 0x1f)
#define FTGMAC100_PHYCR_REG(x)              (((x) >> 21) & 0x1f)

/*
 * PHY data register
 */
#define FTGMAC100_PHYDATA_MIIWDATA(x)       ((x) & 0xffff)
#define FTGMAC100_PHYDATA_MIIRDATA(x)       (((x) >> 16) & 0xffff)

/*
 * PHY control register - New MDC/MDIO interface
 */
#define FTGMAC100_PHYCR_NEW_DATA(x)     (((x) >> 16) & 0xffff)
#define FTGMAC100_PHYCR_NEW_FIRE        (1 << 15)
#define FTGMAC100_PHYCR_NEW_ST_22       (1 << 12)
#define FTGMAC100_PHYCR_NEW_OP(x)       (((x) >> 10) & 3)
#define   FTGMAC100_PHYCR_NEW_OP_WRITE    0x1
#define   FTGMAC100_PHYCR_NEW_OP_READ     0x2
#define FTGMAC100_PHYCR_NEW_DEV(x)      (((x) >> 5) & 0x1f)
#define FTGMAC100_PHYCR_NEW_REG(x)      ((x) & 0x1f)

/*
 * Feature Register
 */
#define FTGMAC100_REVR_NEW_MDIO_INTERFACE   (1 << 31)

/*
 * MAC control register
 */
#define FTGMAC100_MACCR_TXDMA_EN         (1 << 0)
#define FTGMAC100_MACCR_RXDMA_EN         (1 << 1)
#define FTGMAC100_MACCR_TXMAC_EN         (1 << 2)
#define FTGMAC100_MACCR_RXMAC_EN         (1 << 3)
#define FTGMAC100_MACCR_RM_VLAN          (1 << 4)
#define FTGMAC100_MACCR_HPTXR_EN         (1 << 5)
#define FTGMAC100_MACCR_LOOP_EN          (1 << 6)
#define FTGMAC100_MACCR_ENRX_IN_HALFTX   (1 << 7)
#define FTGMAC100_MACCR_FULLDUP          (1 << 8)
#define FTGMAC100_MACCR_GIGA_MODE        (1 << 9)
#define FTGMAC100_MACCR_CRC_APD          (1 << 10) /* not needed */
#define FTGMAC100_MACCR_RX_RUNT          (1 << 12)
#define FTGMAC100_MACCR_JUMBO_LF         (1 << 13)
#define FTGMAC100_MACCR_RX_ALL           (1 << 14)
#define FTGMAC100_MACCR_HT_MULTI_EN      (1 << 15)
#define FTGMAC100_MACCR_RX_MULTIPKT      (1 << 16)
#define FTGMAC100_MACCR_RX_BROADPKT      (1 << 17)
#define FTGMAC100_MACCR_DISCARD_CRCERR   (1 << 18)
#define FTGMAC100_MACCR_FAST_MODE        (1 << 19)
#define FTGMAC100_MACCR_SW_RST           (1 << 31)

/*
 * Transmit descriptor
 */
#define FTGMAC100_TXDES0_TXBUF_SIZE(x)   ((x) & 0x3fff)
#define FTGMAC100_TXDES0_EDOTR           (1 << 15)
#define FTGMAC100_TXDES0_CRC_ERR         (1 << 19)
#define FTGMAC100_TXDES0_LTS             (1 << 28)
#define FTGMAC100_TXDES0_FTS             (1 << 29)
#define FTGMAC100_TXDES0_EDOTR_ASPEED    (1 << 30)
#define FTGMAC100_TXDES0_TXDMA_OWN       (1 << 31)

#define FTGMAC100_TXDES1_VLANTAG_CI(x)   ((x) & 0xffff)
#define FTGMAC100_TXDES1_INS_VLANTAG     (1 << 16)
#define FTGMAC100_TXDES1_TCP_CHKSUM      (1 << 17)
#define FTGMAC100_TXDES1_UDP_CHKSUM      (1 << 18)
#define FTGMAC100_TXDES1_IP_CHKSUM       (1 << 19)
#define FTGMAC100_TXDES1_LLC             (1 << 22)
#define FTGMAC100_TXDES1_TX2FIC          (1 << 30)
#define FTGMAC100_TXDES1_TXIC            (1 << 31)

/*
 * Receive descriptor
 */
#define FTGMAC100_RXDES0_VDBC            0x3fff
#define FTGMAC100_RXDES0_EDORR           (1 << 15)
#define FTGMAC100_RXDES0_MULTICAST       (1 << 16)
#define FTGMAC100_RXDES0_BROADCAST       (1 << 17)
#define FTGMAC100_RXDES0_RX_ERR          (1 << 18)
#define FTGMAC100_RXDES0_CRC_ERR         (1 << 19)
#define FTGMAC100_RXDES0_FTL             (1 << 20)
#define FTGMAC100_RXDES0_RUNT            (1 << 21)
#define FTGMAC100_RXDES0_RX_ODD_NB       (1 << 22)
#define FTGMAC100_RXDES0_FIFO_FULL       (1 << 23)
#define FTGMAC100_RXDES0_PAUSE_OPCODE    (1 << 24)
#define FTGMAC100_RXDES0_PAUSE_FRAME     (1 << 25)
#define FTGMAC100_RXDES0_LRS             (1 << 28)
#define FTGMAC100_RXDES0_FRS             (1 << 29)
#define FTGMAC100_RXDES0_EDORR_ASPEED    (1 << 30)
#define FTGMAC100_RXDES0_RXPKT_RDY       (1 << 31)

#define FTGMAC100_RXDES1_VLANTAG_CI      0xffff
#define FTGMAC100_RXDES1_PROT_MASK       (0x3 << 20)
#define FTGMAC100_RXDES1_PROT_NONIP      (0x0 << 20)
#define FTGMAC100_RXDES1_PROT_IP         (0x1 << 20)
#define FTGMAC100_RXDES1_PROT_TCPIP      (0x2 << 20)
#define FTGMAC100_RXDES1_PROT_UDPIP      (0x3 << 20)
#define FTGMAC100_RXDES1_LLC             (1 << 22)
#define FTGMAC100_RXDES1_DF              (1 << 23)
#define FTGMAC100_RXDES1_VLANTAG_AVAIL   (1 << 24)
#define FTGMAC100_RXDES1_TCP_CHKSUM_ERR  (1 << 25)
#define FTGMAC100_RXDES1_UDP_CHKSUM_ERR  (1 << 26)
#define FTGMAC100_RXDES1_IP_CHKSUM_ERR   (1 << 27)

/*
 * Receive and transmit Buffer Descriptor
 */
typedef struct {
    uint32_t        des0;
    uint32_t        des1;
    uint32_t        des2;        /* not used by HW */
    uint32_t        des3;
} FTGMAC100Desc;

#define FTGMAC100_DESC_ALIGNMENT 16

/*
 * Specific RTL8211E MII Registers
 */
#define RTL8211E_MII_PHYCR        16 /* PHY Specific Control */
#define RTL8211E_MII_PHYSR        17 /* PHY Specific Status */
#define RTL8211E_MII_INER         18 /* Interrupt Enable */
#define RTL8211E_MII_INSR         19 /* Interrupt Status */
#define RTL8211E_MII_RXERC        24 /* Receive Error Counter */
#define RTL8211E_MII_LDPSR        27 /* Link Down Power Saving */
#define RTL8211E_MII_EPAGSR       30 /* Extension Page Select */
#define RTL8211E_MII_PAGSEL       31 /* Page Select */

/*
 * RTL8211E Interrupt Status
 */
#define PHY_INT_AUTONEG_ERROR       (1 << 15)
#define PHY_INT_PAGE_RECV           (1 << 12)
#define PHY_INT_AUTONEG_COMPLETE    (1 << 11)
#define PHY_INT_LINK_STATUS         (1 << 10)
#define PHY_INT_ERROR               (1 << 9)
#define PHY_INT_DOWN                (1 << 8)
#define PHY_INT_JABBER              (1 << 0)

/*
 * Max frame size for the receiving buffer
 */
#define FTGMAC100_MAX_FRAME_SIZE    9220

/* Limits depending on the type of the frame
 *
 *   9216 for Jumbo frames (+ 4 for VLAN)
 *   1518 for other frames (+ 4 for VLAN)
 */
static int ftgmac100_max_frame_size(FTGMAC100State *s, uint16_t proto)
{
    int max = (s->maccr & FTGMAC100_MACCR_JUMBO_LF ? 9216 : 1518);

    return max + (proto == ETH_P_VLAN ? 4 : 0);
}

static void ftgmac100_update_irq(FTGMAC100State *s)
{
    qemu_set_irq(s->irq, s->isr & s->ier);
}

/*
 * The MII phy could raise a GPIO to the processor which in turn
 * could be handled as an interrpt by the OS.
 * For now we don't handle any GPIO/interrupt line, so the OS will
 * have to poll for the PHY status.
 */
static void phy_update_irq(FTGMAC100State *s)
{
    ftgmac100_update_irq(s);
}

static void phy_update_link(FTGMAC100State *s)
{
    /* Autonegotiation status mirrors link status.  */
    if (qemu_get_queue(s->nic)->link_down) {
        s->phy_status &= ~(MII_BMSR_LINK_ST | MII_BMSR_AN_COMP);
        s->phy_int |= PHY_INT_DOWN;
    } else {
        s->phy_status |= (MII_BMSR_LINK_ST | MII_BMSR_AN_COMP);
        s->phy_int |= PHY_INT_AUTONEG_COMPLETE;
    }
    phy_update_irq(s);
}

static void ftgmac100_set_link(NetClientState *nc)
{
    phy_update_link(FTGMAC100(qemu_get_nic_opaque(nc)));
}

static void phy_reset(FTGMAC100State *s)
{
    s->phy_status = (MII_BMSR_100TX_FD | MII_BMSR_100TX_HD | MII_BMSR_10T_FD |
                     MII_BMSR_10T_HD | MII_BMSR_EXTSTAT | MII_BMSR_MFPS |
                     MII_BMSR_AN_COMP | MII_BMSR_AUTONEG | MII_BMSR_LINK_ST |
                     MII_BMSR_EXTCAP);
    s->phy_control = (MII_BMCR_AUTOEN | MII_BMCR_FD | MII_BMCR_SPEED1000);
    s->phy_advertise = (MII_ANAR_PAUSE_ASYM | MII_ANAR_PAUSE | MII_ANAR_TXFD |
                        MII_ANAR_TX | MII_ANAR_10FD | MII_ANAR_10 |
                        MII_ANAR_CSMACD);
    s->phy_int_mask = 0;
    s->phy_int = 0;
}

static uint16_t do_phy_read(FTGMAC100State *s, uint8_t reg)
{
    uint16_t val;

    switch (reg) {
    case MII_BMCR: /* Basic Control */
        val = s->phy_control;
        break;
    case MII_BMSR: /* Basic Status */
        val = s->phy_status;
        break;
    case MII_PHYID1: /* ID1 */
        val = RTL8211E_PHYID1;
        break;
    case MII_PHYID2: /* ID2 */
        val = RTL8211E_PHYID2;
        break;
    case MII_ANAR: /* Auto-neg advertisement */
        val = s->phy_advertise;
        break;
    case MII_ANLPAR: /* Auto-neg Link Partner Ability */
        val = (MII_ANLPAR_ACK | MII_ANLPAR_PAUSE | MII_ANLPAR_TXFD |
               MII_ANLPAR_TX | MII_ANLPAR_10FD | MII_ANLPAR_10 |
               MII_ANLPAR_CSMACD);
        break;
    case MII_ANER: /* Auto-neg Expansion */
        val = MII_ANER_NWAY;
        break;
    case MII_CTRL1000: /* 1000BASE-T control  */
        val = (MII_CTRL1000_HALF | MII_CTRL1000_FULL);
        break;
    case MII_STAT1000: /* 1000BASE-T status  */
        val = MII_STAT1000_FULL;
        break;
    case RTL8211E_MII_INSR:  /* Interrupt status.  */
        val = s->phy_int;
        s->phy_int = 0;
        phy_update_irq(s);
        break;
    case RTL8211E_MII_INER:  /* Interrupt enable */
        val = s->phy_int_mask;
        break;
    case RTL8211E_MII_PHYCR:
    case RTL8211E_MII_PHYSR:
    case RTL8211E_MII_RXERC:
    case RTL8211E_MII_LDPSR:
    case RTL8211E_MII_EPAGSR:
    case RTL8211E_MII_PAGSEL:
        qemu_log_mask(LOG_UNIMP, "%s: reg %d not implemented\n",
                      __func__, reg);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset %d\n",
                      __func__, reg);
        val = 0;
        break;
    }

    return val;
}

#define MII_BMCR_MASK (MII_BMCR_LOOPBACK | MII_BMCR_SPEED100 |          \
                       MII_BMCR_SPEED | MII_BMCR_AUTOEN | MII_BMCR_PDOWN | \
                       MII_BMCR_FD | MII_BMCR_CTST)
#define MII_ANAR_MASK 0x2d7f

static void do_phy_write(FTGMAC100State *s, uint8_t reg, uint16_t val)
{
    switch (reg) {
    case MII_BMCR:     /* Basic Control */
        if (val & MII_BMCR_RESET) {
            phy_reset(s);
        } else {
            s->phy_control = val & MII_BMCR_MASK;
            /* Complete autonegotiation immediately.  */
            if (val & MII_BMCR_AUTOEN) {
                s->phy_status |= MII_BMSR_AN_COMP;
            }
        }
        break;
    case MII_ANAR:     /* Auto-neg advertisement */
        s->phy_advertise = (val & MII_ANAR_MASK) | MII_ANAR_TX;
        break;
    case RTL8211E_MII_INER: /* Interrupt enable */
        s->phy_int_mask = val & 0xff;
        phy_update_irq(s);
        break;
    case RTL8211E_MII_PHYCR:
    case RTL8211E_MII_PHYSR:
    case RTL8211E_MII_RXERC:
    case RTL8211E_MII_LDPSR:
    case RTL8211E_MII_EPAGSR:
    case RTL8211E_MII_PAGSEL:
        qemu_log_mask(LOG_UNIMP, "%s: reg %d not implemented\n",
                      __func__, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset %d\n",
                      __func__, reg);
        break;
    }
}

static void do_phy_new_ctl(FTGMAC100State *s)
{
    uint8_t reg;
    uint16_t data;

    if (!(s->phycr & FTGMAC100_PHYCR_NEW_ST_22)) {
        qemu_log_mask(LOG_UNIMP, "%s: unsupported ST code\n", __func__);
        return;
    }

    /* Nothing to do */
    if (!(s->phycr & FTGMAC100_PHYCR_NEW_FIRE)) {
        return;
    }

    reg = FTGMAC100_PHYCR_NEW_REG(s->phycr);
    data = FTGMAC100_PHYCR_NEW_DATA(s->phycr);

    switch (FTGMAC100_PHYCR_NEW_OP(s->phycr)) {
    case FTGMAC100_PHYCR_NEW_OP_WRITE:
        do_phy_write(s, reg, data);
        break;
    case FTGMAC100_PHYCR_NEW_OP_READ:
        s->phydata = do_phy_read(s, reg) & 0xffff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid OP code %08x\n",
                      __func__, s->phycr);
    }

    s->phycr &= ~FTGMAC100_PHYCR_NEW_FIRE;
}

static void do_phy_ctl(FTGMAC100State *s)
{
    uint8_t reg = FTGMAC100_PHYCR_REG(s->phycr);

    if (s->phycr & FTGMAC100_PHYCR_MIIWR) {
        do_phy_write(s, reg, s->phydata & 0xffff);
        s->phycr &= ~FTGMAC100_PHYCR_MIIWR;
    } else if (s->phycr & FTGMAC100_PHYCR_MIIRD) {
        s->phydata = do_phy_read(s, reg) << 16;
        s->phycr &= ~FTGMAC100_PHYCR_MIIRD;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no OP code %08x\n",
                      __func__, s->phycr);
    }
}

static int ftgmac100_read_bd(FTGMAC100Desc *bd, dma_addr_t addr)
{
    if (dma_memory_read(&address_space_memory, addr,
                        bd, sizeof(*bd), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed to read descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    bd->des0 = le32_to_cpu(bd->des0);
    bd->des1 = le32_to_cpu(bd->des1);
    bd->des2 = le32_to_cpu(bd->des2);
    bd->des3 = le32_to_cpu(bd->des3);
    return 0;
}

static int ftgmac100_write_bd(FTGMAC100Desc *bd, dma_addr_t addr)
{
    FTGMAC100Desc lebd;

    lebd.des0 = cpu_to_le32(bd->des0);
    lebd.des1 = cpu_to_le32(bd->des1);
    lebd.des2 = cpu_to_le32(bd->des2);
    lebd.des3 = cpu_to_le32(bd->des3);
    if (dma_memory_write(&address_space_memory, addr,
                         &lebd, sizeof(lebd), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed to write descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    return 0;
}

static int ftgmac100_insert_vlan(FTGMAC100State *s, int frame_size,
                                  uint8_t vlan_tci)
{
    uint8_t *vlan_hdr = s->frame + (ETH_ALEN * 2);
    uint8_t *payload = vlan_hdr + sizeof(struct vlan_header);

    if (frame_size < sizeof(struct eth_header)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: frame too small for VLAN insertion : %d bytes\n",
                      __func__, frame_size);
        s->isr |= FTGMAC100_INT_XPKT_LOST;
        goto out;
    }

    if (frame_size + sizeof(struct vlan_header) > sizeof(s->frame)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: frame too big : %d bytes\n",
                      __func__, frame_size);
        s->isr |= FTGMAC100_INT_XPKT_LOST;
        frame_size -= sizeof(struct vlan_header);
    }

    memmove(payload, vlan_hdr, frame_size - (ETH_ALEN * 2));
    stw_be_p(vlan_hdr, ETH_P_VLAN);
    stw_be_p(vlan_hdr + 2, vlan_tci);
    frame_size += sizeof(struct vlan_header);

out:
    return frame_size;
}

static void ftgmac100_do_tx(FTGMAC100State *s, uint32_t tx_ring,
                            uint32_t tx_descriptor)
{
    int frame_size = 0;
    uint8_t *ptr = s->frame;
    uint32_t addr = tx_descriptor;
    uint32_t flags = 0;

    while (1) {
        FTGMAC100Desc bd;
        int len;

        if (ftgmac100_read_bd(&bd, addr) ||
            ((bd.des0 & FTGMAC100_TXDES0_TXDMA_OWN) == 0)) {
            /* Run out of descriptors to transmit.  */
            s->isr |= FTGMAC100_INT_NO_NPTXBUF;
            break;
        }

        /* record transmit flags as they are valid only on the first
         * segment */
        if (bd.des0 & FTGMAC100_TXDES0_FTS) {
            flags = bd.des1;
        }

        len = FTGMAC100_TXDES0_TXBUF_SIZE(bd.des0);
        if (!len) {
            /*
             * 0 is an invalid size, however the HW does not raise any
             * interrupt. Flag an error because the guest is buggy.
             */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid segment size\n",
                          __func__);
        }

        if (frame_size + len > sizeof(s->frame)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: frame too big : %d bytes\n",
                          __func__, len);
            s->isr |= FTGMAC100_INT_XPKT_LOST;
            len =  sizeof(s->frame) - frame_size;
        }

        if (dma_memory_read(&address_space_memory, bd.des3,
                            ptr, len, MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: failed to read packet @ 0x%x\n",
                          __func__, bd.des3);
            s->isr |= FTGMAC100_INT_AHB_ERR;
            break;
        }

        ptr += len;
        frame_size += len;
        if (bd.des0 & FTGMAC100_TXDES0_LTS) {
            int csum = 0;

            /* Check for VLAN */
            if (flags & FTGMAC100_TXDES1_INS_VLANTAG &&
                be16_to_cpu(PKT_GET_ETH_HDR(s->frame)->h_proto) != ETH_P_VLAN) {
                frame_size = ftgmac100_insert_vlan(s, frame_size,
                                            FTGMAC100_TXDES1_VLANTAG_CI(flags));
            }

            if (flags & FTGMAC100_TXDES1_IP_CHKSUM) {
                csum |= CSUM_IP;
            }
            if (flags & FTGMAC100_TXDES1_TCP_CHKSUM) {
                csum |= CSUM_TCP;
            }
            if (flags & FTGMAC100_TXDES1_UDP_CHKSUM) {
                csum |= CSUM_UDP;
            }
            if (csum) {
                net_checksum_calculate(s->frame, frame_size, csum);
            }

            /* Last buffer in frame.  */
            qemu_send_packet(qemu_get_queue(s->nic), s->frame, frame_size);
            ptr = s->frame;
            frame_size = 0;
            s->isr |= FTGMAC100_INT_XPKT_ETH;
        }

        if (flags & FTGMAC100_TXDES1_TX2FIC) {
            s->isr |= FTGMAC100_INT_XPKT_FIFO;
        }
        bd.des0 &= ~FTGMAC100_TXDES0_TXDMA_OWN;

        /* Write back the modified descriptor.  */
        ftgmac100_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if (bd.des0 & s->txdes0_edotr) {
            addr = tx_ring;
        } else {
            addr += FTGMAC100_DBLAC_TXDES_SIZE(s->dblac);
        }
    }

    s->tx_descriptor = addr;

    ftgmac100_update_irq(s);
}

static bool ftgmac100_can_receive(NetClientState *nc)
{
    FTGMAC100State *s = FTGMAC100(qemu_get_nic_opaque(nc));
    FTGMAC100Desc bd;

    if ((s->maccr & (FTGMAC100_MACCR_RXDMA_EN | FTGMAC100_MACCR_RXMAC_EN))
         != (FTGMAC100_MACCR_RXDMA_EN | FTGMAC100_MACCR_RXMAC_EN)) {
        return false;
    }

    if (ftgmac100_read_bd(&bd, s->rx_descriptor)) {
        return false;
    }
    return !(bd.des0 & FTGMAC100_RXDES0_RXPKT_RDY);
}

/*
 * This is purely informative. The HW can poll the RW (and RX) ring
 * buffers for available descriptors but we don't need to trigger a
 * timer for that in qemu.
 */
static uint32_t ftgmac100_rxpoll(FTGMAC100State *s)
{
    /* Polling times :
     *
     * Speed      TIME_SEL=0    TIME_SEL=1
     *
     *    10         51.2 ms      819.2 ms
     *   100         5.12 ms      81.92 ms
     *  1000        1.024 ms     16.384 ms
     */
    static const int div[] = { 20, 200, 1000 };

    uint32_t cnt = 1024 * FTGMAC100_APTC_RXPOLL_CNT(s->aptcr);
    uint32_t speed = (s->maccr & FTGMAC100_MACCR_FAST_MODE) ? 1 : 0;

    if (s->aptcr & FTGMAC100_APTC_RXPOLL_TIME_SEL) {
        cnt <<= 4;
    }

    if (s->maccr & FTGMAC100_MACCR_GIGA_MODE) {
        speed = 2;
    }

    return cnt / div[speed];
}

static void ftgmac100_do_reset(FTGMAC100State *s, bool sw_reset)
{
    /* Reset the FTGMAC100 */
    s->isr = 0;
    s->ier = 0;
    s->rx_enabled = 0;
    s->rx_ring = 0;
    s->rbsr = 0x640;
    s->rx_descriptor = 0;
    s->tx_ring = 0;
    s->tx_descriptor = 0;
    s->math[0] = 0;
    s->math[1] = 0;
    s->itc = 0;
    s->aptcr = 1;
    s->dblac = 0x00022f00;
    s->revr = 0;
    s->fear1 = 0;
    s->tpafcr = 0xf1;

    if (sw_reset) {
        s->maccr &= FTGMAC100_MACCR_GIGA_MODE | FTGMAC100_MACCR_FAST_MODE;
    } else {
        s->maccr = 0;
    }

    s->phycr = 0;
    s->phydata = 0;
    s->fcr = 0x400;

    /* and the PHY */
    phy_reset(s);
}

static void ftgmac100_reset(DeviceState *d)
{
    ftgmac100_do_reset(FTGMAC100(d), false);
}

static uint64_t ftgmac100_read(void *opaque, hwaddr addr, unsigned size)
{
    FTGMAC100State *s = FTGMAC100(opaque);

    switch (addr & 0xff) {
    case FTGMAC100_ISR:
        return s->isr;
    case FTGMAC100_IER:
        return s->ier;
    case FTGMAC100_MAC_MADR:
        return (s->conf.macaddr.a[0] << 8)  | s->conf.macaddr.a[1];
    case FTGMAC100_MAC_LADR:
        return ((uint32_t) s->conf.macaddr.a[2] << 24) |
            (s->conf.macaddr.a[3] << 16) | (s->conf.macaddr.a[4] << 8) |
            s->conf.macaddr.a[5];
    case FTGMAC100_MATH0:
        return s->math[0];
    case FTGMAC100_MATH1:
        return s->math[1];
    case FTGMAC100_RXR_BADR:
        return s->rx_ring;
    case FTGMAC100_NPTXR_BADR:
        return s->tx_ring;
    case FTGMAC100_ITC:
        return s->itc;
    case FTGMAC100_DBLAC:
        return s->dblac;
    case FTGMAC100_REVR:
        return s->revr;
    case FTGMAC100_FEAR1:
        return s->fear1;
    case FTGMAC100_TPAFCR:
        return s->tpafcr;
    case FTGMAC100_FCR:
        return s->fcr;
    case FTGMAC100_MACCR:
        return s->maccr;
    case FTGMAC100_PHYCR:
        return s->phycr;
    case FTGMAC100_PHYDATA:
        return s->phydata;

        /* We might want to support these one day */
    case FTGMAC100_HPTXPD: /* High Priority Transmit Poll Demand */
    case FTGMAC100_HPTXR_BADR: /* High Priority Transmit Ring Base Address */
    case FTGMAC100_MACSR: /* MAC Status Register (MACSR) */
        qemu_log_mask(LOG_UNIMP, "%s: read to unimplemented register 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static void ftgmac100_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size)
{
    FTGMAC100State *s = FTGMAC100(opaque);

    switch (addr & 0xff) {
    case FTGMAC100_ISR: /* Interrupt status */
        s->isr &= ~value;
        break;
    case FTGMAC100_IER: /* Interrupt control */
        s->ier = value;
        break;
    case FTGMAC100_MAC_MADR: /* MAC */
        s->conf.macaddr.a[0] = value >> 8;
        s->conf.macaddr.a[1] = value;
        break;
    case FTGMAC100_MAC_LADR:
        s->conf.macaddr.a[2] = value >> 24;
        s->conf.macaddr.a[3] = value >> 16;
        s->conf.macaddr.a[4] = value >> 8;
        s->conf.macaddr.a[5] = value;
        break;
    case FTGMAC100_MATH0: /* Multicast Address Hash Table 0 */
        s->math[0] = value;
        break;
    case FTGMAC100_MATH1: /* Multicast Address Hash Table 1 */
        s->math[1] = value;
        break;
    case FTGMAC100_ITC: /* TODO: Interrupt Timer Control */
        s->itc = value;
        break;
    case FTGMAC100_RXR_BADR: /* Ring buffer address */
        if (!QEMU_IS_ALIGNED(value, FTGMAC100_DESC_ALIGNMENT)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad RX buffer alignment 0x%"
                          HWADDR_PRIx "\n", __func__, value);
            return;
        }

        s->rx_ring = value;
        s->rx_descriptor = s->rx_ring;
        break;

    case FTGMAC100_RBSR: /* DMA buffer size */
        s->rbsr = value;
        break;

    case FTGMAC100_NPTXR_BADR: /* Transmit buffer address */
        if (!QEMU_IS_ALIGNED(value, FTGMAC100_DESC_ALIGNMENT)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad TX buffer alignment 0x%"
                          HWADDR_PRIx "\n", __func__, value);
            return;
        }
        s->tx_ring = value;
        s->tx_descriptor = s->tx_ring;
        break;

    case FTGMAC100_NPTXPD: /* Trigger transmit */
        if ((s->maccr & (FTGMAC100_MACCR_TXDMA_EN | FTGMAC100_MACCR_TXMAC_EN))
            == (FTGMAC100_MACCR_TXDMA_EN | FTGMAC100_MACCR_TXMAC_EN)) {
            /* TODO: high priority tx ring */
            ftgmac100_do_tx(s, s->tx_ring, s->tx_descriptor);
        }
        if (ftgmac100_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case FTGMAC100_RXPD: /* Receive Poll Demand Register */
        if (ftgmac100_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case FTGMAC100_APTC: /* Automatic polling */
        s->aptcr = value;

        if (FTGMAC100_APTC_RXPOLL_CNT(s->aptcr)) {
            ftgmac100_rxpoll(s);
        }

        if (FTGMAC100_APTC_TXPOLL_CNT(s->aptcr)) {
            qemu_log_mask(LOG_UNIMP, "%s: no transmit polling\n", __func__);
        }
        break;

    case FTGMAC100_MACCR: /* MAC Device control */
        s->maccr = value;
        if (value & FTGMAC100_MACCR_SW_RST) {
            ftgmac100_do_reset(s, true);
        }

        if (ftgmac100_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case FTGMAC100_PHYCR:  /* PHY Device control */
        s->phycr = value;
        if (s->revr & FTGMAC100_REVR_NEW_MDIO_INTERFACE) {
            do_phy_new_ctl(s);
        } else {
            do_phy_ctl(s);
        }
        break;
    case FTGMAC100_PHYDATA:
        s->phydata = value & 0xffff;
        break;
    case FTGMAC100_DBLAC: /* DMA Burst Length and Arbitration Control */
        if (FTGMAC100_DBLAC_TXDES_SIZE(value) < sizeof(FTGMAC100Desc)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: transmit descriptor too small: %" PRIx64
                          " bytes\n", __func__,
                          FTGMAC100_DBLAC_TXDES_SIZE(value));
            break;
        }
        if (FTGMAC100_DBLAC_RXDES_SIZE(value) < sizeof(FTGMAC100Desc)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: receive descriptor too small : %" PRIx64
                          " bytes\n", __func__,
                          FTGMAC100_DBLAC_RXDES_SIZE(value));
            break;
        }
        s->dblac = value;
        break;
    case FTGMAC100_REVR:  /* Feature Register */
        s->revr = value;
        break;
    case FTGMAC100_FEAR1: /* Feature Register 1 */
        s->fear1 = value;
        break;
    case FTGMAC100_TPAFCR: /* Transmit Priority Arbitration and FIFO Control */
        s->tpafcr = value;
        break;
    case FTGMAC100_FCR: /* Flow Control  */
        s->fcr  = value;
        break;

    case FTGMAC100_HPTXPD: /* High Priority Transmit Poll Demand */
    case FTGMAC100_HPTXR_BADR: /* High Priority Transmit Ring Base Address */
    case FTGMAC100_MACSR: /* MAC Status Register (MACSR) */
        qemu_log_mask(LOG_UNIMP, "%s: write to unimplemented register 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    ftgmac100_update_irq(s);
}

static int ftgmac100_filter(FTGMAC100State *s, const uint8_t *buf, size_t len)
{
    unsigned mcast_idx;

    if (s->maccr & FTGMAC100_MACCR_RX_ALL) {
        return 1;
    }

    switch (get_eth_packet_type(PKT_GET_ETH_HDR(buf))) {
    case ETH_PKT_BCAST:
        if (!(s->maccr & FTGMAC100_MACCR_RX_BROADPKT)) {
            return 0;
        }
        break;
    case ETH_PKT_MCAST:
        if (!(s->maccr & FTGMAC100_MACCR_RX_MULTIPKT)) {
            if (!(s->maccr & FTGMAC100_MACCR_HT_MULTI_EN)) {
                return 0;
            }

            mcast_idx = net_crc32_le(buf, ETH_ALEN);
            mcast_idx = (~(mcast_idx >> 2)) & 0x3f;
            if (!(s->math[mcast_idx / 32] & (1 << (mcast_idx % 32)))) {
                return 0;
            }
        }
        break;
    case ETH_PKT_UCAST:
        if (memcmp(s->conf.macaddr.a, buf, 6)) {
            return 0;
        }
        break;
    }

    return 1;
}

static ssize_t ftgmac100_receive(NetClientState *nc, const uint8_t *buf,
                                 size_t len)
{
    FTGMAC100State *s = FTGMAC100(qemu_get_nic_opaque(nc));
    FTGMAC100Desc bd;
    uint32_t flags = 0;
    uint32_t addr;
    uint32_t crc;
    uint32_t buf_addr;
    uint8_t *crc_ptr;
    uint32_t buf_len;
    size_t size = len;
    uint32_t first = FTGMAC100_RXDES0_FRS;
    uint16_t proto = be16_to_cpu(PKT_GET_ETH_HDR(buf)->h_proto);
    int max_frame_size = ftgmac100_max_frame_size(s, proto);

    if ((s->maccr & (FTGMAC100_MACCR_RXDMA_EN | FTGMAC100_MACCR_RXMAC_EN))
         != (FTGMAC100_MACCR_RXDMA_EN | FTGMAC100_MACCR_RXMAC_EN)) {
        return -1;
    }

    if (!ftgmac100_filter(s, buf, size)) {
        return size;
    }

    crc = cpu_to_be32(crc32(~0, buf, size));
    /* Increase size by 4, loop below reads the last 4 bytes from crc_ptr. */
    size += 4;
    crc_ptr = (uint8_t *) &crc;

    /* Huge frames are truncated.  */
    if (size > max_frame_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: frame too big : %zd bytes\n",
                      __func__, size);
        size = max_frame_size;
        flags |= FTGMAC100_RXDES0_FTL;
    }

    switch (get_eth_packet_type(PKT_GET_ETH_HDR(buf))) {
    case ETH_PKT_BCAST:
        flags |= FTGMAC100_RXDES0_BROADCAST;
        break;
    case ETH_PKT_MCAST:
        flags |= FTGMAC100_RXDES0_MULTICAST;
        break;
    case ETH_PKT_UCAST:
        break;
    }

    s->isr |= FTGMAC100_INT_RPKT_FIFO;
    addr = s->rx_descriptor;
    while (size > 0) {
        if (!ftgmac100_can_receive(nc)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unexpected packet\n", __func__);
            return -1;
        }

        if (ftgmac100_read_bd(&bd, addr) ||
            (bd.des0 & FTGMAC100_RXDES0_RXPKT_RDY)) {
            /* No descriptors available.  Bail out.  */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Lost end of frame\n",
                          __func__);
            s->isr |= FTGMAC100_INT_NO_RXBUF;
            break;
        }
        buf_len = (size <= s->rbsr) ? size : s->rbsr;
        bd.des0 |= buf_len & 0x3fff;
        size -= buf_len;

        /* The last 4 bytes are the CRC.  */
        if (size < 4) {
            buf_len += size - 4;
        }
        buf_addr = bd.des3;
        if (first && proto == ETH_P_VLAN && buf_len >= 18) {
            bd.des1 = lduw_be_p(buf + 14) | FTGMAC100_RXDES1_VLANTAG_AVAIL;

            if (s->maccr & FTGMAC100_MACCR_RM_VLAN) {
                dma_memory_write(&address_space_memory, buf_addr, buf, 12,
                                 MEMTXATTRS_UNSPECIFIED);
                dma_memory_write(&address_space_memory, buf_addr + 12,
                                 buf + 16, buf_len - 16,
                                 MEMTXATTRS_UNSPECIFIED);
            } else {
                dma_memory_write(&address_space_memory, buf_addr, buf,
                                 buf_len, MEMTXATTRS_UNSPECIFIED);
            }
        } else {
            bd.des1 = 0;
            dma_memory_write(&address_space_memory, buf_addr, buf, buf_len,
                             MEMTXATTRS_UNSPECIFIED);
        }
        buf += buf_len;
        if (size < 4) {
            dma_memory_write(&address_space_memory, buf_addr + buf_len,
                             crc_ptr, 4 - size, MEMTXATTRS_UNSPECIFIED);
            crc_ptr += 4 - size;
        }

        bd.des0 |= first | FTGMAC100_RXDES0_RXPKT_RDY;
        first = 0;
        if (size == 0) {
            /* Last buffer in frame.  */
            bd.des0 |= flags | FTGMAC100_RXDES0_LRS;
            s->isr |= FTGMAC100_INT_RPKT_BUF;
        }
        ftgmac100_write_bd(&bd, addr);
        if (bd.des0 & s->rxdes0_edorr) {
            addr = s->rx_ring;
        } else {
            addr += FTGMAC100_DBLAC_RXDES_SIZE(s->dblac);
        }
    }
    s->rx_descriptor = addr;

    ftgmac100_update_irq(s);
    return len;
}

static const MemoryRegionOps ftgmac100_ops = {
    .read = ftgmac100_read,
    .write = ftgmac100_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ftgmac100_cleanup(NetClientState *nc)
{
    FTGMAC100State *s = FTGMAC100(qemu_get_nic_opaque(nc));

    s->nic = NULL;
}

static NetClientInfo net_ftgmac100_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = ftgmac100_can_receive,
    .receive = ftgmac100_receive,
    .cleanup = ftgmac100_cleanup,
    .link_status_changed = ftgmac100_set_link,
};

static void ftgmac100_realize(DeviceState *dev, Error **errp)
{
    FTGMAC100State *s = FTGMAC100(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->aspeed) {
        s->txdes0_edotr = FTGMAC100_TXDES0_EDOTR_ASPEED;
        s->rxdes0_edorr = FTGMAC100_RXDES0_EDORR_ASPEED;
    } else {
        s->txdes0_edotr = FTGMAC100_TXDES0_EDOTR;
        s->rxdes0_edorr = FTGMAC100_RXDES0_EDORR;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &ftgmac100_ops, s,
                          TYPE_FTGMAC100, 0x2000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_ftgmac100_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static const VMStateDescription vmstate_ftgmac100 = {
    .name = TYPE_FTGMAC100,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(irq_state, FTGMAC100State),
        VMSTATE_UINT32(isr, FTGMAC100State),
        VMSTATE_UINT32(ier, FTGMAC100State),
        VMSTATE_UINT32(rx_enabled, FTGMAC100State),
        VMSTATE_UINT32(rx_ring, FTGMAC100State),
        VMSTATE_UINT32(rbsr, FTGMAC100State),
        VMSTATE_UINT32(tx_ring, FTGMAC100State),
        VMSTATE_UINT32(rx_descriptor, FTGMAC100State),
        VMSTATE_UINT32(tx_descriptor, FTGMAC100State),
        VMSTATE_UINT32_ARRAY(math, FTGMAC100State, 2),
        VMSTATE_UINT32(itc, FTGMAC100State),
        VMSTATE_UINT32(aptcr, FTGMAC100State),
        VMSTATE_UINT32(dblac, FTGMAC100State),
        VMSTATE_UINT32(revr, FTGMAC100State),
        VMSTATE_UINT32(fear1, FTGMAC100State),
        VMSTATE_UINT32(tpafcr, FTGMAC100State),
        VMSTATE_UINT32(maccr, FTGMAC100State),
        VMSTATE_UINT32(phycr, FTGMAC100State),
        VMSTATE_UINT32(phydata, FTGMAC100State),
        VMSTATE_UINT32(fcr, FTGMAC100State),
        VMSTATE_UINT32(phy_status, FTGMAC100State),
        VMSTATE_UINT32(phy_control, FTGMAC100State),
        VMSTATE_UINT32(phy_advertise, FTGMAC100State),
        VMSTATE_UINT32(phy_int, FTGMAC100State),
        VMSTATE_UINT32(phy_int_mask, FTGMAC100State),
        VMSTATE_UINT32(txdes0_edotr, FTGMAC100State),
        VMSTATE_UINT32(rxdes0_edorr, FTGMAC100State),
        VMSTATE_END_OF_LIST()
    }
};

static Property ftgmac100_properties[] = {
    DEFINE_PROP_BOOL("aspeed", FTGMAC100State, aspeed, false),
    DEFINE_NIC_PROPERTIES(FTGMAC100State, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void ftgmac100_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_ftgmac100;
    dc->reset = ftgmac100_reset;
    device_class_set_props(dc, ftgmac100_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->realize = ftgmac100_realize;
    dc->desc = "Faraday FTGMAC100 Gigabit Ethernet emulation";
}

static const TypeInfo ftgmac100_info = {
    .name = TYPE_FTGMAC100,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FTGMAC100State),
    .class_init = ftgmac100_class_init,
};

/*
 * AST2600 MII controller
 */
#define ASPEED_MII_PHYCR_FIRE        BIT(31)
#define ASPEED_MII_PHYCR_ST_22       BIT(28)
#define ASPEED_MII_PHYCR_OP(x)       ((x) & (ASPEED_MII_PHYCR_OP_WRITE | \
                                             ASPEED_MII_PHYCR_OP_READ))
#define ASPEED_MII_PHYCR_OP_WRITE    BIT(26)
#define ASPEED_MII_PHYCR_OP_READ     BIT(27)
#define ASPEED_MII_PHYCR_DATA(x)     (x & 0xffff)
#define ASPEED_MII_PHYCR_PHY(x)      (((x) >> 21) & 0x1f)
#define ASPEED_MII_PHYCR_REG(x)      (((x) >> 16) & 0x1f)

#define ASPEED_MII_PHYDATA_IDLE      BIT(16)

static void aspeed_mii_transition(AspeedMiiState *s, bool fire)
{
    if (fire) {
        s->phycr |= ASPEED_MII_PHYCR_FIRE;
        s->phydata &= ~ASPEED_MII_PHYDATA_IDLE;
    } else {
        s->phycr &= ~ASPEED_MII_PHYCR_FIRE;
        s->phydata |= ASPEED_MII_PHYDATA_IDLE;
    }
}

static void aspeed_mii_do_phy_ctl(AspeedMiiState *s)
{
    uint8_t reg;
    uint16_t data;

    if (!(s->phycr & ASPEED_MII_PHYCR_ST_22)) {
        aspeed_mii_transition(s, !ASPEED_MII_PHYCR_FIRE);
        qemu_log_mask(LOG_UNIMP, "%s: unsupported ST code\n", __func__);
        return;
    }

    /* Nothing to do */
    if (!(s->phycr & ASPEED_MII_PHYCR_FIRE)) {
        return;
    }

    reg = ASPEED_MII_PHYCR_REG(s->phycr);
    data = ASPEED_MII_PHYCR_DATA(s->phycr);

    switch (ASPEED_MII_PHYCR_OP(s->phycr)) {
    case ASPEED_MII_PHYCR_OP_WRITE:
        do_phy_write(s->nic, reg, data);
        break;
    case ASPEED_MII_PHYCR_OP_READ:
        s->phydata = (s->phydata & ~0xffff) | do_phy_read(s->nic, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid OP code %08x\n",
                      __func__, s->phycr);
    }

    aspeed_mii_transition(s, !ASPEED_MII_PHYCR_FIRE);
}

static uint64_t aspeed_mii_read(void *opaque, hwaddr addr, unsigned size)
{
    AspeedMiiState *s = ASPEED_MII(opaque);

    switch (addr) {
    case 0x0:
        return s->phycr;
    case 0x4:
        return s->phydata;
    default:
        g_assert_not_reached();
    }
}

static void aspeed_mii_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    AspeedMiiState *s = ASPEED_MII(opaque);

    switch (addr) {
    case 0x0:
        s->phycr = value & ~(s->phycr & ASPEED_MII_PHYCR_FIRE);
        break;
    case 0x4:
        s->phydata = value & ~(0xffff | ASPEED_MII_PHYDATA_IDLE);
        break;
    default:
        g_assert_not_reached();
    }

    aspeed_mii_transition(s, !!(s->phycr & ASPEED_MII_PHYCR_FIRE));
    aspeed_mii_do_phy_ctl(s);
}

static const MemoryRegionOps aspeed_mii_ops = {
    .read = aspeed_mii_read,
    .write = aspeed_mii_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void aspeed_mii_reset(DeviceState *dev)
{
    AspeedMiiState *s = ASPEED_MII(dev);

    s->phycr = 0;
    s->phydata = 0;

    aspeed_mii_transition(s, !!(s->phycr & ASPEED_MII_PHYCR_FIRE));
};

static void aspeed_mii_realize(DeviceState *dev, Error **errp)
{
    AspeedMiiState *s = ASPEED_MII(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    assert(s->nic);

    memory_region_init_io(&s->iomem, OBJECT(dev), &aspeed_mii_ops, s,
                          TYPE_ASPEED_MII, 0x8);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_mii = {
    .name = TYPE_ASPEED_MII,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(phycr, FTGMAC100State),
        VMSTATE_UINT32(phydata, FTGMAC100State),
        VMSTATE_END_OF_LIST()
    }
};

static Property aspeed_mii_properties[] = {
    DEFINE_PROP_LINK("nic", AspeedMiiState, nic, TYPE_FTGMAC100,
                     FTGMAC100State *),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_mii_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_aspeed_mii;
    dc->reset = aspeed_mii_reset;
    dc->realize = aspeed_mii_realize;
    dc->desc = "Aspeed MII controller";
    device_class_set_props(dc, aspeed_mii_properties);
}

static const TypeInfo aspeed_mii_info = {
    .name = TYPE_ASPEED_MII,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedMiiState),
    .class_init = aspeed_mii_class_init,
};

static void ftgmac100_register_types(void)
{
    type_register_static(&ftgmac100_info);
    type_register_static(&aspeed_mii_info);
}

type_init(ftgmac100_register_types)
