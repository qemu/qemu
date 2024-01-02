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
#include "hw/registerfields.h"
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

REG32(NWCTRL, 0x0) /* Network Control reg */
    FIELD(NWCTRL, LOOPBACK , 0, 1)
    FIELD(NWCTRL, LOOPBACK_LOCAL , 1, 1)
    FIELD(NWCTRL, ENABLE_RECEIVE, 2, 1)
    FIELD(NWCTRL, ENABLE_TRANSMIT, 3, 1)
    FIELD(NWCTRL, MAN_PORT_EN , 4, 1)
    FIELD(NWCTRL, CLEAR_ALL_STATS_REGS , 5, 1)
    FIELD(NWCTRL, INC_ALL_STATS_REGS, 6, 1)
    FIELD(NWCTRL, STATS_WRITE_EN, 7, 1)
    FIELD(NWCTRL, BACK_PRESSURE, 8, 1)
    FIELD(NWCTRL, TRANSMIT_START , 9, 1)
    FIELD(NWCTRL, TRANSMIT_HALT, 10, 1)
    FIELD(NWCTRL, TX_PAUSE_FRAME_RE, 11, 1)
    FIELD(NWCTRL, TX_PAUSE_FRAME_ZE, 12, 1)
    FIELD(NWCTRL, STATS_TAKE_SNAP, 13, 1)
    FIELD(NWCTRL, STATS_READ_SNAP, 14, 1)
    FIELD(NWCTRL, STORE_RX_TS, 15, 1)
    FIELD(NWCTRL, PFC_ENABLE, 16, 1)
    FIELD(NWCTRL, PFC_PRIO_BASED, 17, 1)
    FIELD(NWCTRL, FLUSH_RX_PKT_PCLK , 18, 1)
    FIELD(NWCTRL, TX_LPI_EN, 19, 1)
    FIELD(NWCTRL, PTP_UNICAST_ENA, 20, 1)
    FIELD(NWCTRL, ALT_SGMII_MODE, 21, 1)
    FIELD(NWCTRL, STORE_UDP_OFFSET, 22, 1)
    FIELD(NWCTRL, EXT_TSU_PORT_EN, 23, 1)
    FIELD(NWCTRL, ONE_STEP_SYNC_MO, 24, 1)
    FIELD(NWCTRL, PFC_CTRL , 25, 1)
    FIELD(NWCTRL, EXT_RXQ_SEL_EN , 26, 1)
    FIELD(NWCTRL, OSS_CORRECTION_FIELD, 27, 1)
    FIELD(NWCTRL, SEL_MII_ON_RGMII, 28, 1)
    FIELD(NWCTRL, TWO_PT_FIVE_GIG, 29, 1)
    FIELD(NWCTRL, IFG_EATS_QAV_CREDIT, 30, 1)

REG32(NWCFG, 0x4) /* Network Config reg */
    FIELD(NWCFG, SPEED, 0, 1)
    FIELD(NWCFG, FULL_DUPLEX, 1, 1)
    FIELD(NWCFG, DISCARD_NON_VLAN_FRAMES, 2, 1)
    FIELD(NWCFG, JUMBO_FRAMES, 3, 1)
    FIELD(NWCFG, PROMISC, 4, 1)
    FIELD(NWCFG, NO_BROADCAST, 5, 1)
    FIELD(NWCFG, MULTICAST_HASH_EN, 6, 1)
    FIELD(NWCFG, UNICAST_HASH_EN, 7, 1)
    FIELD(NWCFG, RECV_1536_BYTE_FRAMES, 8, 1)
    FIELD(NWCFG, EXTERNAL_ADDR_MATCH_EN, 9, 1)
    FIELD(NWCFG, GIGABIT_MODE_ENABLE, 10, 1)
    FIELD(NWCFG, PCS_SELECT, 11, 1)
    FIELD(NWCFG, RETRY_TEST, 12, 1)
    FIELD(NWCFG, PAUSE_ENABLE, 13, 1)
    FIELD(NWCFG, RECV_BUF_OFFSET, 14, 2)
    FIELD(NWCFG, LEN_ERR_DISCARD, 16, 1)
    FIELD(NWCFG, FCS_REMOVE, 17, 1)
    FIELD(NWCFG, MDC_CLOCK_DIV, 18, 3)
    FIELD(NWCFG, DATA_BUS_WIDTH, 21, 2)
    FIELD(NWCFG, DISABLE_COPY_PAUSE_FRAMES, 23, 1)
    FIELD(NWCFG, RECV_CSUM_OFFLOAD_EN, 24, 1)
    FIELD(NWCFG, EN_HALF_DUPLEX_RX, 25, 1)
    FIELD(NWCFG, IGNORE_RX_FCS, 26, 1)
    FIELD(NWCFG, SGMII_MODE_ENABLE, 27, 1)
    FIELD(NWCFG, IPG_STRETCH_ENABLE, 28, 1)
    FIELD(NWCFG, NSP_ACCEPT, 29, 1)
    FIELD(NWCFG, IGNORE_IPG_RX_ER, 30, 1)
    FIELD(NWCFG, UNI_DIRECTION_ENABLE, 31, 1)

REG32(NWSTATUS, 0x8) /* Network Status reg */
REG32(USERIO, 0xc) /* User IO reg */

