/*
 * QEMU Cadence GEM emulation
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

#include "qemu/osdep.h"
#include <zlib.h> /* For crc32 */

#include "hw/irq.h"
#include "hw/net/cadence_gem.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/dma.h"
#include "net/checksum.h"
#include "net/eth.h"

#define CADENCE_GEM_ERR_DEBUG 0
#define DB_PRINT(...) do {\
    if (CADENCE_GEM_ERR_DEBUG) {   \
        qemu_log(": %s: ", __func__); \
        qemu_log(__VA_ARGS__); \
    } \
} while (0)

#define GEM_NWCTRL        (0x00000000 / 4) /* Network Control reg */
#define GEM_NWCFG         (0x00000004 / 4) /* Network Config reg */
#define GEM_NWSTATUS      (0x00000008 / 4) /* Network Status reg */
#define GEM_USERIO        (0x0000000C / 4) /* User IO reg */
#define GEM_DMACFG        (0x00000010 / 4) /* DMA Control reg */
#define GEM_TXSTATUS      (0x00000014 / 4) /* TX Status reg */
#define GEM_RXQBASE       (0x00000018 / 4) /* RX Q Base address reg */
#define GEM_TXQBASE       (0x0000001C / 4) /* TX Q Base address reg */
#define GEM_RXSTATUS      (0x00000020 / 4) /* RX Status reg */
#define GEM_ISR           (0x00000024 / 4) /* Interrupt Status reg */
#define GEM_IER           (0x00000028 / 4) /* Interrupt Enable reg */
#define GEM_IDR           (0x0000002C / 4) /* Interrupt Disable reg */
#define GEM_IMR           (0x00000030 / 4) /* Interrupt Mask reg */
#define GEM_PHYMNTNC      (0x00000034 / 4) /* Phy Maintenance reg */
#define GEM_RXPAUSE       (0x00000038 / 4) /* RX Pause Time reg */
#define GEM_TXPAUSE       (0x0000003C / 4) /* TX Pause Time reg */
#define GEM_TXPARTIALSF   (0x00000040 / 4) /* TX Partial Store and Forward */
#define GEM_RXPARTIALSF   (0x00000044 / 4) /* RX Partial Store and Forward */
#define GEM_JUMBO_MAX_LEN (0x00000048 / 4) /* Max Jumbo Frame Size */
#define GEM_HASHLO        (0x00000080 / 4) /* Hash Low address reg */
#define GEM_HASHHI        (0x00000084 / 4) /* Hash High address reg */
#define GEM_SPADDR1LO     (0x00000088 / 4) /* Specific addr 1 low reg */
#define GEM_SPADDR1HI     (0x0000008C / 4) /* Specific addr 1 high reg */
#define GEM_SPADDR2LO     (0x00000090 / 4) /* Specific addr 2 low reg */
#define GEM_SPADDR2HI     (0x00000094 / 4) /* Specific addr 2 high reg */
#define GEM_SPADDR3LO     (0x00000098 / 4) /* Specific addr 3 low reg */
#define GEM_SPADDR3HI     (0x0000009C / 4) /* Specific addr 3 high reg */
#define GEM_SPADDR4LO     (0x000000A0 / 4) /* Specific addr 4 low reg */
#define GEM_SPADDR4HI     (0x000000A4 / 4) /* Specific addr 4 high reg */
#define GEM_TIDMATCH1     (0x000000A8 / 4) /* Type ID1 Match reg */
#define GEM_TIDMATCH2     (0x000000AC / 4) /* Type ID2 Match reg */
#define GEM_TIDMATCH3     (0x000000B0 / 4) /* Type ID3 Match reg */
#define GEM_TIDMATCH4     (0x000000B4 / 4) /* Type ID4 Match reg */
#define GEM_WOLAN         (0x000000B8 / 4) /* Wake on LAN reg */
#define GEM_IPGSTRETCH    (0x000000BC / 4) /* IPG Stretch reg */
#define GEM_SVLAN         (0x000000C0 / 4) /* Stacked VLAN reg */
#define GEM_MODID         (0x000000FC / 4) /* Module ID reg */
#define GEM_OCTTXLO       (0x00000100 / 4) /* Octects transmitted Low reg */
#define GEM_OCTTXHI       (0x00000104 / 4) /* Octects transmitted High reg */
#define GEM_TXCNT         (0x00000108 / 4) /* Error-free Frames transmitted */
#define GEM_TXBCNT        (0x0000010C / 4) /* Error-free Broadcast Frames */
#define GEM_TXMCNT        (0x00000110 / 4) /* Error-free Multicast Frame */
#define GEM_TXPAUSECNT    (0x00000114 / 4) /* Pause Frames Transmitted */
#define GEM_TX64CNT       (0x00000118 / 4) /* Error-free 64 TX */
#define GEM_TX65CNT       (0x0000011C / 4) /* Error-free 65-127 TX */
#define GEM_TX128CNT      (0x00000120 / 4) /* Error-free 128-255 TX */
#define GEM_TX256CNT      (0x00000124 / 4) /* Error-free 256-511 */
#define GEM_TX512CNT      (0x00000128 / 4) /* Error-free 512-1023 TX */
#define GEM_TX1024CNT     (0x0000012C / 4) /* Error-free 1024-1518 TX */
#define GEM_TX1519CNT     (0x00000130 / 4) /* Error-free larger than 1519 TX */
#define GEM_TXURUNCNT     (0x00000134 / 4) /* TX under run error counter */
#define GEM_SINGLECOLLCNT (0x00000138 / 4) /* Single Collision Frames */
#define GEM_MULTCOLLCNT   (0x0000013C / 4) /* Multiple Collision Frames */
#define GEM_EXCESSCOLLCNT (0x00000140 / 4) /* Excessive Collision Frames */
#define GEM_LATECOLLCNT   (0x00000144 / 4) /* Late Collision Frames */
#define GEM_DEFERTXCNT    (0x00000148 / 4) /* Deferred Transmission Frames */
#define GEM_CSENSECNT     (0x0000014C / 4) /* Carrier Sense Error Counter */
#define GEM_OCTRXLO       (0x00000150 / 4) /* Octects Received register Low */
#define GEM_OCTRXHI       (0x00000154 / 4) /* Octects Received register High */
#define GEM_RXCNT         (0x00000158 / 4) /* Error-free Frames Received */
#define GEM_RXBROADCNT    (0x0000015C / 4) /* Error-free Broadcast Frames RX */
#define GEM_RXMULTICNT    (0x00000160 / 4) /* Error-free Multicast Frames RX */
#define GEM_RXPAUSECNT    (0x00000164 / 4) /* Pause Frames Received Counter */
#define GEM_RX64CNT       (0x00000168 / 4) /* Error-free 64 byte Frames RX */
#define GEM_RX65CNT       (0x0000016C / 4) /* Error-free 65-127B Frames RX */
#define GEM_RX128CNT      (0x00000170 / 4) /* Error-free 128-255B Frames RX */
#define GEM_RX256CNT      (0x00000174 / 4) /* Error-free 256-512B Frames RX */
#define GEM_RX512CNT      (0x00000178 / 4) /* Error-free 512-1023B Frames RX */
#define GEM_RX1024CNT     (0x0000017C / 4) /* Error-free 1024-1518B Frames RX */
#define GEM_RX1519CNT     (0x00000180 / 4) /* Error-free 1519-max Frames RX */
#define GEM_RXUNDERCNT    (0x00000184 / 4) /* Undersize Frames Received */
#define GEM_RXOVERCNT     (0x00000188 / 4) /* Oversize Frames Received */
#define GEM_RXJABCNT      (0x0000018C / 4) /* Jabbers Received Counter */
#define GEM_RXFCSCNT      (0x00000190 / 4) /* Frame Check seq. Error Counter */
#define GEM_RXLENERRCNT   (0x00000194 / 4) /* Length Field Error Counter */
#define GEM_RXSYMERRCNT   (0x00000198 / 4) /* Symbol Error Counter */
#define GEM_RXALIGNERRCNT (0x0000019C / 4) /* Alignment Error Counter */
#define GEM_RXRSCERRCNT   (0x000001A0 / 4) /* Receive Resource Error Counter */
#define GEM_RXORUNCNT     (0x000001A4 / 4) /* Receive Overrun Counter */
#define GEM_RXIPCSERRCNT  (0x000001A8 / 4) /* IP header Checksum Err Counter */
#define GEM_RXTCPCCNT     (0x000001AC / 4) /* TCP Checksum Error Counter */
#define GEM_RXUDPCCNT     (0x000001B0 / 4) /* UDP Checksum Error Counter */

