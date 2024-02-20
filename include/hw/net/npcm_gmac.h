/*
 * Nuvoton NPCM7xx/8xx GMAC Module
 *
 * Copyright 2024 Google LLC
 * Authors:
 * Hao Wu <wuhaotsh@google.com>
 * Nabih Estefan <nabihestefan@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef NPCM_GMAC_H
#define NPCM_GMAC_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "net/net.h"

#define NPCM_GMAC_NR_REGS (0x1060 / sizeof(uint32_t))

#define NPCM_GMAC_MAX_PHYS 32
#define NPCM_GMAC_MAX_PHY_REGS 32

struct NPCMGMACRxDesc {
    uint32_t rdes0;
    uint32_t rdes1;
    uint32_t rdes2;
    uint32_t rdes3;
};

/* NPCMGMACRxDesc.flags values */
/* RDES2 and RDES3 are buffer addresses */
/* Owner: 0 = software, 1 = dma */
#define RX_DESC_RDES0_OWN BIT(31)
/* Destination Address Filter Fail */
#define RX_DESC_RDES0_DEST_ADDR_FILT_FAIL BIT(30)
/* Frame length */
#define RX_DESC_RDES0_FRAME_LEN_MASK(word) extract32(word, 16, 14)
/* Frame length Shift*/
#define RX_DESC_RDES0_FRAME_LEN_SHIFT 16
/* Error Summary */
#define RX_DESC_RDES0_ERR_SUMM_MASK BIT(15)
/* Descriptor Error */
#define RX_DESC_RDES0_DESC_ERR_MASK BIT(14)
/* Source Address Filter Fail */
#define RX_DESC_RDES0_SRC_ADDR_FILT_FAIL_MASK BIT(13)
/* Length Error */
#define RX_DESC_RDES0_LEN_ERR_MASK BIT(12)
/* Overflow Error */
#define RX_DESC_RDES0_OVRFLW_ERR_MASK BIT(11)
/* VLAN Tag */
#define RX_DESC_RDES0_VLAN_TAG_MASK BIT(10)
/* First Descriptor */
#define RX_DESC_RDES0_FIRST_DESC_MASK BIT(9)
/* Last Descriptor */
#define RX_DESC_RDES0_LAST_DESC_MASK BIT(8)
/* IPC Checksum Error/Giant Frame */
#define RX_DESC_RDES0_IPC_CHKSM_ERR_GNT_FRM_MASK BIT(7)
/* Late Collision */
#define RX_DESC_RDES0_LT_COLL_MASK BIT(6)
/* Frame Type */
#define RX_DESC_RDES0_FRM_TYPE_MASK BIT(5)
/* Receive Watchdog Timeout */
#define RX_DESC_RDES0_REC_WTCHDG_TMT_MASK BIT(4)
/* Receive Error */
#define RX_DESC_RDES0_RCV_ERR_MASK BIT(3)
/* Dribble Bit Error */
#define RX_DESC_RDES0_DRBL_BIT_ERR_MASK BIT(2)
/* Cyclcic Redundancy Check Error */
#define RX_DESC_RDES0_CRC_ERR_MASK BIT(1)
/* Rx MAC Address/Payload Checksum Error */
#define RC_DESC_RDES0_RCE_MASK BIT(0)

/* Disable Interrupt on Completion */
#define RX_DESC_RDES1_DIS_INTR_COMP_MASK BIT(31)
/* Receive end of ring */
#define RX_DESC_RDES1_RC_END_RING_MASK BIT(25)
/* Second Address Chained */
#define RX_DESC_RDES1_SEC_ADDR_CHND_MASK BIT(24)
/* Receive Buffer 2 Size */
#define RX_DESC_RDES1_BFFR2_SZ_SHIFT 11
#define RX_DESC_RDES1_BFFR2_SZ_MASK(word) extract32(word, \
    RX_DESC_RDES1_BFFR2_SZ_SHIFT, 11)
/* Receive Buffer 1 Size */
#define RX_DESC_RDES1_BFFR1_SZ_MASK(word) extract32(word, 0, 11)


struct NPCMGMACTxDesc {
    uint32_t tdes0;
    uint32_t tdes1;
    uint32_t tdes2;
    uint32_t tdes3;
};

