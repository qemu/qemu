/*
 * QEMU Xilinx GEM emulation
 *
 * Copyright (c) 2011 Xilinx, Inc.
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

#include <zlib.h> /* For crc32 */

#include "hw/sysbus.h"
#include "net/net.h"
#include "net/checksum.h"

#ifdef CADENCE_GEM_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0);
#else
    #define DB_PRINT(...)
#endif

#define GEM_NWCTRL        (0x00000000/4) /* Network Control reg */
#define GEM_NWCFG         (0x00000004/4) /* Network Config reg */
#define GEM_NWSTATUS      (0x00000008/4) /* Network Status reg */
#define GEM_USERIO        (0x0000000C/4) /* User IO reg */
#define GEM_DMACFG        (0x00000010/4) /* DMA Control reg */
#define GEM_TXSTATUS      (0x00000014/4) /* TX Status reg */
#define GEM_RXQBASE       (0x00000018/4) /* RX Q Base address reg */
#define GEM_TXQBASE       (0x0000001C/4) /* TX Q Base address reg */
#define GEM_RXSTATUS      (0x00000020/4) /* RX Status reg */
#define GEM_ISR           (0x00000024/4) /* Interrupt Status reg */
#define GEM_IER           (0x00000028/4) /* Interrupt Enable reg */
#define GEM_IDR           (0x0000002C/4) /* Interrupt Disable reg */
#define GEM_IMR           (0x00000030/4) /* Interrupt Mask reg */
#define GEM_PHYMNTNC      (0x00000034/4) /* Phy Maintaince reg */
#define GEM_RXPAUSE       (0x00000038/4) /* RX Pause Time reg */
#define GEM_TXPAUSE       (0x0000003C/4) /* TX Pause Time reg */
#define GEM_TXPARTIALSF   (0x00000040/4) /* TX Partial Store and Forward */
#define GEM_RXPARTIALSF   (0x00000044/4) /* RX Partial Store and Forward */
#define GEM_HASHLO        (0x00000080/4) /* Hash Low address reg */
#define GEM_HASHHI        (0x00000084/4) /* Hash High address reg */
#define GEM_SPADDR1LO     (0x00000088/4) /* Specific addr 1 low reg */
#define GEM_SPADDR1HI     (0x0000008C/4) /* Specific addr 1 high reg */
#define GEM_SPADDR2LO     (0x00000090/4) /* Specific addr 2 low reg */
#define GEM_SPADDR2HI     (0x00000094/4) /* Specific addr 2 high reg */
#define GEM_SPADDR3LO     (0x00000098/4) /* Specific addr 3 low reg */
#define GEM_SPADDR3HI     (0x0000009C/4) /* Specific addr 3 high reg */
#define GEM_SPADDR4LO     (0x000000A0/4) /* Specific addr 4 low reg */
#define GEM_SPADDR4HI     (0x000000A4/4) /* Specific addr 4 high reg */
#define GEM_TIDMATCH1     (0x000000A8/4) /* Type ID1 Match reg */
#define GEM_TIDMATCH2     (0x000000AC/4) /* Type ID2 Match reg */
#define GEM_TIDMATCH3     (0x000000B0/4) /* Type ID3 Match reg */
#define GEM_TIDMATCH4     (0x000000B4/4) /* Type ID4 Match reg */
#define GEM_WOLAN         (0x000000B8/4) /* Wake on LAN reg */
#define GEM_IPGSTRETCH    (0x000000BC/4) /* IPG Stretch reg */
#define GEM_SVLAN         (0x000000C0/4) /* Stacked VLAN reg */
#define GEM_MODID         (0x000000FC/4) /* Module ID reg */
#define GEM_OCTTXLO       (0x00000100/4) /* Octects transmitted Low reg */
#define GEM_OCTTXHI       (0x00000104/4) /* Octects transmitted High reg */
#define GEM_TXCNT         (0x00000108/4) /* Error-free Frames transmitted */
#define GEM_TXBCNT        (0x0000010C/4) /* Error-free Broadcast Frames */
#define GEM_TXMCNT        (0x00000110/4) /* Error-free Multicast Frame */
#define GEM_TXPAUSECNT    (0x00000114/4) /* Pause Frames Transmitted */
#define GEM_TX64CNT       (0x00000118/4) /* Error-free 64 TX */
#define GEM_TX65CNT       (0x0000011C/4) /* Error-free 65-127 TX */
#define GEM_TX128CNT      (0x00000120/4) /* Error-free 128-255 TX */
#define GEM_TX256CNT      (0x00000124/4) /* Error-free 256-511 */
#define GEM_TX512CNT      (0x00000128/4) /* Error-free 512-1023 TX */
#define GEM_TX1024CNT     (0x0000012C/4) /* Error-free 1024-1518 TX */
#define GEM_TX1519CNT     (0x00000130/4) /* Error-free larger than 1519 TX */
#define GEM_TXURUNCNT     (0x00000134/4) /* TX under run error counter */
#define GEM_SINGLECOLLCNT (0x00000138/4) /* Single Collision Frames */
#define GEM_MULTCOLLCNT   (0x0000013C/4) /* Multiple Collision Frames */
#define GEM_EXCESSCOLLCNT (0x00000140/4) /* Excessive Collision Frames */
#define GEM_LATECOLLCNT   (0x00000144/4) /* Late Collision Frames */
#define GEM_DEFERTXCNT    (0x00000148/4) /* Deferred Transmission Frames */
#define GEM_CSENSECNT     (0x0000014C/4) /* Carrier Sense Error Counter */
#define GEM_OCTRXLO       (0x00000150/4) /* Octects Received register Low */
#define GEM_OCTRXHI       (0x00000154/4) /* Octects Received register High */
#define GEM_RXCNT         (0x00000158/4) /* Error-free Frames Received */
#define GEM_RXBROADCNT    (0x0000015C/4) /* Error-free Broadcast Frames RX */
#define GEM_RXMULTICNT    (0x00000160/4) /* Error-free Multicast Frames RX */
#define GEM_RXPAUSECNT    (0x00000164/4) /* Pause Frames Received Counter */
#define GEM_RX64CNT       (0x00000168/4) /* Error-free 64 byte Frames RX */
#define GEM_RX65CNT       (0x0000016C/4) /* Error-free 65-127B Frames RX */
#define GEM_RX128CNT      (0x00000170/4) /* Error-free 128-255B Frames RX */
#define GEM_RX256CNT      (0x00000174/4) /* Error-free 256-512B Frames RX */
#define GEM_RX512CNT      (0x00000178/4) /* Error-free 512-1023B Frames RX */
#define GEM_RX1024CNT     (0x0000017C/4) /* Error-free 1024-1518B Frames RX */
#define GEM_RX1519CNT     (0x00000180/4) /* Error-free 1519-max Frames RX */
#define GEM_RXUNDERCNT    (0x00000184/4) /* Undersize Frames Received */
#define GEM_RXOVERCNT     (0x00000188/4) /* Oversize Frames Received */
#define GEM_RXJABCNT      (0x0000018C/4) /* Jabbers Received Counter */
#define GEM_RXFCSCNT      (0x00000190/4) /* Frame Check seq. Error Counter */
#define GEM_RXLENERRCNT   (0x00000194/4) /* Length Field Error Counter */
#define GEM_RXSYMERRCNT   (0x00000198/4) /* Symbol Error Counter */
#define GEM_RXALIGNERRCNT (0x0000019C/4) /* Alignment Error Counter */
#define GEM_RXRSCERRCNT   (0x000001A0/4) /* Receive Resource Error Counter */
#define GEM_RXORUNCNT     (0x000001A4/4) /* Receive Overrun Counter */
#define GEM_RXIPCSERRCNT  (0x000001A8/4) /* IP header Checksum Error Counter */
#define GEM_RXTCPCCNT     (0x000001AC/4) /* TCP Checksum Error Counter */
#define GEM_RXUDPCCNT     (0x000001B0/4) /* UDP Checksum Error Counter */