REG32(DMACFG, 0x10) /* DMA Control reg */
    FIELD(DMACFG, SEND_BCAST_TO_ALL_QS, 31, 1)
    FIELD(DMACFG, DMA_ADDR_BUS_WIDTH, 30, 1)
    FIELD(DMACFG, TX_BD_EXT_MODE_EN , 29, 1)
    FIELD(DMACFG, RX_BD_EXT_MODE_EN , 28, 1)
    FIELD(DMACFG, FORCE_MAX_AMBA_BURST_TX, 26, 1)
    FIELD(DMACFG, FORCE_MAX_AMBA_BURST_RX, 25, 1)
    FIELD(DMACFG, FORCE_DISCARD_ON_ERR, 24, 1)
    FIELD(DMACFG, RX_BUF_SIZE, 16, 8)
    FIELD(DMACFG, CRC_ERROR_REPORT, 13, 1)
    FIELD(DMACFG, INF_LAST_DBUF_SIZE_EN, 12, 1)
    FIELD(DMACFG, TX_PBUF_CSUM_OFFLOAD, 11, 1)
    FIELD(DMACFG, TX_PBUF_SIZE, 10, 1)
    FIELD(DMACFG, RX_PBUF_SIZE, 8, 2)
    FIELD(DMACFG, ENDIAN_SWAP_PACKET, 7, 1)
    FIELD(DMACFG, ENDIAN_SWAP_MGNT, 6, 1)
    FIELD(DMACFG, HDR_DATA_SPLIT_EN, 5, 1)
    FIELD(DMACFG, AMBA_BURST_LEN , 0, 5)
#define GEM_DMACFG_RBUFSZ_MUL  64         /* DMA RX Buffer Size multiplier */

REG32(TXSTATUS, 0x14) /* TX Status reg */
    FIELD(TXSTATUS, TX_USED_BIT_READ_MIDFRAME, 12, 1)
    FIELD(TXSTATUS, TX_FRAME_TOO_LARGE, 11, 1)
    FIELD(TXSTATUS, TX_DMA_LOCKUP, 10, 1)
    FIELD(TXSTATUS, TX_MAC_LOCKUP, 9, 1)
    FIELD(TXSTATUS, RESP_NOT_OK, 8, 1)
    FIELD(TXSTATUS, LATE_COLLISION, 7, 1)
    FIELD(TXSTATUS, TRANSMIT_UNDER_RUN, 6, 1)
    FIELD(TXSTATUS, TRANSMIT_COMPLETE, 5, 1)
    FIELD(TXSTATUS, AMBA_ERROR, 4, 1)
    FIELD(TXSTATUS, TRANSMIT_GO, 3, 1)
    FIELD(TXSTATUS, RETRY_LIMIT, 2, 1)
    FIELD(TXSTATUS, COLLISION, 1, 1)
    FIELD(TXSTATUS, USED_BIT_READ, 0, 1)

REG32(RXQBASE, 0x18) /* RX Q Base address reg */
REG32(TXQBASE, 0x1c) /* TX Q Base address reg */
REG32(RXSTATUS, 0x20) /* RX Status reg */
    FIELD(RXSTATUS, RX_DMA_LOCKUP, 5, 1)
    FIELD(RXSTATUS, RX_MAC_LOCKUP, 4, 1)
    FIELD(RXSTATUS, RESP_NOT_OK, 3, 1)
    FIELD(RXSTATUS, RECEIVE_OVERRUN, 2, 1)
    FIELD(RXSTATUS, FRAME_RECEIVED, 1, 1)
    FIELD(RXSTATUS, BUF_NOT_AVAILABLE, 0, 1)

REG32(ISR, 0x24) /* Interrupt Status reg */
    FIELD(ISR, TX_LOCKUP, 31, 1)
    FIELD(ISR, RX_LOCKUP, 30, 1)
    FIELD(ISR, TSU_TIMER, 29, 1)
    FIELD(ISR, WOL, 28, 1)
    FIELD(ISR, RECV_LPI, 27, 1)
    FIELD(ISR, TSU_SEC_INCR, 26, 1)
    FIELD(ISR, PTP_PDELAY_RESP_XMIT, 25, 1)
    FIELD(ISR, PTP_PDELAY_REQ_XMIT, 24, 1)
    FIELD(ISR, PTP_PDELAY_RESP_RECV, 23, 1)
    FIELD(ISR, PTP_PDELAY_REQ_RECV, 22, 1)
    FIELD(ISR, PTP_SYNC_XMIT, 21, 1)
    FIELD(ISR, PTP_DELAY_REQ_XMIT, 20, 1)
    FIELD(ISR, PTP_SYNC_RECV, 19, 1)
    FIELD(ISR, PTP_DELAY_REQ_RECV, 18, 1)
    FIELD(ISR, PCS_LP_PAGE_RECV, 17, 1)
    FIELD(ISR, PCS_AN_COMPLETE, 16, 1)
    FIELD(ISR, EXT_IRQ, 15, 1)
    FIELD(ISR, PAUSE_FRAME_XMIT, 14, 1)
    FIELD(ISR, PAUSE_TIME_ELAPSED, 13, 1)
    FIELD(ISR, PAUSE_FRAME_RECV, 12, 1)
    FIELD(ISR, RESP_NOT_OK, 11, 1)
    FIELD(ISR, RECV_OVERRUN, 10, 1)
    FIELD(ISR, LINK_CHANGE, 9, 1)
    FIELD(ISR, USXGMII_INT, 8, 1)
    FIELD(ISR, XMIT_COMPLETE, 7, 1)
    FIELD(ISR, AMBA_ERROR, 6, 1)
    FIELD(ISR, RETRY_EXCEEDED, 5, 1)
    FIELD(ISR, XMIT_UNDER_RUN, 4, 1)
    FIELD(ISR, TX_USED, 3, 1)
    FIELD(ISR, RX_USED, 2, 1)
    FIELD(ISR, RECV_COMPLETE, 1, 1)
    FIELD(ISR, MGNT_FRAME_SENT, 0, 1)