/* NPCMGMACTxDesc.flags values */
/* TDES2 and TDES3 are buffer addresses */
/* Owner: 0 = software, 1 = gmac */
#define TX_DESC_TDES0_OWN BIT(31)
/* Tx Time Stamp Status */
#define TX_DESC_TDES0_TTSS_MASK BIT(17)
/* IP Header Error */
#define TX_DESC_TDES0_IP_HEAD_ERR_MASK BIT(16)
/* Error Summary */
#define TX_DESC_TDES0_ERR_SUMM_MASK BIT(15)
/* Jabber Timeout */
#define TX_DESC_TDES0_JBBR_TMT_MASK BIT(14)
/* Frame Flushed */
#define TX_DESC_TDES0_FRM_FLSHD_MASK BIT(13)
/* Payload Checksum Error */
#define TX_DESC_TDES0_PYLD_CHKSM_ERR_MASK BIT(12)
/* Loss of Carrier */
#define TX_DESC_TDES0_LSS_CARR_MASK BIT(11)
/* No Carrier */
#define TX_DESC_TDES0_NO_CARR_MASK BIT(10)
/* Late Collision */
#define TX_DESC_TDES0_LATE_COLL_MASK BIT(9)
/* Excessive Collision */
#define TX_DESC_TDES0_EXCS_COLL_MASK BIT(8)
/* VLAN Frame */
#define TX_DESC_TDES0_VLAN_FRM_MASK BIT(7)
/* Collision Count */
#define TX_DESC_TDES0_COLL_CNT_MASK(word) extract32(word, 3, 4)
/* Excessive Deferral */
#define TX_DESC_TDES0_EXCS_DEF_MASK BIT(2)
/* Underflow Error */
#define TX_DESC_TDES0_UNDRFLW_ERR_MASK BIT(1)
/* Deferred Bit */
#define TX_DESC_TDES0_DFRD_BIT_MASK BIT(0)

/* Interrupt of Completion */
#define TX_DESC_TDES1_INTERR_COMP_MASK BIT(31)
/* Last Segment */
#define TX_DESC_TDES1_LAST_SEG_MASK BIT(30)
/* First Segment */
#define TX_DESC_TDES1_FIRST_SEG_MASK BIT(29)
/* Checksum Insertion Control */
#define TX_DESC_TDES1_CHKSM_INS_CTRL_MASK(word) extract32(word, 27, 2)
/* Disable Cyclic Redundancy Check */
#define TX_DESC_TDES1_DIS_CDC_MASK BIT(26)
/* Transmit End of Ring */
#define TX_DESC_TDES1_TX_END_RING_MASK BIT(25)
/* Secondary Address Chained */
#define TX_DESC_TDES1_SEC_ADDR_CHND_MASK BIT(24)
/* Transmit Buffer 2 Size */
#define TX_DESC_TDES1_BFFR2_SZ_MASK(word) extract32(word, 11, 11)
/* Transmit Buffer 1 Size */
#define TX_DESC_TDES1_BFFR1_SZ_MASK(word) extract32(word, 0, 11)

typedef struct NPCMGMACState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    NICState *nic;
    NICConf conf;

    uint32_t regs[NPCM_GMAC_NR_REGS];
    uint16_t phy_regs[NPCM_GMAC_MAX_PHYS][NPCM_GMAC_MAX_PHY_REGS];
} NPCMGMACState;

#define TYPE_NPCM_GMAC "npcm-gmac"
OBJECT_DECLARE_SIMPLE_TYPE(NPCMGMACState, NPCM_GMAC)

/* Mask for RO bits in Status */
#define NPCM_DMA_STATUS_RO_MASK(word) (word & 0xfffe0000)
/* Mask for RO bits in Status */
#define NPCM_DMA_STATUS_W1C_MASK(word) (word & 0x1e7ff)

/* Transmit Process State */
#define NPCM_DMA_STATUS_TX_PROCESS_STATE_SHIFT 20
/* Transmit States */
#define NPCM_DMA_STATUS_TX_STOPPED_STATE \
    (0b000)
#define NPCM_DMA_STATUS_TX_RUNNING_FETCHING_STATE \
    (0b001)
#define NPCM_DMA_STATUS_TX_RUNNING_WAITING_STATE \
    (0b010)
#define NPCM_DMA_STATUS_TX_RUNNING_READ_STATE \
    (0b011)