#define GEM_1588S         (0x000001D0/4) /* 1588 Timer Seconds */
#define GEM_1588NS        (0x000001D4/4) /* 1588 Timer Nanoseconds */
#define GEM_1588ADJ       (0x000001D8/4) /* 1588 Timer Adjust */
#define GEM_1588INC       (0x000001DC/4) /* 1588 Timer Increment */
#define GEM_PTPETXS       (0x000001E0/4) /* PTP Event Frame Transmitted (s) */
#define GEM_PTPETXNS      (0x000001E4/4) /* PTP Event Frame Transmitted (ns) */
#define GEM_PTPERXS       (0x000001E8/4) /* PTP Event Frame Received (s) */
#define GEM_PTPERXNS      (0x000001EC/4) /* PTP Event Frame Received (ns) */
#define GEM_PTPPTXS       (0x000001E0/4) /* PTP Peer Frame Transmitted (s) */
#define GEM_PTPPTXNS      (0x000001E4/4) /* PTP Peer Frame Transmitted (ns) */
#define GEM_PTPPRXS       (0x000001E8/4) /* PTP Peer Frame Received (s) */
#define GEM_PTPPRXNS      (0x000001EC/4) /* PTP Peer Frame Received (ns) */

/* Design Configuration Registers */
#define GEM_DESCONF       (0x00000280/4)
#define GEM_DESCONF2      (0x00000284/4)
#define GEM_DESCONF3      (0x00000288/4)
#define GEM_DESCONF4      (0x0000028C/4)
#define GEM_DESCONF5      (0x00000290/4)
#define GEM_DESCONF6      (0x00000294/4)
#define GEM_DESCONF7      (0x00000298/4)

#define GEM_MAXREG        (0x00000640/4) /* Last valid GEM address */

/*****************************************/
#define GEM_NWCTRL_TXSTART     0x00000200 /* Transmit Enable */
#define GEM_NWCTRL_TXENA       0x00000008 /* Transmit Enable */
#define GEM_NWCTRL_RXENA       0x00000004 /* Receive Enable */
#define GEM_NWCTRL_LOCALLOOP   0x00000002 /* Local Loopback */

#define GEM_NWCFG_STRIP_FCS    0x00020000 /* Strip FCS field */
#define GEM_NWCFG_LERR_DISC    0x00010000 /* Discard RX frames with lenth err */
#define GEM_NWCFG_BUFF_OFST_M  0x0000C000 /* Receive buffer offset mask */
#define GEM_NWCFG_BUFF_OFST_S  14         /* Receive buffer offset shift */
#define GEM_NWCFG_UCAST_HASH   0x00000080 /* accept unicast if hash match */
#define GEM_NWCFG_MCAST_HASH   0x00000040 /* accept multicast if hash match */
#define GEM_NWCFG_BCAST_REJ    0x00000020 /* Reject broadcast packets */
#define GEM_NWCFG_PROMISC      0x00000010 /* Accept all packets */

#define GEM_DMACFG_RBUFSZ_M    0x007F0000 /* DMA RX Buffer Size mask */
#define GEM_DMACFG_RBUFSZ_S    16         /* DMA RX Buffer Size shift */
#define GEM_DMACFG_RBUFSZ_MUL  64         /* DMA RX Buffer Size multiplier */
#define GEM_DMACFG_TXCSUM_OFFL 0x00000800 /* Transmit checksum offload */

#define GEM_TXSTATUS_TXCMPL    0x00000020 /* Transmit Complete */
#define GEM_TXSTATUS_USED      0x00000001 /* sw owned descriptor encountered */

#define GEM_RXSTATUS_FRMRCVD   0x00000002 /* Frame received */
#define GEM_RXSTATUS_NOBUF     0x00000001 /* Buffer unavailable */

/* GEM_ISR GEM_IER GEM_IDR GEM_IMR */
#define GEM_INT_TXCMPL        0x00000080 /* Transmit Complete */
#define GEM_INT_TXUSED         0x00000008
#define GEM_INT_RXUSED         0x00000004
#define GEM_INT_RXCMPL        0x00000002

#define GEM_PHYMNTNC_OP_R      0x20000000 /* read operation */
#define GEM_PHYMNTNC_OP_W      0x10000000 /* write operation */
#define GEM_PHYMNTNC_ADDR      0x0F800000 /* Address bits */
#define GEM_PHYMNTNC_ADDR_SHFT 23
#define GEM_PHYMNTNC_REG       0x007C0000 /* register bits */
#define GEM_PHYMNTNC_REG_SHIFT 18

/* Marvell PHY definitions */
#define BOARD_PHY_ADDRESS    23 /* PHY address we will emulate a device at */