#define GEM_1588S         (0x000001D0 / 4) /* 1588 Timer Seconds */
#define GEM_1588NS        (0x000001D4 / 4) /* 1588 Timer Nanoseconds */
#define GEM_1588ADJ       (0x000001D8 / 4) /* 1588 Timer Adjust */
#define GEM_1588INC       (0x000001DC / 4) /* 1588 Timer Increment */
#define GEM_PTPETXS       (0x000001E0 / 4) /* PTP Event Frame Transmitted (s) */
#define GEM_PTPETXNS      (0x000001E4 / 4) /*
                                            * PTP Event Frame Transmitted (ns)
                                            */
#define GEM_PTPERXS       (0x000001E8 / 4) /* PTP Event Frame Received (s) */
#define GEM_PTPERXNS      (0x000001EC / 4) /* PTP Event Frame Received (ns) */
#define GEM_PTPPTXS       (0x000001E0 / 4) /* PTP Peer Frame Transmitted (s) */
#define GEM_PTPPTXNS      (0x000001E4 / 4) /* PTP Peer Frame Transmitted (ns) */
#define GEM_PTPPRXS       (0x000001E8 / 4) /* PTP Peer Frame Received (s) */
#define GEM_PTPPRXNS      (0x000001EC / 4) /* PTP Peer Frame Received (ns) */

/* Design Configuration Registers */
#define GEM_DESCONF       (0x00000280 / 4)
#define GEM_DESCONF2      (0x00000284 / 4)
#define GEM_DESCONF3      (0x00000288 / 4)
#define GEM_DESCONF4      (0x0000028C / 4)
#define GEM_DESCONF5      (0x00000290 / 4)
#define GEM_DESCONF6      (0x00000294 / 4)
#define GEM_DESCONF6_64B_MASK (1U << 23)
#define GEM_DESCONF7      (0x00000298 / 4)

#define GEM_INT_Q1_STATUS               (0x00000400 / 4)
#define GEM_INT_Q1_MASK                 (0x00000640 / 4)

#define GEM_TRANSMIT_Q1_PTR             (0x00000440 / 4)
#define GEM_TRANSMIT_Q7_PTR             (GEM_TRANSMIT_Q1_PTR + 6)

#define GEM_RECEIVE_Q1_PTR              (0x00000480 / 4)
#define GEM_RECEIVE_Q7_PTR              (GEM_RECEIVE_Q1_PTR + 6)

#define GEM_TBQPH                       (0x000004C8 / 4)
#define GEM_RBQPH                       (0x000004D4 / 4)

#define GEM_INT_Q1_ENABLE               (0x00000600 / 4)
#define GEM_INT_Q7_ENABLE               (GEM_INT_Q1_ENABLE + 6)

#define GEM_INT_Q1_DISABLE              (0x00000620 / 4)
#define GEM_INT_Q7_DISABLE              (GEM_INT_Q1_DISABLE + 6)

#define GEM_INT_Q1_MASK                 (0x00000640 / 4)
#define GEM_INT_Q7_MASK                 (GEM_INT_Q1_MASK + 6)

#define GEM_SCREENING_TYPE1_REGISTER_0  (0x00000500 / 4)

#define GEM_ST1R_UDP_PORT_MATCH_ENABLE  (1 << 29)
#define GEM_ST1R_DSTC_ENABLE            (1 << 28)
#define GEM_ST1R_UDP_PORT_MATCH_SHIFT   (12)
#define GEM_ST1R_UDP_PORT_MATCH_WIDTH   (27 - GEM_ST1R_UDP_PORT_MATCH_SHIFT + 1)
#define GEM_ST1R_DSTC_MATCH_SHIFT       (4)
#define GEM_ST1R_DSTC_MATCH_WIDTH       (11 - GEM_ST1R_DSTC_MATCH_SHIFT + 1)
#define GEM_ST1R_QUEUE_SHIFT            (0)
#define GEM_ST1R_QUEUE_WIDTH            (3 - GEM_ST1R_QUEUE_SHIFT + 1)

#define GEM_SCREENING_TYPE2_REGISTER_0  (0x00000540 / 4)

#define GEM_ST2R_COMPARE_A_ENABLE       (1 << 18)
#define GEM_ST2R_COMPARE_A_SHIFT        (13)
#define GEM_ST2R_COMPARE_WIDTH          (17 - GEM_ST2R_COMPARE_A_SHIFT + 1)
#define GEM_ST2R_ETHERTYPE_ENABLE       (1 << 12)
#define GEM_ST2R_ETHERTYPE_INDEX_SHIFT  (9)
#define GEM_ST2R_ETHERTYPE_INDEX_WIDTH  (11 - GEM_ST2R_ETHERTYPE_INDEX_SHIFT \
                                            + 1)
#define GEM_ST2R_QUEUE_SHIFT            (0)
#define GEM_ST2R_QUEUE_WIDTH            (3 - GEM_ST2R_QUEUE_SHIFT + 1)

#define GEM_SCREENING_TYPE2_ETHERTYPE_REG_0     (0x000006e0 / 4)
#define GEM_TYPE2_COMPARE_0_WORD_0              (0x00000700 / 4)

#define GEM_T2CW1_COMPARE_OFFSET_SHIFT  (7)
#define GEM_T2CW1_COMPARE_OFFSET_WIDTH  (8 - GEM_T2CW1_COMPARE_OFFSET_SHIFT + 1)
#define GEM_T2CW1_OFFSET_VALUE_SHIFT    (0)
#define GEM_T2CW1_OFFSET_VALUE_WIDTH    (6 - GEM_T2CW1_OFFSET_VALUE_SHIFT + 1)