#define NPCM_DMA_STATUS_TX_SUSPENDED_STATE \
    (0b110)
#define NPCM_DMA_STATUS_TX_RUNNING_CLOSING_STATE \
    (0b111)
/* Transmit Process State */
#define NPCM_DMA_STATUS_RX_PROCESS_STATE_SHIFT 17
/* Receive States */
#define NPCM_DMA_STATUS_RX_STOPPED_STATE \
    (0b000)
#define NPCM_DMA_STATUS_RX_RUNNING_FETCHING_STATE \
    (0b001)
#define NPCM_DMA_STATUS_RX_RUNNING_WAITING_STATE \
    (0b011)
#define NPCM_DMA_STATUS_RX_SUSPENDED_STATE \
    (0b100)
#define NPCM_DMA_STATUS_RX_RUNNING_CLOSING_STATE \
    (0b101)
#define NPCM_DMA_STATUS_RX_RUNNING_TRANSFERRING_STATE \
    (0b111)


/* Early Receive Interrupt */
#define NPCM_DMA_STATUS_ERI BIT(14)
/* Fatal Bus Error Interrupt */
#define NPCM_DMA_STATUS_FBI BIT(13)
/* Early transmit Interrupt */
#define NPCM_DMA_STATUS_ETI BIT(10)
/* Receive Watchdog Timeout */
#define NPCM_DMA_STATUS_RWT BIT(9)
/* Receive Process Stopped */
#define NPCM_DMA_STATUS_RPS BIT(8)
/* Receive Buffer Unavailable */
#define NPCM_DMA_STATUS_RU BIT(7)
/* Receive Interrupt */
#define NPCM_DMA_STATUS_RI BIT(6)
/* Transmit Underflow */
#define NPCM_DMA_STATUS_UNF BIT(5)
/* Receive Overflow */
#define NPCM_DMA_STATUS_OVF BIT(4)
/* Transmit Jabber Timeout */
#define NPCM_DMA_STATUS_TJT BIT(3)
/* Transmit Buffer Unavailable */
#define NPCM_DMA_STATUS_TU BIT(2)
/* Transmit Process Stopped */
#define NPCM_DMA_STATUS_TPS BIT(1)
/* Transmit Interrupt */
#define NPCM_DMA_STATUS_TI BIT(0)

/* Normal Interrupt Summary */
#define NPCM_DMA_STATUS_NIS BIT(16)
/* Interrupts enabled by NIE */
#define NPCM_DMA_STATUS_NIS_BITS (NPCM_DMA_STATUS_TI | \
                                  NPCM_DMA_STATUS_TU | \
                                  NPCM_DMA_STATUS_RI | \
                                  NPCM_DMA_STATUS_ERI)
/* Abnormal Interrupt Summary */
#define NPCM_DMA_STATUS_AIS BIT(15)
/* Interrupts enabled by AIE */
#define NPCM_DMA_STATUS_AIS_BITS (NPCM_DMA_STATUS_TPS | \
                                  NPCM_DMA_STATUS_TJT | \
                                  NPCM_DMA_STATUS_OVF | \
                                  NPCM_DMA_STATUS_UNF | \
                                  NPCM_DMA_STATUS_RU  | \
                                  NPCM_DMA_STATUS_RPS | \
                                  NPCM_DMA_STATUS_RWT | \
                                  NPCM_DMA_STATUS_ETI | \
                                  NPCM_DMA_STATUS_FBI)

/* Early Receive Interrupt Enable */
#define NPCM_DMA_INTR_ENAB_ERE BIT(14)
/* Fatal Bus Error Interrupt Enable */
#define NPCM_DMA_INTR_ENAB_FBE BIT(13)
/* Early transmit Interrupt Enable */
#define NPCM_DMA_INTR_ENAB_ETE BIT(10)
/* Receive Watchdog Timout Enable */
#define NPCM_DMA_INTR_ENAB_RWE BIT(9)
/* Receive Process Stopped Enable */
#define NPCM_DMA_INTR_ENAB_RSE BIT(8)
/* Receive Buffer Unavailable Enable */
#define NPCM_DMA_INTR_ENAB_RUE BIT(7)
/* Receive Interrupt Enable */
#define NPCM_DMA_INTR_ENAB_RIE BIT(6)
/* Transmit Underflow Enable */
#define NPCM_DMA_INTR_ENAB_UNE BIT(5)
/* Receive Overflow Enable */
#define NPCM_DMA_INTR_ENAB_OVE BIT(4)
/* Transmit Jabber Timeout Enable */
#define NPCM_DMA_INTR_ENAB_TJE BIT(3)
/* Transmit Buffer Unavailable Enable */
#define NPCM_DMA_INTR_ENAB_TUE BIT(2)
/* Transmit Process Stopped Enable */
#define NPCM_DMA_INTR_ENAB_TSE BIT(1)
/* Transmit Interrupt Enable */
#define NPCM_DMA_INTR_ENAB_TIE BIT(0)