#define PHY_REG_CONTROL      0
#define PHY_REG_STATUS       1
#define PHY_REG_PHYID1       2
#define PHY_REG_PHYID2       3
#define PHY_REG_ANEGADV      4
#define PHY_REG_LINKPABIL    5
#define PHY_REG_ANEGEXP      6
#define PHY_REG_NEXTP        7
#define PHY_REG_LINKPNEXTP   8
#define PHY_REG_100BTCTRL    9
#define PHY_REG_1000BTSTAT   10
#define PHY_REG_EXTSTAT      15
#define PHY_REG_PHYSPCFC_CTL 16
#define PHY_REG_PHYSPCFC_ST  17
#define PHY_REG_INT_EN       18
#define PHY_REG_INT_ST       19
#define PHY_REG_EXT_PHYSPCFC_CTL  20
#define PHY_REG_RXERR        21
#define PHY_REG_EACD         22
#define PHY_REG_LED          24
#define PHY_REG_LED_OVRD     25
#define PHY_REG_EXT_PHYSPCFC_CTL2 26
#define PHY_REG_EXT_PHYSPCFC_ST   27
#define PHY_REG_CABLE_DIAG   28

#define PHY_REG_CONTROL_RST  0x8000
#define PHY_REG_CONTROL_LOOP 0x4000
#define PHY_REG_CONTROL_ANEG 0x1000

#define PHY_REG_STATUS_LINK     0x0004
#define PHY_REG_STATUS_ANEGCMPL 0x0020

#define PHY_REG_INT_ST_ANEGCMPL 0x0800
#define PHY_REG_INT_ST_LINKC    0x0400
#define PHY_REG_INT_ST_ENERGY   0x0010

/***********************************************************************/
#define GEM_RX_REJECT                   (-1)
#define GEM_RX_PROMISCUOUS_ACCEPT       (-2)
#define GEM_RX_BROADCAST_ACCEPT         (-3)
#define GEM_RX_MULTICAST_HASH_ACCEPT    (-4)
#define GEM_RX_UNICAST_HASH_ACCEPT      (-5)

#define GEM_RX_SAR_ACCEPT               0

/***********************************************************************/

#define DESC_1_USED 0x80000000
#define DESC_1_LENGTH 0x00001FFF

#define DESC_1_TX_WRAP 0x40000000
#define DESC_1_TX_LAST 0x00008000

#define DESC_0_RX_WRAP 0x00000002
#define DESC_0_RX_OWNERSHIP 0x00000001

#define R_DESC_1_RX_SAR_SHIFT           25
#define R_DESC_1_RX_SAR_LENGTH          2
#define R_DESC_1_RX_SAR_MATCH           (1 << 27)
#define R_DESC_1_RX_UNICAST_HASH        (1 << 29)
#define R_DESC_1_RX_MULTICAST_HASH      (1 << 30)
#define R_DESC_1_RX_BROADCAST           (1 << 31)

#define DESC_1_RX_SOF 0x00004000
#define DESC_1_RX_EOF 0x00008000

static inline unsigned tx_desc_get_buffer(unsigned *desc)
{
    return desc[0];
}

static inline unsigned tx_desc_get_used(unsigned *desc)
{
    return (desc[1] & DESC_1_USED) ? 1 : 0;
}

static inline void tx_desc_set_used(unsigned *desc)
{
    desc[1] |= DESC_1_USED;
}

static inline unsigned tx_desc_get_wrap(unsigned *desc)
{
    return (desc[1] & DESC_1_TX_WRAP) ? 1 : 0;
}

static inline unsigned tx_desc_get_last(unsigned *desc)
{
    return (desc[1] & DESC_1_TX_LAST) ? 1 : 0;
}

static inline unsigned tx_desc_get_length(unsigned *desc)
{
    return desc[1] & DESC_1_LENGTH;
}

static inline void print_gem_tx_desc(unsigned *desc)
{
    DB_PRINT("TXDESC:\n");
    DB_PRINT("bufaddr: 0x%08x\n", *desc);
    DB_PRINT("used_hw: %d\n", tx_desc_get_used(desc));
    DB_PRINT("wrap:    %d\n", tx_desc_get_wrap(desc));
    DB_PRINT("last:    %d\n", tx_desc_get_last(desc));
    DB_PRINT("length:  %d\n", tx_desc_get_length(desc));
}

static inline unsigned rx_desc_get_buffer(unsigned *desc)
{
    return desc[0] & ~0x3UL;
}

static inline unsigned rx_desc_get_wrap(unsigned *desc)
{
    return desc[0] & DESC_0_RX_WRAP ? 1 : 0;
}

static inline unsigned rx_desc_get_ownership(unsigned *desc)
{
    return desc[0] & DESC_0_RX_OWNERSHIP ? 1 : 0;
}

static inline void rx_desc_set_ownership(unsigned *desc)
{
    desc[0] |= DESC_0_RX_OWNERSHIP;
}

static inline void rx_desc_set_sof(unsigned *desc)
{
    desc[1] |= DESC_1_RX_SOF;
}

static inline void rx_desc_set_eof(unsigned *desc)
{
    desc[1] |= DESC_1_RX_EOF;
}

static inline void rx_desc_set_length(unsigned *desc, unsigned len)
{
    desc[1] &= ~DESC_1_LENGTH;
    desc[1] |= len;
}

static inline void rx_desc_set_broadcast(unsigned *desc)
{
    desc[1] |= R_DESC_1_RX_BROADCAST;
}

static inline void rx_desc_set_unicast_hash(unsigned *desc)
{
    desc[1] |= R_DESC_1_RX_UNICAST_HASH;
}

static inline void rx_desc_set_multicast_hash(unsigned *desc)
{
    desc[1] |= R_DESC_1_RX_MULTICAST_HASH;
}

static inline void rx_desc_set_sar(unsigned *desc, int sar_idx)
{
    desc[1] = deposit32(desc[1], R_DESC_1_RX_SAR_SHIFT, R_DESC_1_RX_SAR_LENGTH,
                        sar_idx);
    desc[1] |= R_DESC_1_RX_SAR_MATCH;
}

#define TYPE_CADENCE_GEM "cadence_gem"
#define GEM(obj) OBJECT_CHECK(GemState, (obj), TYPE_CADENCE_GEM)

typedef struct GemState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;

    /* GEM registers backing store */
    uint32_t regs[GEM_MAXREG];
    /* Mask of register bits which are write only */
    uint32_t regs_wo[GEM_MAXREG];
    /* Mask of register bits which are read only */
    uint32_t regs_ro[GEM_MAXREG];
    /* Mask of register bits which are clear on read */
    uint32_t regs_rtc[GEM_MAXREG];
    /* Mask of register bits which are write 1 to clear */
    uint32_t regs_w1c[GEM_MAXREG];

    /* PHY registers backing store */
    uint16_t phy_regs[32];

    uint8_t phy_loop; /* Are we in phy loopback? */

    /* The current DMA descriptor pointers */
    uint32_t rx_desc_addr;
    uint32_t tx_desc_addr;

    uint8_t can_rx_state; /* Debug only */

    unsigned rx_desc[2];

    bool sar_active[4];
} GemState;

