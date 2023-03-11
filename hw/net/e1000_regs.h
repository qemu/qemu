/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* e1000_hw.h
 * Structures, enums, and macros for the MAC
 */

#ifndef HW_E1000_REGS_H
#define HW_E1000_REGS_H

#include "e1000x_regs.h"

#define E1000_ITR      0x000C4  /* Interrupt Throttling Rate - RW */
#define E1000_EIAC     0x000DC  /* Ext. Interrupt Auto Clear - RW */
#define E1000_IVAR     0x000E4  /* Interrupt Vector Allocation Register - RW */
#define E1000_EITR     0x000E8  /* Extended Interrupt Throttling Rate - RW */
#define E1000_RDBAL1   0x02900  /* RX Descriptor Base Address Low (1) - RW */
#define E1000_RDBAH1   0x02904  /* RX Descriptor Base Address High (1) - RW */
#define E1000_RDLEN1   0x02908  /* RX Descriptor Length (1) - RW */
#define E1000_RDH1     0x02910  /* RX Descriptor Head (1) - RW */
#define E1000_RDT1     0x02918  /* RX Descriptor Tail (1) - RW */
#define E1000_FCRTV    0x05F40  /* Flow Control Refresh Timer Value - RW */
#define E1000_TXCW     0x00178  /* TX Configuration Word - RW */
#define E1000_RXCW     0x00180  /* RX Configuration Word - RO */
#define E1000_TBT      0x00448  /* TX Burst Timer - RW */
#define E1000_AIT      0x00458  /* Adaptive Interframe Spacing Throttle - RW */
#define E1000_EXTCNF_CTRL  0x00F00  /* Extended Configuration Control */
#define E1000_EXTCNF_SIZE  0x00F08  /* Extended Configuration Size */
#define E1000_PHY_CTRL     0x00F10  /* PHY Control Register in CSR */
#define E1000_PBA      0x01000  /* Packet Buffer Allocation - RW */
#define E1000_PBM      0x10000  /* Packet Buffer Memory - RW */
#define E1000_PBS      0x01008  /* Packet Buffer Size - RW */
#define E1000_FLASHT   0x01028  /* FLASH Timer Register */
#define E1000_EEWR     0x0102C  /* EEPROM Write Register - RW */
#define E1000_FLSWCTL  0x01030  /* FLASH control register */
#define E1000_FLSWDATA 0x01034  /* FLASH data register */
#define E1000_FLSWCNT  0x01038  /* FLASH Access Counter */
#define E1000_FLOL     0x01050  /* FEEP Auto Load */
#define E1000_ERT      0x02008  /* Early Rx Threshold - RW */
#define E1000_FCRTH_A  0x00160  /* Alias to FCRTH */
#define E1000_PSRCTL   0x02170  /* Packet Split Receive Control - RW */
#define E1000_RDBAL    0x02800  /* RX Descriptor Base Address Low - RW */
#define E1000_RDBAH    0x02804  /* RX Descriptor Base Address High - RW */
#define E1000_RDLEN    0x02808  /* RX Descriptor Length - RW */
#define E1000_RDH      0x02810  /* RX Descriptor Head - RW */
#define E1000_RDT      0x02818  /* RX Descriptor Tail - RW */
#define E1000_RDTR     0x02820  /* RX Delay Timer - RW */
#define E1000_RDTR_A   0x00108  /* Alias to RDTR */
#define E1000_RDBAL0   E1000_RDBAL /* RX Desc Base Address Low (0) - RW */
#define E1000_RDBAL0_A 0x00110     /* Alias to RDBAL0 */
#define E1000_RDBAH0   E1000_RDBAH /* RX Desc Base Address High (0) - RW */
#define E1000_RDBAH0_A 0x00114     /* Alias to RDBAH0 */
#define E1000_RDLEN0   E1000_RDLEN /* RX Desc Length (0) - RW */
#define E1000_RDLEN0_A 0x00118     /* Alias to RDLEN0 */
#define E1000_RDH0     E1000_RDH   /* RX Desc Head (0) - RW */
#define E1000_RDH0_A   0x00120     /* Alias to RDH0 */
#define E1000_RDT0     E1000_RDT   /* RX Desc Tail (0) - RW */
#define E1000_RDT0_A   0x00128     /* Alias to RDT0 */
#define E1000_RDTR0    E1000_RDTR  /* RX Delay Timer (0) - RW */
#define E1000_RXDCTL   0x02828  /* RX Descriptor Control queue 0 - RW */
#define E1000_RXDCTL1  0x02928  /* RX Descriptor Control queue 1 - RW */
#define E1000_RADV     0x0282C  /* RX Interrupt Absolute Delay Timer - RW */
#define E1000_RSRPD    0x02C00  /* RX Small Packet Detect - RW */
#define E1000_RAID     0x02C08  /* Receive Ack Interrupt Delay - RW */
#define E1000_POEMB    0x00F10  /* PHY OEM Bits Register - RW */
#define E1000_TDBAL    0x03800  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAL_A  0x00420  /* Alias to TDBAL */
#define E1000_TDBAH    0x03804  /* TX Descriptor Base Address High - RW */
#define E1000_TDBAH_A  0x00424  /* Alias to TDBAH */
#define E1000_TDLEN    0x03808  /* TX Descriptor Length - RW */
#define E1000_TDLEN_A  0x00428  /* Alias to TDLEN */
#define E1000_TDH      0x03810  /* TX Descriptor Head - RW */
#define E1000_TDH_A    0x00430  /* Alias to TDH */
#define E1000_TDT      0x03818  /* TX Descripotr Tail - RW */
#define E1000_TDT_A    0x00438  /* Alias to TDT */
#define E1000_TIDV     0x03820  /* TX Interrupt Delay Value - RW */
#define E1000_TIDV_A   0x00440  /* Alias to TIDV */
#define E1000_TXDCTL   0x03828  /* TX Descriptor Control - RW */
#define E1000_TADV     0x0382C  /* TX Interrupt Absolute Delay Val - RW */
#define E1000_TSPMT    0x03830  /* TCP Segmentation PAD & Min Threshold - RW */
#define E1000_TARC0    0x03840  /* TX Arbitration Count (0) */
#define E1000_TDBAL1   0x03900  /* TX Desc Base Address Low (1) - RW */
#define E1000_TDBAH1   0x03904  /* TX Desc Base Address High (1) - RW */
#define E1000_TDLEN1   0x03908  /* TX Desc Length (1) - RW */
#define E1000_TDH1     0x03910  /* TX Desc Head (1) - RW */
#define E1000_TDT1     0x03918  /* TX Desc Tail (1) - RW */
#define E1000_TXDCTL1  0x03928  /* TX Descriptor Control (1) - RW */
#define E1000_TARC1    0x03940  /* TX Arbitration Count (1) */
#define E1000_SEQEC    0x04038  /* Sequence Error Count - R/clr */
#define E1000_CEXTERR  0x0403C  /* Carrier Extension Error Count - R/clr */
#define E1000_TSCTFC   0x040FC  /* TCP Segmentation Context TX Fail - R/clr */
#define E1000_ICRXATC  0x04108  /* Interrupt Cause Rx Absolute Timer Expire Count */
#define E1000_ICTXPTC  0x0410C  /* Interrupt Cause Tx Packet Timer Expire Count */
#define E1000_ICTXATC  0x04110  /* Interrupt Cause Tx Absolute Timer Expire Count */
#define E1000_ICTXQEC  0x04118  /* Interrupt Cause Tx Queue Empty Count */
#define E1000_ICTXQMTC 0x0411C  /* Interrupt Cause Tx Queue Minimum Threshold Count */
#define E1000_ICRXOC   0x04124  /* Interrupt Cause Receiver Overrun Count */
#define E1000_MFUTP01  0x05828  /* Management Flex UDP/TCP Ports 0/1 - RW */
#define E1000_MFUTP23  0x05830  /* Management Flex UDP/TCP Ports 2/3 - RW */
#define E1000_FFLT     0x05F00  /* Flexible Filter Length Table - RW Array */
#define E1000_HOST_IF  0x08800  /* Host Interface */
#define E1000_FFVT     0x09800  /* Flexible Filter Value Table - RW Array */