REG32(IER, 0x28) /* Interrupt Enable reg */
REG32(IDR, 0x2c) /* Interrupt Disable reg */
REG32(IMR, 0x30) /* Interrupt Mask reg */

REG32(PHYMNTNC, 0x34) /* Phy Maintenance reg */
    FIELD(PHYMNTNC, DATA, 0, 16)
    FIELD(PHYMNTNC, REG_ADDR, 18, 5)
    FIELD(PHYMNTNC, PHY_ADDR, 23, 5)
    FIELD(PHYMNTNC, OP, 28, 2)
    FIELD(PHYMNTNC, ST, 30, 2)
#define MDIO_OP_READ    0x2
#define MDIO_OP_WRITE   0x1

REG32(RXPAUSE, 0x38) /* RX Pause Time reg */
REG32(TXPAUSE, 0x3c) /* TX Pause Time reg */
REG32(TXPARTIALSF, 0x40) /* TX Partial Store and Forward */
REG32(RXPARTIALSF, 0x44) /* RX Partial Store and Forward */
REG32(JUMBO_MAX_LEN, 0x48) /* Max Jumbo Frame Size */
REG32(HASHLO, 0x80) /* Hash Low address reg */
REG32(HASHHI, 0x84) /* Hash High address reg */
REG32(SPADDR1LO, 0x88) /* Specific addr 1 low reg */
REG32(SPADDR1HI, 0x8c) /* Specific addr 1 high reg */
REG32(SPADDR2LO, 0x90) /* Specific addr 2 low reg */
REG32(SPADDR2HI, 0x94) /* Specific addr 2 high reg */
REG32(SPADDR3LO, 0x98) /* Specific addr 3 low reg */
REG32(SPADDR3HI, 0x9c) /* Specific addr 3 high reg */
REG32(SPADDR4LO, 0xa0) /* Specific addr 4 low reg */
REG32(SPADDR4HI, 0xa4) /* Specific addr 4 high reg */
REG32(TIDMATCH1, 0xa8) /* Type ID1 Match reg */
REG32(TIDMATCH2, 0xac) /* Type ID2 Match reg */
REG32(TIDMATCH3, 0xb0) /* Type ID3 Match reg */
REG32(TIDMATCH4, 0xb4) /* Type ID4 Match reg */
REG32(WOLAN, 0xb8) /* Wake on LAN reg */
REG32(IPGSTRETCH, 0xbc) /* IPG Stretch reg */
REG32(SVLAN, 0xc0) /* Stacked VLAN reg */
REG32(MODID, 0xfc) /* Module ID reg */
REG32(OCTTXLO, 0x100) /* Octets transmitted Low reg */
REG32(OCTTXHI, 0x104) /* Octets transmitted High reg */
REG32(TXCNT, 0x108) /* Error-free Frames transmitted */
REG32(TXBCNT, 0x10c) /* Error-free Broadcast Frames */
REG32(TXMCNT, 0x110) /* Error-free Multicast Frame */
REG32(TXPAUSECNT, 0x114) /* Pause Frames Transmitted */
REG32(TX64CNT, 0x118) /* Error-free 64 TX */
REG32(TX65CNT, 0x11c) /* Error-free 65-127 TX */
REG32(TX128CNT, 0x120) /* Error-free 128-255 TX */
REG32(TX256CNT, 0x124) /* Error-free 256-511 */
REG32(TX512CNT, 0x128) /* Error-free 512-1023 TX */
REG32(TX1024CNT, 0x12c) /* Error-free 1024-1518 TX */
REG32(TX1519CNT, 0x130) /* Error-free larger than 1519 TX */
REG32(TXURUNCNT, 0x134) /* TX under run error counter */
REG32(SINGLECOLLCNT, 0x138) /* Single Collision Frames */
REG32(MULTCOLLCNT, 0x13c) /* Multiple Collision Frames */
REG32(EXCESSCOLLCNT, 0x140) /* Excessive Collision Frames */
REG32(LATECOLLCNT, 0x144) /* Late Collision Frames */
REG32(DEFERTXCNT, 0x148) /* Deferred Transmission Frames */
REG32(CSENSECNT, 0x14c) /* Carrier Sense Error Counter */
REG32(OCTRXLO, 0x150) /* Octets Received register Low */
REG32(OCTRXHI, 0x154) /* Octets Received register High */
REG32(RXCNT, 0x158) /* Error-free Frames Received */
REG32(RXBROADCNT, 0x15c) /* Error-free Broadcast Frames RX */
REG32(RXMULTICNT, 0x160) /* Error-free Multicast Frames RX */
REG32(RXPAUSECNT, 0x164) /* Pause Frames Received Counter */
REG32(RX64CNT, 0x168) /* Error-free 64 byte Frames RX */
REG32(RX65CNT, 0x16c) /* Error-free 65-127B Frames RX */
REG32(RX128CNT, 0x170) /* Error-free 128-255B Frames RX */
REG32(RX256CNT, 0x174) /* Error-free 256-512B Frames RX */
REG32(RX512CNT, 0x178) /* Error-free 512-1023B Frames RX */
REG32(RX1024CNT, 0x17c) /* Error-free 1024-1518B Frames RX */
REG32(RX1519CNT, 0x180) /* Error-free 1519-max Frames RX */
REG32(RXUNDERCNT, 0x184) /* Undersize Frames Received */
REG32(RXOVERCNT, 0x188) /* Oversize Frames Received */
REG32(RXJABCNT, 0x18c) /* Jabbers Received Counter */
REG32(RXFCSCNT, 0x190) /* Frame Check seq. Error Counter */
REG32(RXLENERRCNT, 0x194) /* Length Field Error Counter */
REG32(RXSYMERRCNT, 0x198) /* Symbol Error Counter */
REG32(RXALIGNERRCNT, 0x19c) /* Alignment Error Counter */
REG32(RXRSCERRCNT, 0x1a0) /* Receive Resource Error Counter */
REG32(RXORUNCNT, 0x1a4) /* Receive Overrun Counter */
REG32(RXIPCSERRCNT, 0x1a8) /* IP header Checksum Err Counter */
REG32(RXTCPCCNT, 0x1ac) /* TCP Checksum Error Counter */
REG32(RXUDPCCNT, 0x1b0) /* UDP Checksum Error Counter */