/* Normal Interrupt Summary Enable */
#define NPCM_DMA_INTR_ENAB_NIE BIT(16)
/* Interrupts enabled by NIE Enable */
#define NPCM_DMA_INTR_ENAB_NIE_BITS (NPCM_DMA_INTR_ENAB_TIE | \
                                     NPCM_DMA_INTR_ENAB_TUE | \
                                     NPCM_DMA_INTR_ENAB_RIE | \
                                     NPCM_DMA_INTR_ENAB_ERE)
/* Abnormal Interrupt Summary Enable */
#define NPCM_DMA_INTR_ENAB_AIE BIT(15)
/* Interrupts enabled by AIE Enable */
#define NPCM_DMA_INTR_ENAB_AIE_BITS (NPCM_DMA_INTR_ENAB_TSE | \
                                     NPCM_DMA_INTR_ENAB_TJE | \
                                     NPCM_DMA_INTR_ENAB_OVE | \
                                     NPCM_DMA_INTR_ENAB_UNE | \
                                     NPCM_DMA_INTR_ENAB_RUE | \
                                     NPCM_DMA_INTR_ENAB_RSE | \
                                     NPCM_DMA_INTR_ENAB_RWE | \
                                     NPCM_DMA_INTR_ENAB_ETE | \
                                     NPCM_DMA_INTR_ENAB_FBE)

/* Flushing Disabled */
#define NPCM_DMA_CONTROL_FLUSH_MASK BIT(24)
/* Start/stop Transmit */
#define NPCM_DMA_CONTROL_START_STOP_TX BIT(13)
/* Start/stop Receive */
#define NPCM_DMA_CONTROL_START_STOP_RX BIT(1)
/* Next receive descriptor start address */
#define NPCM_DMA_HOST_RX_DESC_MASK(word) ((uint32_t) (word) & ~3u)
/* Next transmit descriptor start address */
#define NPCM_DMA_HOST_TX_DESC_MASK(word) ((uint32_t) (word) & ~3u)

/* Receive enable */
#define NPCM_GMAC_MAC_CONFIG_RX_EN BIT(2)
/* Transmit enable */
#define NPCM_GMAC_MAC_CONFIG_TX_EN BIT(3)

/* Frame Receive All */
#define NPCM_GMAC_FRAME_FILTER_REC_ALL_MASK BIT(31)
/* Frame HPF Filter*/
#define NPCM_GMAC_FRAME_FILTER_HPF_MASK BIT(10)
/* Frame SAF Filter*/
#define NPCM_GMAC_FRAME_FILTER_SAF_MASK BIT(9)
/* Frame SAIF Filter*/
#define NPCM_GMAC_FRAME_FILTER_SAIF_MASK BIT(8)
/* Frame PCF Filter*/
#define NPCM_GMAC_FRAME_FILTER_PCF_MASK BIT(word) extract32((word), 6, 2)
/* Frame DBF Filter*/
#define NPCM_GMAC_FRAME_FILTER_DBF_MASK BIT(5)
/* Frame PM Filter*/
#define NPCM_GMAC_FRAME_FILTER_PM_MASK BIT(4)
/* Frame DAIF Filter*/
#define NPCM_GMAC_FRAME_FILTER_DAIF_MASK BIT(3)
/* Frame HMC Filter*/
#define NPCM_GMAC_FRAME_FILTER_HMC_MASK BIT(2)
/* Frame HUC Filter*/
#define NPCM_GMAC_FRAME_FILTER_HUC_MASK BIT(1)
/* Frame PR Filter*/
#define NPCM_GMAC_FRAME_FILTER_PR_MASK BIT(0)

#endif /* NPCM_GMAC_H */