#define E1000_KUMCTRLSTA 0x00034 /* MAC-PHY interface - RW */
#define E1000_MDPHYA     0x0003C /* PHY address - RW */

#define E1000_GCR2      0x05B64 /* 3GIO Control Register 2 */
#define E1000_FFLT_DBG  0x05F04 /* Debug Register */
#define E1000_HICR      0x08F00 /* Host Inteface Control */

#define E1000_RXMTRL     0x0B634 /* Time sync Rx EtherType and Msg Type - RW */
#define E1000_RXUDP      0x0B638 /* Time Sync Rx UDP Port - RW */
#define E1000_RXCFGL     0x0B634 /* RX Ethertype and Message Type - RW*/

#define E1000_MRQC_ENABLED(mrqc) (((mrqc) & (BIT(0) | BIT(1))) == BIT(0))

#define E1000_CPUVEC    0x02C10 /* CPU Vector Register - RW */
#define E1000_RSSIM     0x05864 /* RSS Interrupt Mask */
#define E1000_RSSIR     0x05868 /* RSS Interrupt Request */

#define E1000_RSS_QUEUE(reta, hash) ((E1000_RETA_VAL(reta, hash) & BIT(7)) >> 7)

/* [TR]DBAL and [TR]DLEN masks */
#define E1000_XDBAL_MASK            (~(BIT(4) - 1))
#define E1000_XDLEN_MASK            ((BIT(20) - 1) & (~(BIT(7) - 1)))