/*****************************************/
#define GEM_NWCTRL_TXSTART     0x00000200 /* Transmit Enable */
#define GEM_NWCTRL_TXENA       0x00000008 /* Transmit Enable */
#define GEM_NWCTRL_RXENA       0x00000004 /* Receive Enable */
#define GEM_NWCTRL_LOCALLOOP   0x00000002 /* Local Loopback */

#define GEM_NWCFG_STRIP_FCS    0x00020000 /* Strip FCS field */
#define GEM_NWCFG_LERR_DISC    0x00010000 /* Discard RX frames with len err */
#define GEM_NWCFG_BUFF_OFST_M  0x0000C000 /* Receive buffer offset mask */
#define GEM_NWCFG_BUFF_OFST_S  14         /* Receive buffer offset shift */
#define GEM_NWCFG_RCV_1538     0x00000100 /* Receive 1538 bytes frame */
#define GEM_NWCFG_UCAST_HASH   0x00000080 /* accept unicast if hash match */
#define GEM_NWCFG_MCAST_HASH   0x00000040 /* accept multicast if hash match */
#define GEM_NWCFG_BCAST_REJ    0x00000020 /* Reject broadcast packets */
#define GEM_NWCFG_PROMISC      0x00000010 /* Accept all packets */
#define GEM_NWCFG_JUMBO_FRAME  0x00000008 /* Jumbo Frames enable */

#define GEM_DMACFG_ADDR_64B    (1U << 30)
#define GEM_DMACFG_TX_BD_EXT   (1U << 29)
#define GEM_DMACFG_RX_BD_EXT   (1U << 28)
#define GEM_DMACFG_RBUFSZ_M    0x00FF0000 /* DMA RX Buffer Size mask */
#define GEM_DMACFG_RBUFSZ_S    16         /* DMA RX Buffer Size shift */
#define GEM_DMACFG_RBUFSZ_MUL  64         /* DMA RX Buffer Size multiplier */
#define GEM_DMACFG_TXCSUM_OFFL 0x00000800 /* Transmit checksum offload */

#define GEM_TXSTATUS_TXCMPL    0x00000020 /* Transmit Complete */
#define GEM_TXSTATUS_USED      0x00000001 /* sw owned descriptor encountered */

#define GEM_RXSTATUS_FRMRCVD   0x00000002 /* Frame received */
#define GEM_RXSTATUS_NOBUF     0x00000001 /* Buffer unavailable */

/* GEM_ISR GEM_IER GEM_IDR GEM_IMR */
#define GEM_INT_TXCMPL        0x00000080 /* Transmit Complete */
#define GEM_INT_AMBA_ERR      0x00000040
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
#define BOARD_PHY_ADDRESS    0 /* PHY address we will emulate a device at */

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

#define PHY_REG_CONTROL_RST       0x8000
#define PHY_REG_CONTROL_LOOP      0x4000
#define PHY_REG_CONTROL_ANEG      0x1000
#define PHY_REG_CONTROL_ANRESTART 0x0200

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

#define GEM_MODID_VALUE 0x00020118

static inline uint64_t tx_desc_get_buffer(CadenceGEMState *s, uint32_t *desc)
{
    uint64_t ret = desc[0];

    if (s->regs[GEM_DMACFG] & GEM_DMACFG_ADDR_64B) {
        ret |= (uint64_t)desc[2] << 32;
    }
    return ret;
}

static inline unsigned tx_desc_get_used(uint32_t *desc)
{
    return (desc[1] & DESC_1_USED) ? 1 : 0;
}

static inline void tx_desc_set_used(uint32_t *desc)
{
    desc[1] |= DESC_1_USED;
}

static inline unsigned tx_desc_get_wrap(uint32_t *desc)
{
    return (desc[1] & DESC_1_TX_WRAP) ? 1 : 0;
}

static inline unsigned tx_desc_get_last(uint32_t *desc)
{
    return (desc[1] & DESC_1_TX_LAST) ? 1 : 0;
}

static inline unsigned tx_desc_get_length(uint32_t *desc)
{
    return desc[1] & DESC_1_LENGTH;
}

static inline void print_gem_tx_desc(uint32_t *desc, uint8_t queue)
{
    DB_PRINT("TXDESC (queue %" PRId8 "):\n", queue);
    DB_PRINT("bufaddr: 0x%08x\n", *desc);
    DB_PRINT("used_hw: %d\n", tx_desc_get_used(desc));
    DB_PRINT("wrap:    %d\n", tx_desc_get_wrap(desc));
    DB_PRINT("last:    %d\n", tx_desc_get_last(desc));
    DB_PRINT("length:  %d\n", tx_desc_get_length(desc));
}

static inline uint64_t rx_desc_get_buffer(CadenceGEMState *s, uint32_t *desc)
{
    uint64_t ret = desc[0] & ~0x3UL;

    if (s->regs[GEM_DMACFG] & GEM_DMACFG_ADDR_64B) {
        ret |= (uint64_t)desc[2] << 32;
    }
    return ret;
}

static inline int gem_get_desc_len(CadenceGEMState *s, bool rx_n_tx)
{
    int ret = 2;

    if (s->regs[GEM_DMACFG] & GEM_DMACFG_ADDR_64B) {
        ret += 2;
    }
    if (s->regs[GEM_DMACFG] & (rx_n_tx ? GEM_DMACFG_RX_BD_EXT
                                       : GEM_DMACFG_TX_BD_EXT)) {
        ret += 2;
    }

    assert(ret <= DESC_MAX_NUM_WORDS);
    return ret;
}

static inline unsigned rx_desc_get_wrap(uint32_t *desc)
{
    return desc[0] & DESC_0_RX_WRAP ? 1 : 0;
}

static inline unsigned rx_desc_get_ownership(uint32_t *desc)
{
    return desc[0] & DESC_0_RX_OWNERSHIP ? 1 : 0;
}

static inline void rx_desc_set_ownership(uint32_t *desc)
{
    desc[0] |= DESC_0_RX_OWNERSHIP;
}

static inline void rx_desc_set_sof(uint32_t *desc)
{
    desc[1] |= DESC_1_RX_SOF;
}

static inline void rx_desc_clear_control(uint32_t *desc)
{
    desc[1]  = 0;
}

static inline void rx_desc_set_eof(uint32_t *desc)
{
    desc[1] |= DESC_1_RX_EOF;
}

static inline void rx_desc_set_length(uint32_t *desc, unsigned len)
{
    desc[1] &= ~DESC_1_LENGTH;
    desc[1] |= len;
}

static inline void rx_desc_set_broadcast(uint32_t *desc)
{
    desc[1] |= R_DESC_1_RX_BROADCAST;
}

static inline void rx_desc_set_unicast_hash(uint32_t *desc)
{
    desc[1] |= R_DESC_1_RX_UNICAST_HASH;
}

static inline void rx_desc_set_multicast_hash(uint32_t *desc)
{
    desc[1] |= R_DESC_1_RX_MULTICAST_HASH;
}