/* The broadcast MAC address: 0xFFFFFFFFFFFF */
const uint8_t broadcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/*
 * gem_init_register_masks:
 * One time initialization.
 * Set masks to identify which register bits have magical clear properties
 */
static void gem_init_register_masks(GemState *s)
{
    /* Mask of register bits which are read only*/
    memset(&s->regs_ro[0], 0, sizeof(s->regs_ro));
    s->regs_ro[GEM_NWCTRL]   = 0xFFF80000;
    s->regs_ro[GEM_NWSTATUS] = 0xFFFFFFFF;
    s->regs_ro[GEM_DMACFG]   = 0xFE00F000;
    s->regs_ro[GEM_TXSTATUS] = 0xFFFFFE08;
    s->regs_ro[GEM_RXQBASE]  = 0x00000003;
    s->regs_ro[GEM_TXQBASE]  = 0x00000003;
    s->regs_ro[GEM_RXSTATUS] = 0xFFFFFFF0;
    s->regs_ro[GEM_ISR]      = 0xFFFFFFFF;
    s->regs_ro[GEM_IMR]      = 0xFFFFFFFF;
    s->regs_ro[GEM_MODID]    = 0xFFFFFFFF;

    /* Mask of register bits which are clear on read */
    memset(&s->regs_rtc[0], 0, sizeof(s->regs_rtc));
    s->regs_rtc[GEM_ISR]      = 0xFFFFFFFF;

    /* Mask of register bits which are write 1 to clear */
    memset(&s->regs_w1c[0], 0, sizeof(s->regs_w1c));
    s->regs_w1c[GEM_TXSTATUS] = 0x000001F7;
    s->regs_w1c[GEM_RXSTATUS] = 0x0000000F;

    /* Mask of register bits which are write only */
    memset(&s->regs_wo[0], 0, sizeof(s->regs_wo));
    s->regs_wo[GEM_NWCTRL]   = 0x00073E60;
    s->regs_wo[GEM_IER]      = 0x07FFFFFF;
    s->regs_wo[GEM_IDR]      = 0x07FFFFFF;
}

/*
 * phy_update_link:
 * Make the emulated PHY link state match the QEMU "interface" state.
 */
static void phy_update_link(GemState *s)
{
    DB_PRINT("down %d\n", qemu_get_queue(s->nic)->link_down);

    /* Autonegotiation status mirrors link status.  */
    if (qemu_get_queue(s->nic)->link_down) {
        s->phy_regs[PHY_REG_STATUS] &= ~(PHY_REG_STATUS_ANEGCMPL |
                                         PHY_REG_STATUS_LINK);
        s->phy_regs[PHY_REG_INT_ST] |= PHY_REG_INT_ST_LINKC;
    } else {
        s->phy_regs[PHY_REG_STATUS] |= (PHY_REG_STATUS_ANEGCMPL |
                                         PHY_REG_STATUS_LINK);
        s->phy_regs[PHY_REG_INT_ST] |= (PHY_REG_INT_ST_LINKC |
                                        PHY_REG_INT_ST_ANEGCMPL |
                                        PHY_REG_INT_ST_ENERGY);
    }
}

static int gem_can_receive(NetClientState *nc)
{
    GemState *s;

    s = qemu_get_nic_opaque(nc);

    /* Do nothing if receive is not enabled. */
    if (!(s->regs[GEM_NWCTRL] & GEM_NWCTRL_RXENA)) {
        if (s->can_rx_state != 1) {
            s->can_rx_state = 1;
            DB_PRINT("can't receive - no enable\n");
        }
        return 0;
    }

    if (rx_desc_get_ownership(s->rx_desc) == 1) {
        if (s->can_rx_state != 2) {
            s->can_rx_state = 2;
            DB_PRINT("can't receive - busy buffer descriptor 0x%x\n",
                     s->rx_desc_addr);
        }
        return 0;
    }

    if (s->can_rx_state != 0) {
        s->can_rx_state = 0;
        DB_PRINT("can receive 0x%x\n", s->rx_desc_addr);
    }
    return 1;
}

/*
 * gem_update_int_status:
 * Raise or lower interrupt based on current status.
 */
static void gem_update_int_status(GemState *s)
{
    if (s->regs[GEM_ISR]) {
        DB_PRINT("asserting int. (0x%08x)\n", s->regs[GEM_ISR]);
        qemu_set_irq(s->irq, 1);
    }
}

/*
 * gem_receive_updatestats:
 * Increment receive statistics.
 */
static void gem_receive_updatestats(GemState *s, const uint8_t *packet,
                                    unsigned bytes)
{
    uint64_t octets;

    /* Total octets (bytes) received */
    octets = ((uint64_t)(s->regs[GEM_OCTRXLO]) << 32) |
             s->regs[GEM_OCTRXHI];
    octets += bytes;
    s->regs[GEM_OCTRXLO] = octets >> 32;
    s->regs[GEM_OCTRXHI] = octets;

    /* Error-free Frames received */
    s->regs[GEM_RXCNT]++;

    /* Error-free Broadcast Frames counter */
    if (!memcmp(packet, broadcast_addr, 6)) {
        s->regs[GEM_RXBROADCNT]++;
    }

    /* Error-free Multicast Frames counter */
    if (packet[0] == 0x01) {
        s->regs[GEM_RXMULTICNT]++;
    }

    if (bytes <= 64) {
        s->regs[GEM_RX64CNT]++;
    } else if (bytes <= 127) {
        s->regs[GEM_RX65CNT]++;
    } else if (bytes <= 255) {
        s->regs[GEM_RX128CNT]++;
    } else if (bytes <= 511) {
        s->regs[GEM_RX256CNT]++;
    } else if (bytes <= 1023) {
        s->regs[GEM_RX512CNT]++;
    } else if (bytes <= 1518) {
        s->regs[GEM_RX1024CNT]++;
    } else {
        s->regs[GEM_RX1519CNT]++;
    }
}

/*
 * Get the MAC Address bit from the specified position
 */
static unsigned get_bit(const uint8_t *mac, unsigned bit)
{
    unsigned byte;

    byte = mac[bit / 8];
    byte >>= (bit & 0x7);
    byte &= 1;

    return byte;
}

/*
 * Calculate a GEM MAC Address hash index
 */