/* IVAR register parsing helpers */
#define E1000_IVAR_INT_ALLOC_VALID  (0x8)

#define E1000_IVAR_RXQ0_SHIFT       (0)
#define E1000_IVAR_RXQ1_SHIFT       (4)
#define E1000_IVAR_TXQ0_SHIFT       (8)
#define E1000_IVAR_TXQ1_SHIFT       (12)
#define E1000_IVAR_OTHER_SHIFT      (16)

#define E1000_IVAR_ENTRY_MASK       (0xF)
#define E1000_IVAR_ENTRY_VALID_MASK E1000_IVAR_INT_ALLOC_VALID
#define E1000_IVAR_ENTRY_VEC_MASK   (0x7)

#define E1000_IVAR_RXQ0(x)          ((x) >> E1000_IVAR_RXQ0_SHIFT)
#define E1000_IVAR_RXQ1(x)          ((x) >> E1000_IVAR_RXQ1_SHIFT)
#define E1000_IVAR_TXQ0(x)          ((x) >> E1000_IVAR_TXQ0_SHIFT)
#define E1000_IVAR_TXQ1(x)          ((x) >> E1000_IVAR_TXQ1_SHIFT)
#define E1000_IVAR_OTHER(x)         ((x) >> E1000_IVAR_OTHER_SHIFT)

#define E1000_IVAR_ENTRY_VALID(x)   ((x) & E1000_IVAR_ENTRY_VALID_MASK)
#define E1000_IVAR_ENTRY_VEC(x)     ((x) & E1000_IVAR_ENTRY_VEC_MASK)

#define E1000_IVAR_TX_INT_EVERY_WB  BIT(31)

#define E1000_RFCTL_ACK_DIS             0x00001000
#define E1000_RFCTL_ACK_DATA_DIS        0x00002000