REG32(1588S, 0x1d0) /* 1588 Timer Seconds */
REG32(1588NS, 0x1d4) /* 1588 Timer Nanoseconds */
REG32(1588ADJ, 0x1d8) /* 1588 Timer Adjust */
REG32(1588INC, 0x1dc) /* 1588 Timer Increment */
REG32(PTPETXS, 0x1e0) /* PTP Event Frame Transmitted (s) */
REG32(PTPETXNS, 0x1e4) /* PTP Event Frame Transmitted (ns) */
REG32(PTPERXS, 0x1e8) /* PTP Event Frame Received (s) */
REG32(PTPERXNS, 0x1ec) /* PTP Event Frame Received (ns) */
REG32(PTPPTXS, 0x1e0) /* PTP Peer Frame Transmitted (s) */
REG32(PTPPTXNS, 0x1e4) /* PTP Peer Frame Transmitted (ns) */
REG32(PTPPRXS, 0x1e8) /* PTP Peer Frame Received (s) */
REG32(PTPPRXNS, 0x1ec) /* PTP Peer Frame Received (ns) */

/* Design Configuration Registers */
REG32(DESCONF, 0x280)
REG32(DESCONF2, 0x284)
REG32(DESCONF3, 0x288)
REG32(DESCONF4, 0x28c)
REG32(DESCONF5, 0x290)
REG32(DESCONF6, 0x294)
    FIELD(DESCONF6, DMA_ADDR_64B, 23, 1)
REG32(DESCONF7, 0x298)

REG32(INT_Q1_STATUS, 0x400)
REG32(INT_Q1_MASK, 0x640)

REG32(TRANSMIT_Q1_PTR, 0x440)
REG32(TRANSMIT_Q7_PTR, 0x458)

REG32(RECEIVE_Q1_PTR, 0x480)
REG32(RECEIVE_Q7_PTR, 0x498)

REG32(TBQPH, 0x4c8)
REG32(RBQPH, 0x4d4)

REG32(INT_Q1_ENABLE, 0x600)
REG32(INT_Q7_ENABLE, 0x618)

REG32(INT_Q1_DISABLE, 0x620)
REG32(INT_Q7_DISABLE, 0x638)

REG32(SCREENING_TYPE1_REG0, 0x500)
    FIELD(SCREENING_TYPE1_REG0, QUEUE_NUM, 0, 4)
    FIELD(SCREENING_TYPE1_REG0, DSTC_MATCH, 4, 8)
    FIELD(SCREENING_TYPE1_REG0, UDP_PORT_MATCH, 12, 16)
    FIELD(SCREENING_TYPE1_REG0, DSTC_ENABLE, 28, 1)
    FIELD(SCREENING_TYPE1_REG0, UDP_PORT_MATCH_EN, 29, 1)
    FIELD(SCREENING_TYPE1_REG0, DROP_ON_MATCH, 30, 1)

REG32(SCREENING_TYPE2_REG0, 0x540)
    FIELD(SCREENING_TYPE2_REG0, QUEUE_NUM, 0, 4)
    FIELD(SCREENING_TYPE2_REG0, VLAN_PRIORITY, 4, 3)
    FIELD(SCREENING_TYPE2_REG0, VLAN_ENABLE, 8, 1)
    FIELD(SCREENING_TYPE2_REG0, ETHERTYPE_REG_INDEX, 9, 3)
    FIELD(SCREENING_TYPE2_REG0, ETHERTYPE_ENABLE, 12, 1)
    FIELD(SCREENING_TYPE2_REG0, COMPARE_A, 13, 5)
    FIELD(SCREENING_TYPE2_REG0, COMPARE_A_ENABLE, 18, 1)
    FIELD(SCREENING_TYPE2_REG0, COMPARE_B, 19, 5)
    FIELD(SCREENING_TYPE2_REG0, COMPARE_B_ENABLE, 24, 1)
    FIELD(SCREENING_TYPE2_REG0, COMPARE_C, 25, 5)
    FIELD(SCREENING_TYPE2_REG0, COMPARE_C_ENABLE, 30, 1)
    FIELD(SCREENING_TYPE2_REG0, DROP_ON_MATCH, 31, 1)

REG32(SCREENING_TYPE2_ETHERTYPE_REG0, 0x6e0)

REG32(TYPE2_COMPARE_0_WORD_0, 0x700)
    FIELD(TYPE2_COMPARE_0_WORD_0, MASK_VALUE, 0, 16)
    FIELD(TYPE2_COMPARE_0_WORD_0, COMPARE_VALUE, 16, 16)

REG32(TYPE2_COMPARE_0_WORD_1, 0x704)
    FIELD(TYPE2_COMPARE_0_WORD_1, OFFSET_VALUE, 0, 7)
    FIELD(TYPE2_COMPARE_0_WORD_1, COMPARE_OFFSET, 7, 2)
    FIELD(TYPE2_COMPARE_0_WORD_1, DISABLE_MASK, 9, 1)
    FIELD(TYPE2_COMPARE_0_WORD_1, COMPARE_VLAN_ID, 10, 1)