static unsigned calc_mac_hash(const uint8_t *mac)
{
    int index_bit, mac_bit;
    unsigned hash_index;

    hash_index = 0;
    mac_bit = 5;
    for (index_bit = 5; index_bit >= 0; index_bit--) {
        hash_index |= (get_bit(mac,  mac_bit) ^
                               get_bit(mac, mac_bit + 6) ^
                               get_bit(mac, mac_bit + 12) ^
                               get_bit(mac, mac_bit + 18) ^
                               get_bit(mac, mac_bit + 24) ^
                               get_bit(mac, mac_bit + 30) ^
                               get_bit(mac, mac_bit + 36) ^
                               get_bit(mac, mac_bit + 42)) << index_bit;
        mac_bit--;
    }

    return hash_index;
}

/*
 * gem_mac_address_filter:
 * Accept or reject this destination address?
 * Returns:
 * GEM_RX_REJECT: reject
 * >= 0: Specific address accept (which matched SAR is returned)
 * others for various other modes of accept:
 * GEM_RM_PROMISCUOUS_ACCEPT, GEM_RX_BROADCAST_ACCEPT,
 * GEM_RX_MULTICAST_HASH_ACCEPT or GEM_RX_UNICAST_HASH_ACCEPT
 */
static int gem_mac_address_filter(GemState *s, const uint8_t *packet)
{
    uint8_t *gem_spaddr;
    int i;

    /* Promiscuous mode? */
    if (s->regs[GEM_NWCFG] & GEM_NWCFG_PROMISC) {
        return GEM_RX_PROMISCUOUS_ACCEPT;
    }

    if (!memcmp(packet, broadcast_addr, 6)) {
        /* Reject broadcast packets? */
        if (s->regs[GEM_NWCFG] & GEM_NWCFG_BCAST_REJ) {
            return GEM_RX_REJECT;
        }
        return GEM_RX_BROADCAST_ACCEPT;
    }

    /* Accept packets -w- hash match? */
    if ((packet[0] == 0x01 && (s->regs[GEM_NWCFG] & GEM_NWCFG_MCAST_HASH)) ||
        (packet[0] != 0x01 && (s->regs[GEM_NWCFG] & GEM_NWCFG_UCAST_HASH))) {
        unsigned hash_index;

        hash_index = calc_mac_hash(packet);
        if (hash_index < 32) {
            if (s->regs[GEM_HASHLO] & (1<<hash_index)) {
                return packet[0] == 0x01 ? GEM_RX_MULTICAST_HASH_ACCEPT :
                                           GEM_RX_UNICAST_HASH_ACCEPT;
            }
        } else {
            hash_index -= 32;
            if (s->regs[GEM_HASHHI] & (1<<hash_index)) {
                return packet[0] == 0x01 ? GEM_RX_MULTICAST_HASH_ACCEPT :
                                           GEM_RX_UNICAST_HASH_ACCEPT;
            }
        }
    }

    /* Check all 4 specific addresses */
    gem_spaddr = (uint8_t *)&(s->regs[GEM_SPADDR1LO]);
    for (i = 3; i >= 0; i--) {
        if (s->sar_active[i] && !memcmp(packet, gem_spaddr + 8 * i, 6)) {
            return GEM_RX_SAR_ACCEPT + i;
        }
    }

    /* No address match; reject the packet */
    return GEM_RX_REJECT;
}

static void gem_get_rx_desc(GemState *s)
{
    DB_PRINT("read descriptor 0x%x\n", (unsigned)s->rx_desc_addr);
    /* read current descriptor */
    cpu_physical_memory_read(s->rx_desc_addr,
                             (uint8_t *)s->rx_desc, sizeof(s->rx_desc));

    /* Descriptor owned by software ? */
    if (rx_desc_get_ownership(s->rx_desc) == 1) {
        DB_PRINT("descriptor 0x%x owned by sw.\n",
                 (unsigned)s->rx_desc_addr);
        s->regs[GEM_RXSTATUS] |= GEM_RXSTATUS_NOBUF;
        s->regs[GEM_ISR] |= GEM_INT_RXUSED & ~(s->regs[GEM_IMR]);
        /* Handle interrupt consequences */
        gem_update_int_status(s);
    }
}

/*
 * gem_receive:
 * Fit a packet handed to us by QEMU into the receive descriptor ring.
 */