/* PSRCTL parsing */
#define E1000_PSRCTL_BSIZE0_MASK   0x0000007F
#define E1000_PSRCTL_BSIZE1_MASK   0x00003F00
#define E1000_PSRCTL_BSIZE2_MASK   0x003F0000
#define E1000_PSRCTL_BSIZE3_MASK   0x3F000000

#define E1000_PSRCTL_BSIZE0_SHIFT  0
#define E1000_PSRCTL_BSIZE1_SHIFT  8
#define E1000_PSRCTL_BSIZE2_SHIFT  16
#define E1000_PSRCTL_BSIZE3_SHIFT  24

#define E1000_PSRCTL_BUFFS_PER_DESC 4

/* PHY 1000 MII Register/Bit Definitions */
/* 82574-specific registers */
#define PHY_COPPER_CTRL1      0x10 /* Copper Specific Control Register 1 */
#define PHY_COPPER_STAT1      0x11 /* Copper Specific Status Register 1 */
#define PHY_COPPER_INT_ENABLE 0x12  /* Interrupt Enable Register */
#define PHY_COPPER_STAT2      0x13 /* Copper Specific Status Register 2 */
#define PHY_COPPER_CTRL3      0x14 /* Copper Specific Control Register 3 */
#define PHY_COPPER_CTRL2      0x1A /* Copper Specific Control Register 2 */
#define PHY_RX_ERR_CNTR       0x15  /* Receive Error Counter */
#define PHY_PAGE              0x16 /* Page Address (Any page) */
#define PHY_OEM_BITS          0x19 /* OEM Bits (Page 0) */
#define PHY_BIAS_1            0x1d /* Bias Setting Register */
#define PHY_BIAS_2            0x1e /* Bias Setting Register */

/* 82574-specific registers - page 2 */
#define PHY_MAC_CTRL1         0x10 /* MAC Specific Control Register 1 */
#define PHY_MAC_INT_ENABLE    0x12 /* MAC Interrupt Enable Register */
#define PHY_MAC_STAT          0x13 /* MAC Specific Status Register */
#define PHY_MAC_CTRL2         0x15 /* MAC Specific Control Register 2 */

/* 82574-specific registers - page 3 */
#define PHY_LED_03_FUNC_CTRL1 0x10 /* LED[3:0] Function Control */
#define PHY_LED_03_POL_CTRL   0x11 /* LED[3:0] Polarity Control */
#define PHY_LED_TIMER_CTRL    0x12 /* LED Timer Control */
#define PHY_LED_45_CTRL       0x13 /* LED[5:4] Function Control and Polarity */

/* 82574-specific registers - page 5 */
#define PHY_1000T_SKEW        0x14 /* 1000 BASE - T Pair Skew Register */
#define PHY_1000T_SWAP        0x15 /* 1000 BASE - T Pair Swap and Polarity */

/* 82574-specific registers - page 6 */
#define PHY_CRC_COUNTERS      0x11 /* CRC Counters */

#define PHY_PAGE_RW_MASK 0x7F /* R/W part of page address register */

#define MAX_PHY_REG_ADDRESS        0x1F  /* 5 bit address bus (0-0x1F) */
#define MAX_PHY_MULTI_PAGE_REG     0xF   /* Registers equal on all pages */

/* M88E1000 Specific Registers */
#define M88E1000_PHY_SPEC_CTRL     0x10  /* PHY Specific Control Register */
#define M88E1000_PHY_SPEC_STATUS   0x11  /* PHY Specific Status Register */
#define M88E1000_INT_ENABLE        0x12  /* Interrupt Enable Register */
#define M88E1000_INT_STATUS        0x13  /* Interrupt Status Register */
#define M88E1000_EXT_PHY_SPEC_CTRL 0x14  /* Extended PHY Specific Control */
#define M88E1000_RX_ERR_CNTR       0x15  /* Receive Error Counter */