/*****************************************/



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

    if (FIELD_EX32(s->regs[R_DMACFG], DMACFG, DMA_ADDR_BUS_WIDTH)) {
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

    if (FIELD_EX32(s->regs[R_DMACFG], DMACFG, DMA_ADDR_BUS_WIDTH)) {
        ret |= (uint64_t)desc[2] << 32;
    }
    return ret;
}

static inline int gem_get_desc_len(CadenceGEMState *s, bool rx_n_tx)
{
    int ret = 2;

    if (FIELD_EX32(s->regs[R_DMACFG], DMACFG, DMA_ADDR_BUS_WIDTH)) {
        ret += 2;
    }
    if (s->regs[R_DMACFG] & (rx_n_tx ? R_DMACFG_RX_BD_EXT_MODE_EN_MASK
                                     : R_DMACFG_TX_BD_EXT_MODE_EN_MASK)) {
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
    if (FIELD_EX32(s->regs[R_NWCFG], NWCFG, JUMBO_FRAMES)) {
        size = s->regs[R_JUMBO_MAX_LEN];
        if (size > s->jumbo_max_len) {
            size = s->jumbo_max_len;
            qemu_log_mask(LOG_GUEST_ERROR, "GEM_JUMBO_MAX_LEN reg cannot be"
                " greater than 0x%" PRIx32 "\n", s->jumbo_max_len);
        }
    } else if (tx) {
        size = 1518;
    } else {
        size = FIELD_EX32(s->regs[R_NWCFG],
                          NWCFG, RECV_1536_BYTE_FRAMES) ? 1538 : 1518;
    }
    return size;
}

static void gem_set_isr(CadenceGEMState *s, int q, uint32_t flag)
{
    if (q == 0) {
        s->regs[R_ISR] |= flag & ~(s->regs[R_IMR]);
    } else {
        s->regs[R_INT_Q1_STATUS + q - 1] |= flag &
                                      ~(s->regs[R_INT_Q1_MASK + q - 1]);
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
    s->regs_ro[R_NWCTRL]   = 0xFFF80000;
    s->regs_ro[R_NWSTATUS] = 0xFFFFFFFF;
    s->regs_ro[R_DMACFG]   = 0x8E00F000;
    s->regs_ro[R_TXSTATUS] = 0xFFFFFE08;
    s->regs_ro[R_RXQBASE]  = 0x00000003;
    s->regs_ro[R_TXQBASE]  = 0x00000003;
    s->regs_ro[R_RXSTATUS] = 0xFFFFFFF0;
    s->regs_ro[R_ISR]      = 0xFFFFFFFF;
    s->regs_ro[R_IMR]      = 0xFFFFFFFF;
    s->regs_ro[R_MODID]    = 0xFFFFFFFF;
    for (i = 0; i < s->num_priority_queues; i++) {
        s->regs_ro[R_INT_Q1_STATUS + i] = 0xFFFFFFFF;
        s->regs_ro[R_INT_Q1_ENABLE + i] = 0xFFFFF319;
        s->regs_ro[R_INT_Q1_DISABLE + i] = 0xFFFFF319;
        s->regs_ro[R_INT_Q1_MASK + i] = 0xFFFFFFFF;
    }

    /* Mask of register bits which are clear on read */
    memset(&s->regs_rtc[0], 0, sizeof(s->regs_rtc));
    s->regs_rtc[R_ISR]      = 0xFFFFFFFF;
    for (i = 0; i < s->num_priority_queues; i++) {
        s->regs_rtc[R_INT_Q1_STATUS + i] = 0x00000CE6;
    }

    /* Mask of register bits which are write 1 to clear */
    memset(&s->regs_w1c[0], 0, sizeof(s->regs_w1c));
    s->regs_w1c[R_TXSTATUS] = 0x000001F7;
    s->regs_w1c[R_RXSTATUS] = 0x0000000F;

    /* Mask of register bits which are write only */
    memset(&s->regs_wo[0], 0, sizeof(s->regs_wo));
    s->regs_wo[R_NWCTRL]   = 0x00073E60;
    s->regs_wo[R_IER]      = 0x07FFFFFF;
    s->regs_wo[R_IDR]      = 0x07FFFFFF;
    for (i = 0; i < s->num_priority_queues; i++) {
        s->regs_wo[R_INT_Q1_ENABLE + i] = 0x00000CE6;
        s->regs_wo[R_INT_Q1_DISABLE + i] = 0x00000CE6;
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
    if (!FIELD_EX32(s->regs[R_NWCTRL], NWCTRL, ENABLE_RECEIVE)) {
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

    qemu_set_irq(s->irq[0], !!s->regs[R_ISR]);

    for (i = 1; i < s->num_priority_queues; ++i) {
        qemu_set_irq(s->irq[i], !!s->regs[R_INT_Q1_STATUS + i - 1]);
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
    octets = ((uint64_t)(s->regs[R_OCTRXLO]) << 32) |
             s->regs[R_OCTRXHI];
    octets += bytes;
    s->regs[R_OCTRXLO] = octets >> 32;
    s->regs[R_OCTRXHI] = octets;

    /* Error-free Frames received */
    s->regs[R_RXCNT]++;

    /* Error-free Broadcast Frames counter */
    if (!memcmp(packet, broadcast_addr, 6)) {
        s->regs[R_RXBROADCNT]++;
    }

    /* Error-free Multicast Frames counter */
    if (packet[0] == 0x01) {
        s->regs[R_RXMULTICNT]++;
    }

    if (bytes <= 64) {
        s->regs[R_RX64CNT]++;
    } else if (bytes <= 127) {
        s->regs[R_RX65CNT]++;
    } else if (bytes <= 255) {
        s->regs[R_RX128CNT]++;
    } else if (bytes <= 511) {
        s->regs[R_RX256CNT]++;
    } else if (bytes <= 1023) {
        s->regs[R_RX512CNT]++;
    } else if (bytes <= 1518) {
        s->regs[R_RX1024CNT]++;
    } else {
        s->regs[R_RX1519CNT]++;
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
    if (FIELD_EX32(s->regs[R_NWCFG], NWCFG, PROMISC)) {
        return GEM_RX_PROMISCUOUS_ACCEPT;
    }

    if (!memcmp(packet, broadcast_addr, 6)) {
        /* Reject broadcast packets? */
        if (FIELD_EX32(s->regs[R_NWCFG], NWCFG, NO_BROADCAST)) {
            return GEM_RX_REJECT;
        }
        return GEM_RX_BROADCAST_ACCEPT;
    }

    /* Accept packets -w- hash match? */
    is_mc = is_multicast_ether_addr(packet);
    if ((is_mc && (FIELD_EX32(s->regs[R_NWCFG], NWCFG, MULTICAST_HASH_EN))) ||
        (!is_mc && FIELD_EX32(s->regs[R_NWCFG], NWCFG, UNICAST_HASH_EN))) {
        uint64_t buckets;
        unsigned hash_index;

        hash_index = calc_mac_hash(packet);
        buckets = ((uint64_t)s->regs[R_HASHHI] << 32) | s->regs[R_HASHLO];
        if ((buckets >> hash_index) & 1) {
            return is_mc ? GEM_RX_MULTICAST_HASH_ACCEPT
                         : GEM_RX_UNICAST_HASH_ACCEPT;
        }
    }

    /* Check all 4 specific addresses */
    gem_spaddr = (uint8_t *)&(s->regs[R_SPADDR1LO]);
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
        reg = s->regs[R_SCREENING_TYPE1_REG0 + i];
        matched = false;
        mismatched = false;

        /* Screening is based on UDP Port */
        if (FIELD_EX32(reg, SCREENING_TYPE1_REG0, UDP_PORT_MATCH_EN)) {
            uint16_t udp_port = rxbuf_ptr[14 + 22] << 8 | rxbuf_ptr[14 + 23];
            if (udp_port == FIELD_EX32(reg, SCREENING_TYPE1_REG0, UDP_PORT_MATCH)) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        /* Screening is based on DS/TC */
        if (FIELD_EX32(reg, SCREENING_TYPE1_REG0, DSTC_ENABLE)) {
            uint8_t dscp = rxbuf_ptr[14 + 1];
            if (dscp == FIELD_EX32(reg, SCREENING_TYPE1_REG0, DSTC_MATCH)) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        if (matched && !mismatched) {
            return FIELD_EX32(reg, SCREENING_TYPE1_REG0, QUEUE_NUM);
        }
    }

    for (i = 0; i < s->num_type2_screeners; i++) {
        reg = s->regs[R_SCREENING_TYPE2_REG0 + i];
        matched = false;
        mismatched = false;

        if (FIELD_EX32(reg, SCREENING_TYPE2_REG0, ETHERTYPE_ENABLE)) {
            uint16_t type = rxbuf_ptr[12] << 8 | rxbuf_ptr[13];
            int et_idx = FIELD_EX32(reg, SCREENING_TYPE2_REG0,
                                    ETHERTYPE_REG_INDEX);

            if (et_idx > s->num_type2_screeners) {
                qemu_log_mask(LOG_GUEST_ERROR, "Out of range ethertype "
                              "register index: %d\n", et_idx);
            }
            if (type == s->regs[R_SCREENING_TYPE2_ETHERTYPE_REG0 +
                                et_idx]) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        /* Compare A, B, C */
        for (j = 0; j < 3; j++) {
            uint32_t cr0, cr1, mask, compare;
            uint16_t rx_cmp;
            int offset;
            int cr_idx = extract32(reg, R_SCREENING_TYPE2_REG0_COMPARE_A_SHIFT + j * 6,
                                   R_SCREENING_TYPE2_REG0_COMPARE_A_LENGTH);

            if (!extract32(reg, R_SCREENING_TYPE2_REG0_COMPARE_A_ENABLE_SHIFT + j * 6,
                           R_SCREENING_TYPE2_REG0_COMPARE_A_ENABLE_LENGTH)) {
                continue;
            }

            if (cr_idx > s->num_type2_screeners) {
                qemu_log_mask(LOG_GUEST_ERROR, "Out of range compare "
                              "register index: %d\n", cr_idx);
            }

            cr0 = s->regs[R_TYPE2_COMPARE_0_WORD_0 + cr_idx * 2];
            cr1 = s->regs[R_TYPE2_COMPARE_0_WORD_1 + cr_idx * 2];
            offset = FIELD_EX32(cr1, TYPE2_COMPARE_0_WORD_1, OFFSET_VALUE);

            switch (FIELD_EX32(cr1, TYPE2_COMPARE_0_WORD_1, COMPARE_OFFSET)) {
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
            mask = FIELD_EX32(cr0, TYPE2_COMPARE_0_WORD_0, MASK_VALUE);
            compare = FIELD_EX32(cr0, TYPE2_COMPARE_0_WORD_0, COMPARE_VALUE);

            if ((rx_cmp & mask) == (compare & mask)) {
                matched = true;
            } else {
                mismatched = true;
            }
        }

        if (matched && !mismatched) {
            return FIELD_EX32(reg, SCREENING_TYPE2_REG0, QUEUE_NUM);
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
        base_addr = s->regs[tx ? R_TXQBASE : R_RXQBASE];
        break;
    case 1 ... (MAX_PRIORITY_QUEUES - 1):
        base_addr = s->regs[(tx ? R_TRANSMIT_Q1_PTR :
                                 R_RECEIVE_Q1_PTR) + q - 1];
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

    if (FIELD_EX32(s->regs[R_DMACFG], DMACFG, DMA_ADDR_BUS_WIDTH)) {
        desc_addr = s->regs[tx ? R_TBQPH : R_RBQPH];
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
        s->regs[R_RXSTATUS] |= R_RXSTATUS_BUF_NOT_AVAILABLE_MASK;
        gem_set_isr(s, q, R_ISR_RX_USED_MASK);
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
        return size;  /* no, drop silently b/c it's not an error */
    }

    /* Discard packets with receive length error enabled ? */
    if (FIELD_EX32(s->regs[R_NWCFG], NWCFG, LEN_ERR_DISCARD)) {
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
    rxbuf_offset = FIELD_EX32(s->regs[R_NWCFG], NWCFG, RECV_BUF_OFFSET);

    /* The configure size of each receive buffer.  Determines how many
     * buffers needed to hold this packet.
     */
    rxbufsize = FIELD_EX32(s->regs[R_DMACFG], DMACFG, RX_BUF_SIZE);
    rxbufsize *= GEM_DMACFG_RBUFSZ_MUL;

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
    if (FIELD_EX32(s->regs[R_NWCFG], NWCFG, FCS_REMOVE)) {
        rxbuf_ptr = (void *)buf;
    } else {
        uint32_t crc_val;

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
        gem_set_isr(s, q, R_ISR_AMBA_ERROR_MASK);
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

    s->regs[R_RXSTATUS] |= R_RXSTATUS_FRAME_RECEIVED_MASK;
    gem_set_isr(s, q, R_ISR_RECV_COMPLETE_MASK);

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
    octets = ((uint64_t)(s->regs[R_OCTTXLO]) << 32) |
             s->regs[R_OCTTXHI];
    octets += bytes;
    s->regs[R_OCTTXLO] = octets >> 32;
    s->regs[R_OCTTXHI] = octets;

    /* Error-free Frames transmitted */
    s->regs[R_TXCNT]++;

    /* Error-free Broadcast Frames counter */
    if (!memcmp(packet, broadcast_addr, 6)) {
        s->regs[R_TXBCNT]++;
    }

    /* Error-free Multicast Frames counter */
    if (packet[0] == 0x01) {
        s->regs[R_TXMCNT]++;
    }

    if (bytes <= 64) {
        s->regs[R_TX64CNT]++;
    } else if (bytes <= 127) {
        s->regs[R_TX65CNT]++;
    } else if (bytes <= 255) {
        s->regs[R_TX128CNT]++;
    } else if (bytes <= 511) {
        s->regs[R_TX256CNT]++;
    } else if (bytes <= 1023) {
        s->regs[R_TX512CNT]++;
    } else if (bytes <= 1518) {
        s->regs[R_TX1024CNT]++;
    } else {
        s->regs[R_TX1519CNT]++;
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
    if (!FIELD_EX32(s->regs[R_NWCTRL], NWCTRL, ENABLE_TRANSMIT)) {
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
            if (!FIELD_EX32(s->regs[R_NWCTRL], NWCTRL, ENABLE_TRANSMIT)) {
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
                gem_set_isr(s, q, R_ISR_AMBA_ERROR_MASK);
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

                s->regs[R_TXSTATUS] |= R_TXSTATUS_TRANSMIT_COMPLETE_MASK;
                gem_set_isr(s, q, R_ISR_XMIT_COMPLETE_MASK);

                /* Handle interrupt consequences */
                gem_update_int_status(s);

                /* Is checksum offload enabled? */
                if (FIELD_EX32(s->regs[R_DMACFG], DMACFG, TX_PBUF_CSUM_OFFLOAD)) {
                    net_checksum_calculate(s->tx_packet, total_bytes, CSUM_ALL);
                }

                /* Update MAC statistics */
                gem_transmit_updatestats(s, s->tx_packet, total_bytes);

                /* Send the packet somewhere */
                if (s->phy_loop || FIELD_EX32(s->regs[R_NWCTRL], NWCTRL,
                                              LOOPBACK_LOCAL)) {
                    qemu_receive_packet(qemu_get_queue(s->nic), s->tx_packet,
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
                if (FIELD_EX32(s->regs[R_DMACFG], DMACFG, DMA_ADDR_BUS_WIDTH)) {
                    packet_desc_addr = s->regs[R_TBQPH];
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
            s->regs[R_TXSTATUS] |= R_TXSTATUS_USED_BIT_READ_MASK;
            /* IRQ TXUSED is defined only for queue 0 */
            if (q == 0) {
                gem_set_isr(s, 0, R_ISR_TX_USED_MASK);
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
    s->regs[R_NWCFG] = 0x00080000;
    s->regs[R_NWSTATUS] = 0x00000006;
    s->regs[R_DMACFG] = 0x00020784;
    s->regs[R_IMR] = 0x07ffffff;
    s->regs[R_TXPAUSE] = 0x0000ffff;
    s->regs[R_TXPARTIALSF] = 0x000003ff;
    s->regs[R_RXPARTIALSF] = 0x000003ff;
    s->regs[R_MODID] = s->revision;
    s->regs[R_DESCONF] = 0x02D00111;
    s->regs[R_DESCONF2] = 0x2ab10000 | s->jumbo_max_len;
    s->regs[R_DESCONF5] = 0x002f2045;
    s->regs[R_DESCONF6] = R_DESCONF6_DMA_ADDR_64B_MASK;
    s->regs[R_INT_Q1_MASK] = 0x00000CE6;
    s->regs[R_JUMBO_MAX_LEN] = s->jumbo_max_len;

    if (s->num_priority_queues > 1) {
        queues_mask = MAKE_64BIT_MASK(1, s->num_priority_queues - 1);
        s->regs[R_DESCONF6] |= queues_mask;
    }

    /* Set MAC address */
    a = &s->conf.macaddr.a[0];
    s->regs[R_SPADDR1LO] = a[0] | (a[1] << 8) | (a[2] << 16) | (a[3] << 24);
    s->regs[R_SPADDR1HI] = a[4] | (a[5] << 8);

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

static void gem_handle_phy_access(CadenceGEMState *s)
{
    uint32_t val = s->regs[R_PHYMNTNC];
    uint32_t phy_addr, reg_num;

    phy_addr = FIELD_EX32(val, PHYMNTNC, PHY_ADDR);

    if (phy_addr != s->phy_addr) {
        /* no phy at this address */
        if (FIELD_EX32(val, PHYMNTNC, OP) == MDIO_OP_READ) {
            s->regs[R_PHYMNTNC] = FIELD_DP32(val, PHYMNTNC, DATA, 0xffff);
        }
        return;
    }

    reg_num = FIELD_EX32(val, PHYMNTNC, REG_ADDR);

    switch (FIELD_EX32(val, PHYMNTNC, OP)) {
    case MDIO_OP_READ:
        s->regs[R_PHYMNTNC] = FIELD_DP32(val, PHYMNTNC, DATA,
                                         gem_phy_read(s, reg_num));
        break;

    case MDIO_OP_WRITE:
        gem_phy_write(s, reg_num, val);
        break;

    default:
        break; /* only clause 22 operations are supported */
    }
}

/*
 * gem_read32:
 * Read a GEM register.
 */
static uint64_t gem_read(void *opaque, hwaddr offset, unsigned size)
{
    CadenceGEMState *s;
    uint32_t retval;
    s = opaque;

    offset >>= 2;
    retval = s->regs[offset];

    DB_PRINT("offset: 0x%04x read: 0x%08x\n", (unsigned)offset*4, retval);

    switch (offset) {
    case R_ISR:
        DB_PRINT("lowering irqs on ISR read\n");
        /* The interrupts get updated at the end of the function. */
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
    case R_NWCTRL:
        if (FIELD_EX32(val, NWCTRL, ENABLE_RECEIVE)) {
            for (i = 0; i < s->num_priority_queues; ++i) {
                gem_get_rx_desc(s, i);
            }
        }
        if (FIELD_EX32(val, NWCTRL, TRANSMIT_START)) {
            gem_transmit(s);
        }
        if (!(FIELD_EX32(val, NWCTRL, ENABLE_TRANSMIT))) {
            /* Reset to start of Q when transmit disabled. */
            for (i = 0; i < s->num_priority_queues; i++) {
                s->tx_desc_addr[i] = gem_get_tx_queue_base_addr(s, i);
            }
        }
        if (gem_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case R_TXSTATUS:
        gem_update_int_status(s);
        break;
    case R_RXQBASE:
        s->rx_desc_addr[0] = val;
        break;
    case R_RECEIVE_Q1_PTR ... R_RECEIVE_Q7_PTR:
        s->rx_desc_addr[offset - R_RECEIVE_Q1_PTR + 1] = val;
        break;
    case R_TXQBASE:
        s->tx_desc_addr[0] = val;
        break;
    case R_TRANSMIT_Q1_PTR ... R_TRANSMIT_Q7_PTR:
        s->tx_desc_addr[offset - R_TRANSMIT_Q1_PTR + 1] = val;
        break;
    case R_RXSTATUS:
        gem_update_int_status(s);
        break;
    case R_IER:
        s->regs[R_IMR] &= ~val;
        gem_update_int_status(s);
        break;
    case R_JUMBO_MAX_LEN:
        s->regs[R_JUMBO_MAX_LEN] = val & MAX_JUMBO_FRAME_SIZE_MASK;
        break;
    case R_INT_Q1_ENABLE ... R_INT_Q7_ENABLE:
        s->regs[R_INT_Q1_MASK + offset - R_INT_Q1_ENABLE] &= ~val;
        gem_update_int_status(s);
        break;
    case R_IDR:
        s->regs[R_IMR] |= val;
        gem_update_int_status(s);
        break;
    case R_INT_Q1_DISABLE ... R_INT_Q7_DISABLE:
        s->regs[R_INT_Q1_MASK + offset - R_INT_Q1_DISABLE] |= val;
        gem_update_int_status(s);
        break;
    case R_SPADDR1LO:
    case R_SPADDR2LO:
    case R_SPADDR3LO:
    case R_SPADDR4LO:
        s->sar_active[(offset - R_SPADDR1LO) / 2] = false;
        break;
    case R_SPADDR1HI:
    case R_SPADDR2HI:
    case R_SPADDR3HI:
    case R_SPADDR4HI:
        s->sar_active[(offset - R_SPADDR1HI) / 2] = true;
        break;
    case R_PHYMNTNC:
        gem_handle_phy_access(s);
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
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);

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
}

static const VMStateDescription vmstate_cadence_gem = {
    .name = "cadence_gem",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
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
    DEFINE_PROP_LINK("dma", CadenceGEMState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
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