static ssize_t gem_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    GemState *s;
    unsigned   rxbufsize, bytes_to_copy;
    unsigned   rxbuf_offset;
    uint8_t    rxbuf[2048];
    uint8_t   *rxbuf_ptr;
    bool first_desc = true;
    int maf;

    s = qemu_get_nic_opaque(nc);

    /* Is this destination MAC address "for us" ? */
    maf = gem_mac_address_filter(s, buf);
    if (maf == GEM_RX_REJECT) {
        return -1;
    }

    /* Discard packets with receive length error enabled ? */
    if (s->regs[GEM_NWCFG] & GEM_NWCFG_LERR_DISC) {
        unsigned type_len;

        /* Fish the ethertype / length field out of the RX packet */
        type_len = buf[12] << 8 | buf[13];
        /* It is a length field, not an ethertype */
        if (type_len < 0x600) {
            if (size < type_len) {
                /* discard */
                return -1;
            }
        }
    }

    /*
     * Determine configured receive buffer offset (probably 0)
     */
    rxbuf_offset = (s->regs[GEM_NWCFG] & GEM_NWCFG_BUFF_OFST_M) >>
                   GEM_NWCFG_BUFF_OFST_S;

    /* The configure size of each receive buffer.  Determines how many
     * buffers needed to hold this packet.
     */
    rxbufsize = ((s->regs[GEM_DMACFG] & GEM_DMACFG_RBUFSZ_M) >>
                 GEM_DMACFG_RBUFSZ_S) * GEM_DMACFG_RBUFSZ_MUL;
    bytes_to_copy = size;

    /* Pad to minimum length. Assume FCS field is stripped, logic
     * below will increment it to the real minimum of 64 when
     * not FCS stripping
     */
    if (size < 60) {
        size = 60;
    }

    /* Strip of FCS field ? (usually yes) */
    if (s->regs[GEM_NWCFG] & GEM_NWCFG_STRIP_FCS) {
        rxbuf_ptr = (void *)buf;
    } else {
        unsigned crc_val;
        int      crc_offset;

        /* The application wants the FCS field, which QEMU does not provide.
         * We must try and caclculate one.
         */

        memcpy(rxbuf, buf, size);
        memset(rxbuf + size, 0, sizeof(rxbuf) - size);
        rxbuf_ptr = rxbuf;
        crc_val = cpu_to_le32(crc32(0, rxbuf, MAX(size, 60)));
        if (size < 60) {
            crc_offset = 60;
        } else {
            crc_offset = size;
        }
        memcpy(rxbuf + crc_offset, &crc_val, sizeof(crc_val));

        bytes_to_copy += 4;
        size += 4;
    }

    DB_PRINT("config bufsize: %d packet size: %ld\n", rxbufsize, size);

    while (bytes_to_copy) {
        /* Do nothing if receive is not enabled. */
        if (!gem_can_receive(nc)) {
            assert(!first_desc);
            return -1;
        }

        DB_PRINT("copy %d bytes to 0x%x\n", MIN(bytes_to_copy, rxbufsize),
                rx_desc_get_buffer(s->rx_desc));

        /* Copy packet data to emulated DMA buffer */
        cpu_physical_memory_write(rx_desc_get_buffer(s->rx_desc) + rxbuf_offset,
                                  rxbuf_ptr, MIN(bytes_to_copy, rxbufsize));
        rxbuf_ptr += MIN(bytes_to_copy, rxbufsize);
        bytes_to_copy -= MIN(bytes_to_copy, rxbufsize);

        /* Update the descriptor.  */
        if (first_desc) {
            rx_desc_set_sof(s->rx_desc);
            first_desc = false;
        }
        if (bytes_to_copy == 0) {
            rx_desc_set_eof(s->rx_desc);
            rx_desc_set_length(s->rx_desc, size);
        }
        rx_desc_set_ownership(s->rx_desc);

        switch (maf) {
        case GEM_RX_PROMISCUOUS_ACCEPT:
            break;
        case GEM_RX_BROADCAST_ACCEPT:
            rx_desc_set_broadcast(s->rx_desc);
            break;
        case GEM_RX_UNICAST_HASH_ACCEPT:
            rx_desc_set_unicast_hash(s->rx_desc);
            break;
        case GEM_RX_MULTICAST_HASH_ACCEPT:
            rx_desc_set_multicast_hash(s->rx_desc);
            break;
        case GEM_RX_REJECT:
            abort();
        default: /* SAR */
            rx_desc_set_sar(s->rx_desc, maf);
        }

        /* Descriptor write-back.  */
        cpu_physical_memory_write(s->rx_desc_addr,
                                  (uint8_t *)s->rx_desc, sizeof(s->rx_desc));

        /* Next descriptor */
        if (rx_desc_get_wrap(s->rx_desc)) {
            DB_PRINT("wrapping RX descriptor list\n");
            s->rx_desc_addr = s->regs[GEM_RXQBASE];
        } else {
            DB_PRINT("incrementing RX descriptor list\n");
            s->rx_desc_addr += 8;
        }
        gem_get_rx_desc(s);
    }

    /* Count it */
    gem_receive_updatestats(s, buf, size);

    s->regs[GEM_RXSTATUS] |= GEM_RXSTATUS_FRMRCVD;
    s->regs[GEM_ISR] |= GEM_INT_RXCMPL & ~(s->regs[GEM_IMR]);

    /* Handle interrupt consequences */
    gem_update_int_status(s);

    return size;
}

/*
 * gem_transmit_updatestats:
 * Increment transmit statistics.
 */
static void gem_transmit_updatestats(GemState *s, const uint8_t *packet,
                                     unsigned bytes)
{
    uint64_t octets;

    /* Total octets (bytes) transmitted */
    octets = ((uint64_t)(s->regs[GEM_OCTTXLO]) << 32) |
             s->regs[GEM_OCTTXHI];
    octets += bytes;
    s->regs[GEM_OCTTXLO] = octets >> 32;
    s->regs[GEM_OCTTXHI] = octets;

    /* Error-free Frames transmitted */
    s->regs[GEM_TXCNT]++;

    /* Error-free Broadcast Frames counter */
    if (!memcmp(packet, broadcast_addr, 6)) {
        s->regs[GEM_TXBCNT]++;
    }

    /* Error-free Multicast Frames counter */
    if (packet[0] == 0x01) {
        s->regs[GEM_TXMCNT]++;
    }

    if (bytes <= 64) {
        s->regs[GEM_TX64CNT]++;
    } else if (bytes <= 127) {
        s->regs[GEM_TX65CNT]++;
    } else if (bytes <= 255) {
        s->regs[GEM_TX128CNT]++;
    } else if (bytes <= 511) {
        s->regs[GEM_TX256CNT]++;
    } else if (bytes <= 1023) {
        s->regs[GEM_TX512CNT]++;
    } else if (bytes <= 1518) {
        s->regs[GEM_TX1024CNT]++;
    } else {
        s->regs[GEM_TX1519CNT]++;
    }
}

/*
 * gem_transmit:
 * Fish packets out of the descriptor ring and feed them to QEMU
 */