#define M88E1000_PHY_EXT_CTRL      0x1A  /* PHY extend control register */
#define M88E1000_PHY_PAGE_SELECT   0x1D  /* Reg 29 for page number setting */
#define M88E1000_PHY_GEN_CONTROL   0x1E  /* Its meaning depends on reg 29 */
#define M88E1000_PHY_VCO_REG_BIT8  0x100 /* Bits 8 & 11 are adjusted for */
#define M88E1000_PHY_VCO_REG_BIT11 0x800    /* improved BER performance */

#define E1000_STATUS_FUNC_MASK  0x0000000C      /* PCI Function Mask */
#define E1000_STATUS_FUNC_SHIFT 2
#define E1000_STATUS_FUNC_0     0x00000000      /* Function 0 */
#define E1000_STATUS_FUNC_1     0x00000004      /* Function 1 */
#define E1000_STATUS_TXOFF      0x00000010      /* transmission paused */
#define E1000_STATUS_TBIMODE    0x00000020      /* TBI mode */
#define E1000_STATUS_SPEED_MASK 0x000000C0
#define E1000_STATUS_LAN_INIT_DONE 0x00000200   /* Lan Init Completion
                                                   by EEPROM/Flash */
#define E1000_STATUS_ASDV       0x00000300      /* Auto speed detect value */
#define E1000_STATUS_ASDV_10    0x00000000      /* ASDV 10Mb */
#define E1000_STATUS_ASDV_100   0x00000100      /* ASDV 100Mb */
#define E1000_STATUS_ASDV_1000  0x00000200      /* ASDV 1Gb */
#define E1000_STATUS_DOCK_CI    0x00000800      /* Change in Dock/Undock state. Clear on write '0'. */
#define E1000_STATUS_MTXCKOK    0x00000400      /* MTX clock running OK */
#define E1000_STATUS_PCI66      0x00000800      /* In 66Mhz slot */
#define E1000_STATUS_BUS64      0x00001000      /* In 64 bit slot */
#define E1000_STATUS_PCIX_MODE  0x00002000      /* PCI-X mode */
#define E1000_STATUS_PCIX_SPEED 0x0000C000      /* PCI-X bus speed */
#define E1000_STATUS_BMC_SKU_0  0x00100000 /* BMC USB redirect disabled */
#define E1000_STATUS_BMC_SKU_1  0x00200000 /* BMC SRAM disabled */
#define E1000_STATUS_BMC_SKU_2  0x00400000 /* BMC SDRAM disabled */
#define E1000_STATUS_BMC_CRYPTO 0x00800000 /* BMC crypto disabled */
#define E1000_STATUS_BMC_LITE   0x01000000 /* BMC external code execution disabled */
#define E1000_STATUS_RGMII_ENABLE 0x02000000 /* RGMII disabled */
#define E1000_STATUS_FUSE_8       0x04000000
#define E1000_STATUS_FUSE_9       0x08000000
#define E1000_STATUS_SERDES0_DIS  0x10000000 /* SERDES disabled on port 0 */
#define E1000_STATUS_SERDES1_DIS  0x20000000 /* SERDES disabled on port 1 */
#define E1000_STATUS_SPEED_SHIFT  6
#define E1000_STATUS_ASDV_SHIFT   8

/* Transmit Descriptor */
struct e1000_tx_desc {
    uint64_t buffer_addr;       /* Address of the descriptor's data buffer */
    union {
        uint32_t data;
        struct {
            uint16_t length;    /* Data buffer length */
            uint8_t cso;        /* Checksum offset */
            uint8_t cmd;        /* Descriptor control */
        } flags;
    } lower;
    union {
        uint32_t data;
        struct {
            uint8_t status;     /* Descriptor status */
            uint8_t css;        /* Checksum start */
            uint16_t special;
        } fields;
    } upper;
};

#define E1000_TXD_POPTS_IXSM 0x01       /* Insert IP checksum */
#define E1000_TXD_POPTS_TXSM 0x02       /* Insert TCP/UDP checksum */

#endif /* HW_E1000_REGS_H */