static inline void rx_desc_set_sar(uint32_t *desc, int sar_idx)
{
    desc[1] = deposit32(desc[1], R_DESC_1_RX_SAR_SHIFT, R_DESC_1_RX_SAR_LENGTH,
                        sar_idx);
    desc[1] |= R_DESC_1_RX_SAR_MATCH;
}

/* The broadcast MAC address: 0xFFFFFFFFFFFF */
static const uint8_t broadcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint32_t gem_get_max_buf_len(CadenceGEMState *s, bool tx)
{
    uint32_t size;
    if (s->regs[GEM_NWCFG] & GEM_NWCFG_JUMBO_FRAME) {
        size = s->regs[GEM_JUMBO_MAX_LEN];
        if (size > s->jumbo_max_len) {
            size = s->jumbo_max_len;
            qemu_log_mask(LOG_GUEST_ERROR, "GEM_JUMBO_MAX_LEN reg cannot be"
                " greater than 0x%" PRIx32 "\n", s->jumbo_max_len);
        }
    } else if (tx) {
        size = 1518;
    } else {
        size = s->regs[GEM_NWCFG] & GEM_NWCFG_RCV_1538 ? 1538 : 1518;
    }
    return size;
}

static void gem_set_isr(CadenceGEMState *s, int q, uint32_t flag)
{
    if (q == 0) {
        s->regs[GEM_ISR] |= flag & ~(s->regs[GEM_IMR]);
    } else {
        s->regs[GEM_INT_Q1_STATUS + q - 1] |= flag &
                                      ~(s->regs[GEM_INT_Q1_MASK + q - 1]);
    }
}

/*
 * gem_init_register_masks:
 * One time initialization.
 * Set masks to identify which register bits have magical clear properties
 */
static void gem_init_register_masks(CadenceGEMState *s)
{
    unsigned int i;
    /* Mask of register bits which are read only */
    memset(&s->regs_ro[0], 0, sizeof(s->regs_ro));
    s->regs_ro[GEM_NWCTRL]   = 0xFFF80000;
    s->regs_ro[GEM_NWSTATUS] = 0xFFFFFFFF;
    s->regs_ro[GEM_DMACFG]   = 0x8E00F000;
    s->regs_ro[GEM_TXSTATUS] = 0xFFFFFE08;
    s->regs_ro[GEM_RXQBASE]  = 0x00000003;
    s->regs_ro[GEM_TXQBASE]  = 0x00000003;
    s->regs_ro[GEM_RXSTATUS] = 0xFFFFFFF0;
    s->regs_ro[GEM_ISR]      = 0xFFFFFFFF;
    s->regs_ro[GEM_IMR]      = 0xFFFFFFFF;
    s->regs_ro[GEM_MODID]    = 0xFFFFFFFF;
    for (i = 0; i < s->num_priority_queues; i++) {
        s->regs_ro[GEM_INT_Q1_STATUS + i] = 0xFFFFFFFF;
        s->regs_ro[GEM_INT_Q1_ENABLE + i] = 0xFFFFF319;
        s->regs_ro[GEM_INT_Q1_DISABLE + i] = 0xFFFFF319;
        s->regs_ro[GEM_INT_Q1_MASK + i] = 0xFFFFFFFF;
    }

    /* Mask of register bits which are clear on read */
    memset(&s->regs_rtc[0], 0, sizeof(s->regs_rtc));
    s->regs_rtc[GEM_ISR]      = 0xFFFFFFFF;
    for (i = 0; i < s->num_priority_queues; i++) {
        s->regs_rtc[GEM_INT_Q1_STATUS + i] = 0x00000CE6;
    }

    /* Mask of register bits which are write 1 to clear */
    memset(&s->regs_w1c[0], 0, sizeof(s->regs_w1c));
    s->regs_w1c[GEM_TXSTATUS] = 0x000001F7;
    s->regs_w1c[GEM_RXSTATUS] = 0x0000000F;

    /* Mask of register bits which are write only */
    memset(&s->regs_wo[0], 0, sizeof(s->regs_wo));
    s->regs_wo[GEM_NWCTRL]   = 0x00073E60;
    s->regs_wo[GEM_IER]      = 0x07FFFFFF;
    s->regs_wo[GEM_IDR]      = 0x07FFFFFF;
    for (i = 0; i < s->num_priority_queues; i++) {
        s->regs_wo[GEM_INT_Q1_ENABLE + i] = 0x00000CE6;
        s->regs_wo[GEM_INT_Q1_DISABLE + i] = 0x00000CE6;
    }
}

/*
 * phy_update_link:
 * Make the emulated PHY link state match the QEMU "interface" state.
 */
static void phy_update_link(CadenceGEMState *s)
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

static bool gem_can_receive(NetClientState *nc)
{
    CadenceGEMState *s;
    int i;

    s = qemu_get_nic_opaque(nc);

    /* Do nothing if receive is not enabled. */
    if (!(s->regs[GEM_NWCTRL] & GEM_NWCTRL_RXENA)) {
        if (s->can_rx_state != 1) {
            s->can_rx_state = 1;
            DB_PRINT("can't receive - no enable\n");
        }
        return false;
    }

    for (i = 0; i < s->num_priority_queues; i++) {
        if (rx_desc_get_ownership(s->rx_desc[i]) != 1) {
            break;
        }
    };

    if (i == s->num_priority_queues) {
        if (s->can_rx_state != 2) {
            s->can_rx_state = 2;
            DB_PRINT("can't receive - all the buffer descriptors are busy\n");
        }
        return false;
    }

    if (s->can_rx_state != 0) {
        s->can_rx_state = 0;
        DB_PRINT("can receive\n");
    }
    return true;
}

/*
 * gem_update_int_status:
 * Raise or lower interrupt based on current status.
 */
static void gem_update_int_status(CadenceGEMState *s)
{
    int i;

    qemu_set_irq(s->irq[0], !!s->regs[GEM_ISR]);

    for (i = 1; i < s->num_priority_queues; ++i) {
        qemu_set_irq(s->irq[i], !!s->regs[GEM_INT_Q1_STATUS + i - 1]);
    }
}

/*
 * gem_receive_updatestats:
 * Increment receive statistics.
 */