static void gem_transmit(GemState *s)
{
    unsigned    desc[2];
    hwaddr packet_desc_addr;
    uint8_t     tx_packet[2048];
    uint8_t     *p;
    unsigned    total_bytes;

    /* Do nothing if transmit is not enabled. */
    if (!(s->regs[GEM_NWCTRL] & GEM_NWCTRL_TXENA)) {
        return;
    }

    DB_PRINT("\n");

    /* The packet we will hand off to qemu.
     * Packets scattered across multiple descriptors are gathered to this
     * one contiguous buffer first.
     */
    p = tx_packet;
    total_bytes = 0;

    /* read current descriptor */
    packet_desc_addr = s->tx_desc_addr;
    cpu_physical_memory_read(packet_desc_addr,
                             (uint8_t *)&desc[0], sizeof(desc));
    /* Handle all descriptors owned by hardware */
    while (tx_desc_get_used(desc) == 0) {

        /* Do nothing if transmit is not enabled. */
        if (!(s->regs[GEM_NWCTRL] & GEM_NWCTRL_TXENA)) {
            return;
        }
        print_gem_tx_desc(desc);

        /* The real hardware would eat this (and possibly crash).
         * For QEMU let's lend a helping hand.
         */
        if ((tx_desc_get_buffer(desc) == 0) ||
            (tx_desc_get_length(desc) == 0)) {
            DB_PRINT("Invalid TX descriptor @ 0x%x\n",
                     (unsigned)packet_desc_addr);
            break;
        }

        /* Gather this fragment of the packet from "dma memory" to our contig.
         * buffer.
         */
        cpu_physical_memory_read(tx_desc_get_buffer(desc), p,
                                 tx_desc_get_length(desc));
        p += tx_desc_get_length(desc);
        total_bytes += tx_desc_get_length(desc);

        /* Last descriptor for this packet; hand the whole thing off */
        if (tx_desc_get_last(desc)) {
            /* Modify the 1st descriptor of this packet to be owned by
             * the processor.
             */
            cpu_physical_memory_read(s->tx_desc_addr,
                                     (uint8_t *)&desc[0], sizeof(desc));
            tx_desc_set_used(desc);
            cpu_physical_memory_write(s->tx_desc_addr,
                                      (uint8_t *)&desc[0], sizeof(desc));
            /* Advance the hardare current descriptor past this packet */
            if (tx_desc_get_wrap(desc)) {
                s->tx_desc_addr = s->regs[GEM_TXQBASE];
            } else {
                s->tx_desc_addr = packet_desc_addr + 8;
            }
            DB_PRINT("TX descriptor next: 0x%08x\n", s->tx_desc_addr);

            s->regs[GEM_TXSTATUS] |= GEM_TXSTATUS_TXCMPL;
            s->regs[GEM_ISR] |= GEM_INT_TXCMPL & ~(s->regs[GEM_IMR]);

            /* Handle interrupt consequences */
            gem_update_int_status(s);

            /* Is checksum offload enabled? */
            if (s->regs[GEM_DMACFG] & GEM_DMACFG_TXCSUM_OFFL) {
                net_checksum_calculate(tx_packet, total_bytes);
            }

            /* Update MAC statistics */
            gem_transmit_updatestats(s, tx_packet, total_bytes);

            /* Send the packet somewhere */
            if (s->phy_loop || (s->regs[GEM_NWCTRL] & GEM_NWCTRL_LOCALLOOP)) {
                gem_receive(qemu_get_queue(s->nic), tx_packet, total_bytes);
            } else {
                qemu_send_packet(qemu_get_queue(s->nic), tx_packet,
                                 total_bytes);
            }

            /* Prepare for next packet */
            p = tx_packet;
            total_bytes = 0;
        }

        /* read next descriptor */
        if (tx_desc_get_wrap(desc)) {
            packet_desc_addr = s->regs[GEM_TXQBASE];
        } else {
            packet_desc_addr += 8;
        }
        cpu_physical_memory_read(packet_desc_addr,
                                 (uint8_t *)&desc[0], sizeof(desc));
    }

    if (tx_desc_get_used(desc)) {
        s->regs[GEM_TXSTATUS] |= GEM_TXSTATUS_USED;
        s->regs[GEM_ISR] |= GEM_INT_TXUSED & ~(s->regs[GEM_IMR]);
        gem_update_int_status(s);
    }
}

static void gem_phy_reset(GemState *s)
{
    memset(&s->phy_regs[0], 0, sizeof(s->phy_regs));
    s->phy_regs[PHY_REG_CONTROL] = 0x1140;
    s->phy_regs[PHY_REG_STATUS] = 0x7969;
    s->phy_regs[PHY_REG_PHYID1] = 0x0141;
    s->phy_regs[PHY_REG_PHYID2] = 0x0CC2;
    s->phy_regs[PHY_REG_ANEGADV] = 0x01E1;
    s->phy_regs[PHY_REG_LINKPABIL] = 0xCDE1;
    s->phy_regs[PHY_REG_ANEGEXP] = 0x000F;
    s->phy_regs[PHY_REG_NEXTP] = 0x2001;
    s->phy_regs[PHY_REG_LINKPNEXTP] = 0x40E6;
    s->phy_regs[PHY_REG_100BTCTRL] = 0x0300;
    s->phy_regs[PHY_REG_1000BTSTAT] = 0x7C00;
    s->phy_regs[PHY_REG_EXTSTAT] = 0x3000;
    s->phy_regs[PHY_REG_PHYSPCFC_CTL] = 0x0078;
    s->phy_regs[PHY_REG_PHYSPCFC_ST] = 0xBC00;
    s->phy_regs[PHY_REG_EXT_PHYSPCFC_CTL] = 0x0C60;
    s->phy_regs[PHY_REG_LED] = 0x4100;
    s->phy_regs[PHY_REG_EXT_PHYSPCFC_CTL2] = 0x000A;
    s->phy_regs[PHY_REG_EXT_PHYSPCFC_ST] = 0x848B;

    phy_update_link(s);
}

static void gem_reset(DeviceState *d)
{
    int i;
    GemState *s = GEM(d);

    DB_PRINT("\n");

    /* Set post reset register values */
    memset(&s->regs[0], 0, sizeof(s->regs));
    s->regs[GEM_NWCFG] = 0x00080000;
    s->regs[GEM_NWSTATUS] = 0x00000006;
    s->regs[GEM_DMACFG] = 0x00020784;
    s->regs[GEM_IMR] = 0x07ffffff;
    s->regs[GEM_TXPAUSE] = 0x0000ffff;
    s->regs[GEM_TXPARTIALSF] = 0x000003ff;
    s->regs[GEM_RXPARTIALSF] = 0x000003ff;
    s->regs[GEM_MODID] = 0x00020118;
    s->regs[GEM_DESCONF] = 0x02500111;
    s->regs[GEM_DESCONF2] = 0x2ab13fff;
    s->regs[GEM_DESCONF5] = 0x002f2145;
    s->regs[GEM_DESCONF6] = 0x00000200;

    for (i = 0; i < 4; i++) {
        s->sar_active[i] = false;
    }

    gem_phy_reset(s);

    gem_update_int_status(s);
}

static uint16_t gem_phy_read(GemState *s, unsigned reg_num)
{
    DB_PRINT("reg: %d value: 0x%04x\n", reg_num, s->phy_regs[reg_num]);
    return s->phy_regs[reg_num];
}

static void gem_phy_write(GemState *s, unsigned reg_num, uint16_t val)
{
    DB_PRINT("reg: %d value: 0x%04x\n", reg_num, val);

    switch (reg_num) {
    case PHY_REG_CONTROL:
        if (val & PHY_REG_CONTROL_RST) {
            /* Phy reset */
            gem_phy_reset(s);
            val &= ~(PHY_REG_CONTROL_RST | PHY_REG_CONTROL_LOOP);
            s->phy_loop = 0;
        }
        if (val & PHY_REG_CONTROL_ANEG) {
            /* Complete autonegotiation immediately */
            val &= ~PHY_REG_CONTROL_ANEG;
            s->phy_regs[PHY_REG_STATUS] |= PHY_REG_STATUS_ANEGCMPL;
        }
        if (val & PHY_REG_CONTROL_LOOP) {
            DB_PRINT("PHY placed in loopback\n");
            s->phy_loop = 1;
        } else {
            s->phy_loop = 0;
        }
        break;
    }
    s->phy_regs[reg_num] = val;
}

/*
 * gem_read32:
 * Read a GEM register.
 */
static uint64_t gem_read(void *opaque, hwaddr offset, unsigned size)
{
    GemState *s;
    uint32_t retval;

    s = (GemState *)opaque;

    offset >>= 2;
    retval = s->regs[offset];

    DB_PRINT("offset: 0x%04x read: 0x%08x\n", (unsigned)offset*4, retval);

    switch (offset) {
    case GEM_ISR:
        DB_PRINT("lowering irq on ISR read\n");
        qemu_set_irq(s->irq, 0);
        break;
    case GEM_PHYMNTNC:
        if (retval & GEM_PHYMNTNC_OP_R) {
            uint32_t phy_addr, reg_num;

            phy_addr = (retval & GEM_PHYMNTNC_ADDR) >> GEM_PHYMNTNC_ADDR_SHFT;
            if (phy_addr == BOARD_PHY_ADDRESS) {
                reg_num = (retval & GEM_PHYMNTNC_REG) >> GEM_PHYMNTNC_REG_SHIFT;
                retval &= 0xFFFF0000;
                retval |= gem_phy_read(s, reg_num);
            } else {
                retval |= 0xFFFF; /* No device at this address */
            }
        }
        break;
    }

    /* Squash read to clear bits */
    s->regs[offset] &= ~(s->regs_rtc[offset]);

    /* Do not provide write only bits */
    retval &= ~(s->regs_wo[offset]);

    DB_PRINT("0x%08x\n", retval);
    return retval;
}

/*
 * gem_write32:
 * Write a GEM register.
 */
static void gem_write(void *opaque, hwaddr offset, uint64_t val,
        unsigned size)
{
    GemState *s = (GemState *)opaque;
    uint32_t readonly;

    DB_PRINT("offset: 0x%04x write: 0x%08x ", (unsigned)offset, (unsigned)val);
    offset >>= 2;

    /* Squash bits which are read only in write value */
    val &= ~(s->regs_ro[offset]);
    /* Preserve (only) bits which are read only and wtc in register */
    readonly = s->regs[offset] & (s->regs_ro[offset] | s->regs_w1c[offset]);

    /* Copy register write to backing store */
    s->regs[offset] = (val & ~s->regs_w1c[offset]) | readonly;

    /* do w1c */
    s->regs[offset] &= ~(s->regs_w1c[offset] & val);

    /* Handle register write side effects */
    switch (offset) {
    case GEM_NWCTRL:
        if (val & GEM_NWCTRL_RXENA) {
            gem_get_rx_desc(s);
        }
        if (val & GEM_NWCTRL_TXSTART) {
            gem_transmit(s);
        }
        if (!(val & GEM_NWCTRL_TXENA)) {
            /* Reset to start of Q when transmit disabled. */
            s->tx_desc_addr = s->regs[GEM_TXQBASE];
        }
        if (gem_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case GEM_TXSTATUS:
        gem_update_int_status(s);
        break;
    case GEM_RXQBASE:
        s->rx_desc_addr = val;
        break;
    case GEM_TXQBASE:
        s->tx_desc_addr = val;
        break;
    case GEM_RXSTATUS:
        gem_update_int_status(s);
        break;
    case GEM_IER:
        s->regs[GEM_IMR] &= ~val;
        gem_update_int_status(s);
        break;
    case GEM_IDR:
        s->regs[GEM_IMR] |= val;
        gem_update_int_status(s);
        break;
    case GEM_SPADDR1LO:
    case GEM_SPADDR2LO:
    case GEM_SPADDR3LO:
    case GEM_SPADDR4LO:
        s->sar_active[(offset - GEM_SPADDR1LO) / 2] = false;
        break;
    case GEM_SPADDR1HI:
    case GEM_SPADDR2HI:
    case GEM_SPADDR3HI:
    case GEM_SPADDR4HI:
        s->sar_active[(offset - GEM_SPADDR1HI) / 2] = true;
        break;
    case GEM_PHYMNTNC:
        if (val & GEM_PHYMNTNC_OP_W) {
            uint32_t phy_addr, reg_num;

            phy_addr = (val & GEM_PHYMNTNC_ADDR) >> GEM_PHYMNTNC_ADDR_SHFT;
            if (phy_addr == BOARD_PHY_ADDRESS) {
                reg_num = (val & GEM_PHYMNTNC_REG) >> GEM_PHYMNTNC_REG_SHIFT;
                gem_phy_write(s, reg_num, val);
            }
        }
        break;
    }

    DB_PRINT("newval: 0x%08x\n", s->regs[offset]);
}

static const MemoryRegionOps gem_ops = {
    .read = gem_read,
    .write = gem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void gem_cleanup(NetClientState *nc)
{
    GemState *s = qemu_get_nic_opaque(nc);

    DB_PRINT("\n");
    s->nic = NULL;
}

static void gem_set_link(NetClientState *nc)
{
    DB_PRINT("\n");
    phy_update_link(qemu_get_nic_opaque(nc));
}

static NetClientInfo net_gem_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = gem_can_receive,
    .receive = gem_receive,
    .cleanup = gem_cleanup,
    .link_status_changed = gem_set_link,
};

static int gem_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    GemState *s = GEM(dev);

    DB_PRINT("\n");

    gem_init_register_masks(s);
    memory_region_init_io(&s->iomem, OBJECT(s), &gem_ops, s,
                          "enet", sizeof(s->regs));
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_gem_info, &s->conf,
            object_get_typename(OBJECT(dev)), dev->id, s);

    return 0;
}

static const VMStateDescription vmstate_cadence_gem = {
    .name = "cadence_gem",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, GemState, GEM_MAXREG),
        VMSTATE_UINT16_ARRAY(phy_regs, GemState, 32),
        VMSTATE_UINT8(phy_loop, GemState),
        VMSTATE_UINT32(rx_desc_addr, GemState),
        VMSTATE_UINT32(tx_desc_addr, GemState),
        VMSTATE_BOOL_ARRAY(sar_active, GemState, 4),
        VMSTATE_END_OF_LIST(),
    }
};

static Property gem_properties[] = {
    DEFINE_NIC_PROPERTIES(GemState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void gem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = gem_init;
    dc->props = gem_properties;
    dc->vmsd = &vmstate_cadence_gem;
    dc->reset = gem_reset;
}

static const TypeInfo gem_info = {
    .name  = TYPE_CADENCE_GEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(GemState),
    .class_init = gem_class_init,
};

static void gem_register_types(void)
{
    type_register_static(&gem_info);
}

type_init(gem_register_types)