static void gem_receive_updatestats(CadenceGEMState *s, const uint8_t *packet,
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
static int gem_mac_address_filter(CadenceGEMState *s, const uint8_t *packet)
{
    uint8_t *gem_spaddr;
    int i, is_mc;

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
    is_mc = is_multicast_ether_addr(packet);
    if ((is_mc && (s->regs[GEM_NWCFG] & GEM_NWCFG_MCAST_HASH)) ||
        (!is_mc && (s->regs[GEM_NWCFG] & GEM_NWCFG_UCAST_HASH))) {
        uint64_t buckets;
        unsigned hash_index;

        hash_index = calc_mac_hash(packet);
        buckets = ((uint64_t)s->regs[GEM_HASHHI] << 32) | s->regs[GEM_HASHLO];
        if ((buckets >> hash_index) & 1) {
            return is_mc ? GEM_RX_MULTICAST_HASH_ACCEPT
                         : GEM_RX_UNICAST_HASH_ACCEPT;
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

/* Figure out which queue the received data should be sent to */
static int get_queue_from_screen(CadenceGEMState *s, uint8_t *rxbuf_ptr,
                                 unsigned rxbufsize)
{
    uint32_t reg;
    bool matched, mismatched;
    int i, j;

    for (i = 0; i < s->num_type1_screeners; i++) {
        reg = s->regs[GEM_SCREENING_TYPE1_REGISTER_0 + i];
        matched = false;
        mismatched = false;

        /* Screening is based on UDP Port */
        if (reg & GEM_ST1R_UDP_PORT_MATCH_ENABLE) {
            uint16_t udp_port = rxbuf_ptr[14 + 22] << 8 | rxbuf_ptr[14 + 23];
            if (udp_port == extract32(reg, GEM_ST1R_UDP_PORT_MATCH_SHIFT,
                                           GEM_ST1R_UDP_PORT_MATCH_WIDTH)) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        /* Screening is based on DS/TC */
        if (reg & GEM_ST1R_DSTC_ENABLE) {
            uint8_t dscp = rxbuf_ptr[14 + 1];
            if (dscp == extract32(reg, GEM_ST1R_DSTC_MATCH_SHIFT,
                                       GEM_ST1R_DSTC_MATCH_WIDTH)) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        if (matched && !mismatched) {
            return extract32(reg, GEM_ST1R_QUEUE_SHIFT, GEM_ST1R_QUEUE_WIDTH);
        }
    }

    for (i = 0; i < s->num_type2_screeners; i++) {
        reg = s->regs[GEM_SCREENING_TYPE2_REGISTER_0 + i];
        matched = false;
        mismatched = false;

        if (reg & GEM_ST2R_ETHERTYPE_ENABLE) {
            uint16_t type = rxbuf_ptr[12] << 8 | rxbuf_ptr[13];
            int et_idx = extract32(reg, GEM_ST2R_ETHERTYPE_INDEX_SHIFT,
                                        GEM_ST2R_ETHERTYPE_INDEX_WIDTH);

            if (et_idx > s->num_type2_screeners) {
                qemu_log_mask(LOG_GUEST_ERROR, "Out of range ethertype "
                              "register index: %d\n", et_idx);
            }
            if (type == s->regs[GEM_SCREENING_TYPE2_ETHERTYPE_REG_0 +
                                et_idx]) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        /* Compare A, B, C */
        for (j = 0; j < 3; j++) {
            uint32_t cr0, cr1, mask;
            uint16_t rx_cmp;
            int offset;
            int cr_idx = extract32(reg, GEM_ST2R_COMPARE_A_SHIFT + j * 6,
                                        GEM_ST2R_COMPARE_WIDTH);

            if (!(reg & (GEM_ST2R_COMPARE_A_ENABLE << (j * 6)))) {
                continue;
            }
            if (cr_idx > s->num_type2_screeners) {
                qemu_log_mask(LOG_GUEST_ERROR, "Out of range compare "
                              "register index: %d\n", cr_idx);
            }

            cr0 = s->regs[GEM_TYPE2_COMPARE_0_WORD_0 + cr_idx * 2];
            cr1 = s->regs[GEM_TYPE2_COMPARE_0_WORD_0 + cr_idx * 2 + 1];
            offset = extract32(cr1, GEM_T2CW1_OFFSET_VALUE_SHIFT,
                                    GEM_T2CW1_OFFSET_VALUE_WIDTH);

            switch (extract32(cr1, GEM_T2CW1_COMPARE_OFFSET_SHIFT,
                                   GEM_T2CW1_COMPARE_OFFSET_WIDTH)) {
            case 3: /* Skip UDP header */
                qemu_log_mask(LOG_UNIMP, "TCP compare offsets"
                              "unimplemented - assuming UDP\n");
                offset += 8;
                /* Fallthrough */
            case 2: /* skip the IP header */
                offset += 20;
                /* Fallthrough */
            case 1: /* Count from after the ethertype */
                offset += 14;
                break;
            case 0:
                /* Offset from start of frame */
                break;
            }

            rx_cmp = rxbuf_ptr[offset] << 8 | rxbuf_ptr[offset];
            mask = extract32(cr0, 0, 16);

            if ((rx_cmp & mask) == (extract32(cr0, 16, 16) & mask)) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        if (matched && !mismatched) {
            return extract32(reg, GEM_ST2R_QUEUE_SHIFT, GEM_ST2R_QUEUE_WIDTH);
        }
    }

    /* We made it here, assume it's queue 0 */
    return 0;
}

static uint32_t gem_get_queue_base_addr(CadenceGEMState *s, bool tx, int q)
{
    uint32_t base_addr = 0;

    switch (q) {
    case 0:
        base_addr = s->regs[tx ? GEM_TXQBASE : GEM_RXQBASE];
        break;
    case 1 ... (MAX_PRIORITY_QUEUES - 1):
        base_addr = s->regs[(tx ? GEM_TRANSMIT_Q1_PTR :
                                 GEM_RECEIVE_Q1_PTR) + q - 1];
        break;
    default:
        g_assert_not_reached();
    };

    return base_addr;
}

static inline uint32_t gem_get_tx_queue_base_addr(CadenceGEMState *s, int q)
{
    return gem_get_queue_base_addr(s, true, q);
}

static inline uint32_t gem_get_rx_queue_base_addr(CadenceGEMState *s, int q)
{
    return gem_get_queue_base_addr(s, false, q);
}

static hwaddr gem_get_desc_addr(CadenceGEMState *s, bool tx, int q)
{
    hwaddr desc_addr = 0;

    if (s->regs[GEM_DMACFG] & GEM_DMACFG_ADDR_64B) {
        desc_addr = s->regs[tx ? GEM_TBQPH : GEM_RBQPH];
    }
    desc_addr <<= 32;
    desc_addr |= tx ? s->tx_desc_addr[q] : s->rx_desc_addr[q];
    return desc_addr;
}

static hwaddr gem_get_tx_desc_addr(CadenceGEMState *s, int q)
{
    return gem_get_desc_addr(s, true, q);
}

static hwaddr gem_get_rx_desc_addr(CadenceGEMState *s, int q)
{
    return gem_get_desc_addr(s, false, q);
}

static void gem_get_rx_desc(CadenceGEMState *s, int q)
{
    hwaddr desc_addr = gem_get_rx_desc_addr(s, q);

    DB_PRINT("read descriptor 0x%" HWADDR_PRIx "\n", desc_addr);

    /* read current descriptor */
    address_space_read(&s->dma_as, desc_addr, MEMTXATTRS_UNSPECIFIED,
                       s->rx_desc[q],
                       sizeof(uint32_t) * gem_get_desc_len(s, true));

    /* Descriptor owned by software ? */
    if (rx_desc_get_ownership(s->rx_desc[q]) == 1) {
        DB_PRINT("descriptor 0x%" HWADDR_PRIx " owned by sw.\n", desc_addr);
        s->regs[GEM_RXSTATUS] |= GEM_RXSTATUS_NOBUF;
        gem_set_isr(s, q, GEM_INT_RXUSED);
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
    CadenceGEMState *s = qemu_get_nic_opaque(nc);
    unsigned   rxbufsize, bytes_to_copy;
    unsigned   rxbuf_offset;
    uint8_t   *rxbuf_ptr;
    bool first_desc = true;
    int maf;
    int q = 0;

    /* Is this destination MAC address "for us" ? */
    maf = gem_mac_address_filter(s, buf);
    if (maf == GEM_RX_REJECT) {
        return size;  /* no, drop siliently b/c it's not an error */
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

    /* Hardware allows a zero value here but warns against it. To avoid QEMU
     * indefinite loops we enforce a minimum value here
     */
    if (rxbufsize < GEM_DMACFG_RBUFSZ_MUL) {
        rxbufsize = GEM_DMACFG_RBUFSZ_MUL;
    }

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

        if (size > MAX_FRAME_SIZE - sizeof(crc_val)) {
            size = MAX_FRAME_SIZE - sizeof(crc_val);
        }
        bytes_to_copy = size;
        /* The application wants the FCS field, which QEMU does not provide.
         * We must try and calculate one.
         */

        memcpy(s->rx_packet, buf, size);
        memset(s->rx_packet + size, 0, MAX_FRAME_SIZE - size);
        rxbuf_ptr = s->rx_packet;
        crc_val = cpu_to_le32(crc32(0, s->rx_packet, MAX(size, 60)));
        memcpy(s->rx_packet + size, &crc_val, sizeof(crc_val));

        bytes_to_copy += 4;
        size += 4;
    }

    DB_PRINT("config bufsize: %u packet size: %zd\n", rxbufsize, size);

    /* Find which queue we are targeting */
    q = get_queue_from_screen(s, rxbuf_ptr, rxbufsize);

    if (size > gem_get_max_buf_len(s, false)) {
        qemu_log_mask(LOG_GUEST_ERROR, "rx frame too long\n");
        gem_set_isr(s, q, GEM_INT_AMBA_ERR);
        return -1;
    }

    while (bytes_to_copy) {
        hwaddr desc_addr;

        /* Do nothing if receive is not enabled. */
        if (!gem_can_receive(nc)) {
            return -1;
        }

        DB_PRINT("copy %" PRIu32 " bytes to 0x%" PRIx64 "\n",
                MIN(bytes_to_copy, rxbufsize),
                rx_desc_get_buffer(s, s->rx_desc[q]));

        /* Copy packet data to emulated DMA buffer */
        address_space_write(&s->dma_as, rx_desc_get_buffer(s, s->rx_desc[q]) +
                                                                  rxbuf_offset,
                            MEMTXATTRS_UNSPECIFIED, rxbuf_ptr,
                            MIN(bytes_to_copy, rxbufsize));
        rxbuf_ptr += MIN(bytes_to_copy, rxbufsize);
        bytes_to_copy -= MIN(bytes_to_copy, rxbufsize);

        rx_desc_clear_control(s->rx_desc[q]);

        /* Update the descriptor.  */
        if (first_desc) {
            rx_desc_set_sof(s->rx_desc[q]);
            first_desc = false;
        }
        if (bytes_to_copy == 0) {
            rx_desc_set_eof(s->rx_desc[q]);
            rx_desc_set_length(s->rx_desc[q], size);
        }
        rx_desc_set_ownership(s->rx_desc[q]);

        switch (maf) {
        case GEM_RX_PROMISCUOUS_ACCEPT:
            break;
        case GEM_RX_BROADCAST_ACCEPT:
            rx_desc_set_broadcast(s->rx_desc[q]);
            break;
        case GEM_RX_UNICAST_HASH_ACCEPT:
            rx_desc_set_unicast_hash(s->rx_desc[q]);
            break;
        case GEM_RX_MULTICAST_HASH_ACCEPT:
            rx_desc_set_multicast_hash(s->rx_desc[q]);
            break;
        case GEM_RX_REJECT:
            abort();
        default: /* SAR */
            rx_desc_set_sar(s->rx_desc[q], maf);
        }

        /* Descriptor write-back.  */
        desc_addr = gem_get_rx_desc_addr(s, q);
        address_space_write(&s->dma_as, desc_addr, MEMTXATTRS_UNSPECIFIED,
                            s->rx_desc[q],
                            sizeof(uint32_t) * gem_get_desc_len(s, true));

        /* Next descriptor */
        if (rx_desc_get_wrap(s->rx_desc[q])) {
            DB_PRINT("wrapping RX descriptor list\n");
            s->rx_desc_addr[q] = gem_get_rx_queue_base_addr(s, q);
        } else {
            DB_PRINT("incrementing RX descriptor list\n");
            s->rx_desc_addr[q] += 4 * gem_get_desc_len(s, true);
        }

        gem_get_rx_desc(s, q);
    }

    /* Count it */
    gem_receive_updatestats(s, buf, size);

    s->regs[GEM_RXSTATUS] |= GEM_RXSTATUS_FRMRCVD;
    gem_set_isr(s, q, GEM_INT_RXCMPL);

    /* Handle interrupt consequences */
    gem_update_int_status(s);

    return size;
}

/*
 * gem_transmit_updatestats:
 * Increment transmit statistics.
 */
static void gem_transmit_updatestats(CadenceGEMState *s, const uint8_t *packet,
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
static void gem_transmit(CadenceGEMState *s)
{
    uint32_t desc[DESC_MAX_NUM_WORDS];
    hwaddr packet_desc_addr;
    uint8_t     *p;
    unsigned    total_bytes;
    int q = 0;

    /* Do nothing if transmit is not enabled. */
    if (!(s->regs[GEM_NWCTRL] & GEM_NWCTRL_TXENA)) {
        return;
    }

    DB_PRINT("\n");

    /* The packet we will hand off to QEMU.
     * Packets scattered across multiple descriptors are gathered to this
     * one contiguous buffer first.
     */
    p = s->tx_packet;
    total_bytes = 0;

    for (q = s->num_priority_queues - 1; q >= 0; q--) {
        /* read current descriptor */
        packet_desc_addr = gem_get_tx_desc_addr(s, q);

        DB_PRINT("read descriptor 0x%" HWADDR_PRIx "\n", packet_desc_addr);
        address_space_read(&s->dma_as, packet_desc_addr,
                           MEMTXATTRS_UNSPECIFIED, desc,
                           sizeof(uint32_t) * gem_get_desc_len(s, false));
        /* Handle all descriptors owned by hardware */
        while (tx_desc_get_used(desc) == 0) {

            /* Do nothing if transmit is not enabled. */
            if (!(s->regs[GEM_NWCTRL] & GEM_NWCTRL_TXENA)) {
                return;
            }
            print_gem_tx_desc(desc, q);

            /* The real hardware would eat this (and possibly crash).
             * For QEMU let's lend a helping hand.
             */
            if ((tx_desc_get_buffer(s, desc) == 0) ||
                (tx_desc_get_length(desc) == 0)) {
                DB_PRINT("Invalid TX descriptor @ 0x%" HWADDR_PRIx "\n",
                         packet_desc_addr);
                break;
            }

            if (tx_desc_get_length(desc) > gem_get_max_buf_len(s, true) -
                                               (p - s->tx_packet)) {
                qemu_log_mask(LOG_GUEST_ERROR, "TX descriptor @ 0x%" \
                         HWADDR_PRIx " too large: size 0x%x space 0x%zx\n",
                         packet_desc_addr, tx_desc_get_length(desc),
                         gem_get_max_buf_len(s, true) - (p - s->tx_packet));
                gem_set_isr(s, q, GEM_INT_AMBA_ERR);
                break;
            }

            /* Gather this fragment of the packet from "dma memory" to our
             * contig buffer.
             */
            address_space_read(&s->dma_as, tx_desc_get_buffer(s, desc),
                               MEMTXATTRS_UNSPECIFIED,
                               p, tx_desc_get_length(desc));
            p += tx_desc_get_length(desc);
            total_bytes += tx_desc_get_length(desc);

            /* Last descriptor for this packet; hand the whole thing off */
            if (tx_desc_get_last(desc)) {
                uint32_t desc_first[DESC_MAX_NUM_WORDS];
                hwaddr desc_addr = gem_get_tx_desc_addr(s, q);

                /* Modify the 1st descriptor of this packet to be owned by
                 * the processor.
                 */
                address_space_read(&s->dma_as, desc_addr,
                                   MEMTXATTRS_UNSPECIFIED, desc_first,
                                   sizeof(desc_first));
                tx_desc_set_used(desc_first);
                address_space_write(&s->dma_as, desc_addr,
                                    MEMTXATTRS_UNSPECIFIED, desc_first,
                                    sizeof(desc_first));
                /* Advance the hardware current descriptor past this packet */
                if (tx_desc_get_wrap(desc)) {
                    s->tx_desc_addr[q] = gem_get_tx_queue_base_addr(s, q);
                } else {
                    s->tx_desc_addr[q] = packet_desc_addr +
                                         4 * gem_get_desc_len(s, false);
                }
                DB_PRINT("TX descriptor next: 0x%08x\n", s->tx_desc_addr[q]);

                s->regs[GEM_TXSTATUS] |= GEM_TXSTATUS_TXCMPL;
                gem_set_isr(s, q, GEM_INT_TXCMPL);

                /* Handle interrupt consequences */
                gem_update_int_status(s);

                /* Is checksum offload enabled? */
                if (s->regs[GEM_DMACFG] & GEM_DMACFG_TXCSUM_OFFL) {
                    net_checksum_calculate(s->tx_packet, total_bytes, CSUM_ALL);
                }

                /* Update MAC statistics */
                gem_transmit_updatestats(s, s->tx_packet, total_bytes);

                /* Send the packet somewhere */
                if (s->phy_loop || (s->regs[GEM_NWCTRL] &
                                    GEM_NWCTRL_LOCALLOOP)) {
                    gem_receive(qemu_get_queue(s->nic), s->tx_packet,
                                total_bytes);
                } else {
                    qemu_send_packet(qemu_get_queue(s->nic), s->tx_packet,
                                     total_bytes);
                }

                /* Prepare for next packet */
                p = s->tx_packet;
                total_bytes = 0;
            }

            /* read next descriptor */
            if (tx_desc_get_wrap(desc)) {

                if (s->regs[GEM_DMACFG] & GEM_DMACFG_ADDR_64B) {
                    packet_desc_addr = s->regs[GEM_TBQPH];
                    packet_desc_addr <<= 32;
                } else {
                    packet_desc_addr = 0;
                }
                packet_desc_addr |= gem_get_tx_queue_base_addr(s, q);
            } else {
                packet_desc_addr += 4 * gem_get_desc_len(s, false);
            }
            DB_PRINT("read descriptor 0x%" HWADDR_PRIx "\n", packet_desc_addr);
            address_space_read(&s->dma_as, packet_desc_addr,
                               MEMTXATTRS_UNSPECIFIED, desc,
                               sizeof(uint32_t) * gem_get_desc_len(s, false));
        }

        if (tx_desc_get_used(desc)) {
            s->regs[GEM_TXSTATUS] |= GEM_TXSTATUS_USED;
            /* IRQ TXUSED is defined only for queue 0 */
            if (q == 0) {
                gem_set_isr(s, 0, GEM_INT_TXUSED);
            }
            gem_update_int_status(s);
        }
    }
}

static void gem_phy_reset(CadenceGEMState *s)
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
    s->phy_regs[PHY_REG_PHYSPCFC_ST] = 0x7C00;
    s->phy_regs[PHY_REG_EXT_PHYSPCFC_CTL] = 0x0C60;
    s->phy_regs[PHY_REG_LED] = 0x4100;
    s->phy_regs[PHY_REG_EXT_PHYSPCFC_CTL2] = 0x000A;
    s->phy_regs[PHY_REG_EXT_PHYSPCFC_ST] = 0x848B;

    phy_update_link(s);
}

static void gem_reset(DeviceState *d)
{
    int i;
    CadenceGEMState *s = CADENCE_GEM(d);
    const uint8_t *a;
    uint32_t queues_mask = 0;

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
    s->regs[GEM_MODID] = s->revision;
    s->regs[GEM_DESCONF] = 0x02D00111;
    s->regs[GEM_DESCONF2] = 0x2ab10000 | s->jumbo_max_len;
    s->regs[GEM_DESCONF5] = 0x002f2045;
    s->regs[GEM_DESCONF6] = GEM_DESCONF6_64B_MASK;
    s->regs[GEM_INT_Q1_MASK] = 0x00000CE6;
    s->regs[GEM_JUMBO_MAX_LEN] = s->jumbo_max_len;

    if (s->num_priority_queues > 1) {
        queues_mask = MAKE_64BIT_MASK(1, s->num_priority_queues - 1);
        s->regs[GEM_DESCONF6] |= queues_mask;
    }

    /* Set MAC address */
    a = &s->conf.macaddr.a[0];
    s->regs[GEM_SPADDR1LO] = a[0] | (a[1] << 8) | (a[2] << 16) | (a[3] << 24);
    s->regs[GEM_SPADDR1HI] = a[4] | (a[5] << 8);

    for (i = 0; i < 4; i++) {
        s->sar_active[i] = false;
    }

    gem_phy_reset(s);

    gem_update_int_status(s);
}

static uint16_t gem_phy_read(CadenceGEMState *s, unsigned reg_num)
{
    DB_PRINT("reg: %d value: 0x%04x\n", reg_num, s->phy_regs[reg_num]);
    return s->phy_regs[reg_num];
}

static void gem_phy_write(CadenceGEMState *s, unsigned reg_num, uint16_t val)
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
            val &= ~(PHY_REG_CONTROL_ANEG | PHY_REG_CONTROL_ANRESTART);
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
    CadenceGEMState *s;
    uint32_t retval;
    s = (CadenceGEMState *)opaque;

    offset >>= 2;
    retval = s->regs[offset];

    DB_PRINT("offset: 0x%04x read: 0x%08x\n", (unsigned)offset*4, retval);

    switch (offset) {
    case GEM_ISR:
        DB_PRINT("lowering irqs on ISR read\n");
        /* The interrupts get updated at the end of the function. */
        break;
    case GEM_PHYMNTNC:
        if (retval & GEM_PHYMNTNC_OP_R) {
            uint32_t phy_addr, reg_num;

            phy_addr = (retval & GEM_PHYMNTNC_ADDR) >> GEM_PHYMNTNC_ADDR_SHFT;
            if (phy_addr == s->phy_addr) {
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
    gem_update_int_status(s);
    return retval;
}

/*
 * gem_write32:
 * Write a GEM register.
 */
static void gem_write(void *opaque, hwaddr offset, uint64_t val,
        unsigned size)
{
    CadenceGEMState *s = (CadenceGEMState *)opaque;
    uint32_t readonly;
    int i;

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
            for (i = 0; i < s->num_priority_queues; ++i) {
                gem_get_rx_desc(s, i);
            }
        }
        if (val & GEM_NWCTRL_TXSTART) {
            gem_transmit(s);
        }
        if (!(val & GEM_NWCTRL_TXENA)) {
            /* Reset to start of Q when transmit disabled. */
            for (i = 0; i < s->num_priority_queues; i++) {
                s->tx_desc_addr[i] = gem_get_tx_queue_base_addr(s, i);
            }
        }
        if (gem_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case GEM_TXSTATUS:
        gem_update_int_status(s);
        break;
    case GEM_RXQBASE:
        s->rx_desc_addr[0] = val;
        break;
    case GEM_RECEIVE_Q1_PTR ... GEM_RECEIVE_Q7_PTR:
        s->rx_desc_addr[offset - GEM_RECEIVE_Q1_PTR + 1] = val;
        break;
    case GEM_TXQBASE:
        s->tx_desc_addr[0] = val;
        break;
    case GEM_TRANSMIT_Q1_PTR ... GEM_TRANSMIT_Q7_PTR:
        s->tx_desc_addr[offset - GEM_TRANSMIT_Q1_PTR + 1] = val;
        break;
    case GEM_RXSTATUS:
        gem_update_int_status(s);
        break;
    case GEM_IER:
        s->regs[GEM_IMR] &= ~val;
        gem_update_int_status(s);
        break;
    case GEM_JUMBO_MAX_LEN:
        s->regs[GEM_JUMBO_MAX_LEN] = val & MAX_JUMBO_FRAME_SIZE_MASK;
        break;
    case GEM_INT_Q1_ENABLE ... GEM_INT_Q7_ENABLE:
        s->regs[GEM_INT_Q1_MASK + offset - GEM_INT_Q1_ENABLE] &= ~val;
        gem_update_int_status(s);
        break;
    case GEM_IDR:
        s->regs[GEM_IMR] |= val;
        gem_update_int_status(s);
        break;
    case GEM_INT_Q1_DISABLE ... GEM_INT_Q7_DISABLE:
        s->regs[GEM_INT_Q1_MASK + offset - GEM_INT_Q1_DISABLE] |= val;
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
            if (phy_addr == s->phy_addr) {
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

static void gem_set_link(NetClientState *nc)
{
    CadenceGEMState *s = qemu_get_nic_opaque(nc);

    DB_PRINT("\n");
    phy_update_link(s);
    gem_update_int_status(s);
}

static NetClientInfo net_gem_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = gem_can_receive,
    .receive = gem_receive,
    .link_status_changed = gem_set_link,
};

static void gem_realize(DeviceState *dev, Error **errp)
{
    CadenceGEMState *s = CADENCE_GEM(dev);
    int i;

    address_space_init(&s->dma_as,
                       s->dma_mr ? s->dma_mr : get_system_memory(), "dma");

    if (s->num_priority_queues == 0 ||
        s->num_priority_queues > MAX_PRIORITY_QUEUES) {
        error_setg(errp, "Invalid num-priority-queues value: %" PRIx8,
                   s->num_priority_queues);
        return;
    } else if (s->num_type1_screeners > MAX_TYPE1_SCREENERS) {
        error_setg(errp, "Invalid num-type1-screeners value: %" PRIx8,
                   s->num_type1_screeners);
        return;
    } else if (s->num_type2_screeners > MAX_TYPE2_SCREENERS) {
        error_setg(errp, "Invalid num-type2-screeners value: %" PRIx8,
                   s->num_type2_screeners);
        return;
    }

    for (i = 0; i < s->num_priority_queues; ++i) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[i]);
    }

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_gem_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);

    if (s->jumbo_max_len > MAX_FRAME_SIZE) {
        error_setg(errp, "jumbo-max-len is greater than %d",
                  MAX_FRAME_SIZE);
        return;
    }
}

static void gem_init(Object *obj)
{
    CadenceGEMState *s = CADENCE_GEM(obj);
    DeviceState *dev = DEVICE(obj);

    DB_PRINT("\n");

    gem_init_register_masks(s);
    memory_region_init_io(&s->iomem, OBJECT(s), &gem_ops, s,
                          "enet", sizeof(s->regs));

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static const VMStateDescription vmstate_cadence_gem = {
    .name = "cadence_gem",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CadenceGEMState, CADENCE_GEM_MAXREG),
        VMSTATE_UINT16_ARRAY(phy_regs, CadenceGEMState, 32),
        VMSTATE_UINT8(phy_loop, CadenceGEMState),
        VMSTATE_UINT32_ARRAY(rx_desc_addr, CadenceGEMState,
                             MAX_PRIORITY_QUEUES),
        VMSTATE_UINT32_ARRAY(tx_desc_addr, CadenceGEMState,
                             MAX_PRIORITY_QUEUES),
        VMSTATE_BOOL_ARRAY(sar_active, CadenceGEMState, 4),
        VMSTATE_END_OF_LIST(),
    }
};

static Property gem_properties[] = {
    DEFINE_NIC_PROPERTIES(CadenceGEMState, conf),
    DEFINE_PROP_UINT32("revision", CadenceGEMState, revision,
                       GEM_MODID_VALUE),
    DEFINE_PROP_UINT8("phy-addr", CadenceGEMState, phy_addr, BOARD_PHY_ADDRESS),
    DEFINE_PROP_UINT8("num-priority-queues", CadenceGEMState,
                      num_priority_queues, 1),
    DEFINE_PROP_UINT8("num-type1-screeners", CadenceGEMState,
                      num_type1_screeners, 4),
    DEFINE_PROP_UINT8("num-type2-screeners", CadenceGEMState,
                      num_type2_screeners, 4),
    DEFINE_PROP_UINT16("jumbo-max-len", CadenceGEMState,
                       jumbo_max_len, 10240),
    DEFINE_PROP_END_OF_LIST(),
};

static void gem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gem_realize;
    device_class_set_props(dc, gem_properties);
    dc->vmsd = &vmstate_cadence_gem;
    dc->reset = gem_reset;
}

static const TypeInfo gem_info = {
    .name  = TYPE_CADENCE_GEM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(CadenceGEMState),
    .instance_init = gem_init,
    .class_init = gem_class_init,
};

static void gem_register_types(void)
{
    type_register_static(&gem_info);
}

type_init(gem_register_types)
