/*
 * QEMU PowerPC PowerNV (POWER8) PHB3 model
 *
 * Copyright (c) 2013-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB3_REGS_H
#define PCI_HOST_PNV_PHB3_REGS_H

#include "qemu/host-utils.h"

/*
 * PBCQ XSCOM registers
 */

#define PBCQ_NEST_IRSN_COMPARE  0x1a
#define PBCQ_NEST_IRSN_COMP           PPC_BITMASK(0, 18)
#define PBCQ_NEST_IRSN_MASK     0x1b
#define PBCQ_NEST_LSI_SRC_ID    0x1f
#define   PBCQ_NEST_LSI_SRC           PPC_BITMASK(0, 7)
#define PBCQ_NEST_REGS_COUNT    0x46
#define PBCQ_NEST_MMIO_BAR0     0x40
#define PBCQ_NEST_MMIO_BAR1     0x41
#define PBCQ_NEST_PHB_BAR       0x42
#define PBCQ_NEST_MMIO_MASK0    0x43
#define PBCQ_NEST_MMIO_MASK1    0x44
#define PBCQ_NEST_BAR_EN        0x45
#define   PBCQ_NEST_BAR_EN_MMIO0    PPC_BIT(0)
#define   PBCQ_NEST_BAR_EN_MMIO1    PPC_BIT(1)
#define   PBCQ_NEST_BAR_EN_PHB      PPC_BIT(2)
#define   PBCQ_NEST_BAR_EN_IRSN_RX  PPC_BIT(3)
#define   PBCQ_NEST_BAR_EN_IRSN_TX  PPC_BIT(4)

#define PBCQ_PCI_REGS_COUNT     0x15
#define PBCQ_PCI_BAR2           0x0b

#define PBCQ_SPCI_REGS_COUNT    0x5
#define PBCQ_SPCI_ASB_ADDR      0x0
#define PBCQ_SPCI_ASB_STATUS    0x1
#define PBCQ_SPCI_ASB_DATA      0x2
#define PBCQ_SPCI_AIB_CAPP_EN   0x3
#define PBCQ_SPCI_CAPP_SEC_TMR  0x4

/*
 * PHB MMIO registers
 */

/* PHB Fundamental register set A */
#define PHB_LSI_SOURCE_ID               0x100
#define   PHB_LSI_SRC_ID                PPC_BITMASK(5, 12)
#define PHB_DMA_CHAN_STATUS             0x110
#define   PHB_DMA_CHAN_ANY_ERR          PPC_BIT(27)
#define   PHB_DMA_CHAN_ANY_ERR1         PPC_BIT(28)
#define   PHB_DMA_CHAN_ANY_FREEZE       PPC_BIT(29)
#define PHB_CPU_LOADSTORE_STATUS        0x120
#define   PHB_CPU_LS_ANY_ERR            PPC_BIT(27)
#define   PHB_CPU_LS_ANY_ERR1           PPC_BIT(28)
#define   PHB_CPU_LS_ANY_FREEZE         PPC_BIT(29)
#define PHB_DMA_MSI_NODE_ID             0x128
#define   PHB_DMAMSI_NID_FIXED          PPC_BIT(0)
#define   PHB_DMAMSI_NID                PPC_BITMASK(24, 31)
#define PHB_CONFIG_DATA                 0x130
#define PHB_LOCK0                       0x138
#define PHB_CONFIG_ADDRESS              0x140
#define   PHB_CA_ENABLE                 PPC_BIT(0)
#define   PHB_CA_BUS                    PPC_BITMASK(4, 11)
#define   PHB_CA_DEV                    PPC_BITMASK(12, 16)
#define   PHB_CA_FUNC                   PPC_BITMASK(17, 19)
#define   PHB_CA_REG                    PPC_BITMASK(20, 31)
#define   PHB_CA_PE                     PPC_BITMASK(40, 47)
#define PHB_LOCK1                       0x148
#define PHB_IVT_BAR                     0x150
#define   PHB_IVT_BAR_ENABLE            PPC_BIT(0)
#define   PHB_IVT_BASE_ADDRESS_MASK     PPC_BITMASK(14, 48)
#define   PHB_IVT_LENGTH_MASK           PPC_BITMASK(52, 63)
#define PHB_RBA_BAR                     0x158
#define   PHB_RBA_BAR_ENABLE            PPC_BIT(0)
#define   PHB_RBA_BASE_ADDRESS          PPC_BITMASK(14, 55)
#define PHB_PHB3_CONFIG                 0x160
#define   PHB_PHB3C_64B_TCE_EN          PPC_BIT(2)
#define   PHB_PHB3C_32BIT_MSI_EN        PPC_BIT(8)
#define   PHB_PHB3C_64BIT_MSI_EN        PPC_BIT(14)
#define   PHB_PHB3C_M32_EN              PPC_BIT(16)
#define PHB_RTT_BAR                     0x168
#define   PHB_RTT_BAR_ENABLE            PPC_BIT(0)
#define   PHB_RTT_BASE_ADDRESS_MASK     PPC_BITMASK(14, 46)
#define PHB_PELTV_BAR                   0x188
#define   PHB_PELTV_BAR_ENABLE          PPC_BIT(0)
#define   PHB_PELTV_BASE_ADDRESS        PPC_BITMASK(14, 50)
#define PHB_M32_BASE_ADDR               0x190
#define PHB_M32_BASE_MASK               0x198
#define PHB_M32_START_ADDR              0x1a0
#define PHB_PEST_BAR                    0x1a8
#define   PHB_PEST_BAR_ENABLE           PPC_BIT(0)
#define   PHB_PEST_BASE_ADDRESS         PPC_BITMASK(14, 51)
#define PHB_M64_UPPER_BITS              0x1f0
#define PHB_INTREP_TIMER                0x1f8
#define PHB_DMARD_SYNC                  0x200
#define   PHB_DMARD_SYNC_START          PPC_BIT(0)
#define   PHB_DMARD_SYNC_COMPLETE       PPC_BIT(1)
#define PHB_RTC_INVALIDATE              0x208
#define   PHB_RTC_INVALIDATE_ALL        PPC_BIT(0)
#define   PHB_RTC_INVALIDATE_RID        PPC_BITMASK(16, 31)
#define PHB_TCE_KILL                    0x210
#define   PHB_TCE_KILL_ALL              PPC_BIT(0)
#define PHB_TCE_SPEC_CTL                0x218
#define PHB_IODA_ADDR                   0x220
#define   PHB_IODA_AD_AUTOINC           PPC_BIT(0)
#define   PHB_IODA_AD_TSEL              PPC_BITMASK(11, 15)
#define   PHB_IODA_AD_TADR              PPC_BITMASK(55, 63)
#define PHB_IODA_DATA0                  0x228
#define PHB_FFI_REQUEST                 0x238
#define   PHB_FFI_LOCK_CLEAR            PPC_BIT(3)
#define   PHB_FFI_REQUEST_ISN           PPC_BITMASK(49, 59)
#define PHB_FFI_LOCK                    0x240
#define   PHB_FFI_LOCK_STATE            PPC_BIT(0)
#define PHB_XIVE_UPDATE                 0x248 /* Broken in DD1 */
#define PHB_PHB3_GEN_CAP                0x250
#define PHB_PHB3_TCE_CAP                0x258
#define PHB_PHB3_IRQ_CAP                0x260
#define PHB_PHB3_EEH_CAP                0x268
#define PHB_IVC_INVALIDATE              0x2a0
#define   PHB_IVC_INVALIDATE_ALL        PPC_BIT(0)
#define   PHB_IVC_INVALIDATE_SID        PPC_BITMASK(16, 31)
#define PHB_IVC_UPDATE                  0x2a8
#define   PHB_IVC_UPDATE_ENABLE_P       PPC_BIT(0)
#define   PHB_IVC_UPDATE_ENABLE_Q       PPC_BIT(1)
#define   PHB_IVC_UPDATE_ENABLE_SERVER  PPC_BIT(2)
#define   PHB_IVC_UPDATE_ENABLE_PRI     PPC_BIT(3)
#define   PHB_IVC_UPDATE_ENABLE_GEN     PPC_BIT(4)
#define   PHB_IVC_UPDATE_ENABLE_CON     PPC_BIT(5)
#define   PHB_IVC_UPDATE_GEN_MATCH      PPC_BITMASK(6, 7)
#define   PHB_IVC_UPDATE_SERVER         PPC_BITMASK(8, 23)
#define   PHB_IVC_UPDATE_PRI            PPC_BITMASK(24, 31)
#define   PHB_IVC_UPDATE_GEN            PPC_BITMASK(32, 33)
#define   PHB_IVC_UPDATE_P              PPC_BITMASK(34, 34)
#define   PHB_IVC_UPDATE_Q              PPC_BITMASK(35, 35)
#define   PHB_IVC_UPDATE_SID            PPC_BITMASK(48, 63)
#define PHB_PAPR_ERR_INJ_CTL            0x2b0
#define   PHB_PAPR_ERR_INJ_CTL_INB      PPC_BIT(0)
#define   PHB_PAPR_ERR_INJ_CTL_OUTB     PPC_BIT(1)
#define   PHB_PAPR_ERR_INJ_CTL_STICKY   PPC_BIT(2)
#define   PHB_PAPR_ERR_INJ_CTL_CFG      PPC_BIT(3)
#define   PHB_PAPR_ERR_INJ_CTL_RD       PPC_BIT(4)
#define   PHB_PAPR_ERR_INJ_CTL_WR       PPC_BIT(5)
#define   PHB_PAPR_ERR_INJ_CTL_FREEZE   PPC_BIT(6)
#define PHB_PAPR_ERR_INJ_ADDR           0x2b8
#define   PHB_PAPR_ERR_INJ_ADDR_MMIO            PPC_BITMASK(16, 63)
#define PHB_PAPR_ERR_INJ_MASK           0x2c0
#define   PHB_PAPR_ERR_INJ_MASK_CFG             PPC_BITMASK(4, 11)
#define   PHB_PAPR_ERR_INJ_MASK_MMIO            PPC_BITMASK(16, 63)
#define PHB_ETU_ERR_SUMMARY             0x2c8

/*  UTL registers */
#define UTL_SYS_BUS_CONTROL             0x400
#define UTL_STATUS                      0x408
#define UTL_SYS_BUS_AGENT_STATUS        0x410
#define UTL_SYS_BUS_AGENT_ERR_SEVERITY  0x418
#define UTL_SYS_BUS_AGENT_IRQ_EN        0x420
#define UTL_SYS_BUS_BURST_SZ_CONF       0x440
#define UTL_REVISION_ID                 0x448
#define UTL_BCLK_DOMAIN_DBG1            0x460
#define UTL_BCLK_DOMAIN_DBG2            0x468
#define UTL_BCLK_DOMAIN_DBG3            0x470
#define UTL_BCLK_DOMAIN_DBG4            0x478
#define UTL_BCLK_DOMAIN_DBG5            0x480
#define UTL_BCLK_DOMAIN_DBG6            0x488
#define UTL_OUT_POST_HDR_BUF_ALLOC      0x4c0
#define UTL_OUT_POST_DAT_BUF_ALLOC      0x4d0
#define UTL_IN_POST_HDR_BUF_ALLOC       0x4e0
#define UTL_IN_POST_DAT_BUF_ALLOC       0x4f0
#define UTL_OUT_NP_BUF_ALLOC            0x500
#define UTL_IN_NP_BUF_ALLOC             0x510
#define UTL_PCIE_TAGS_ALLOC             0x520
#define UTL_GBIF_READ_TAGS_ALLOC        0x530
#define UTL_PCIE_PORT_CONTROL           0x540
#define UTL_PCIE_PORT_STATUS            0x548
#define UTL_PCIE_PORT_ERROR_SEV         0x550
#define UTL_PCIE_PORT_IRQ_EN            0x558
#define UTL_RC_STATUS                   0x560
#define UTL_RC_ERR_SEVERITY             0x568
#define UTL_RC_IRQ_EN                   0x570
#define UTL_EP_STATUS                   0x578
#define UTL_EP_ERR_SEVERITY             0x580
#define UTL_EP_ERR_IRQ_EN               0x588
#define UTL_PCI_PM_CTRL1                0x590
#define UTL_PCI_PM_CTRL2                0x598
#define UTL_GP_CTL1                     0x5a0
#define UTL_GP_CTL2                     0x5a8
#define UTL_PCLK_DOMAIN_DBG1            0x5b0
#define UTL_PCLK_DOMAIN_DBG2            0x5b8
#define UTL_PCLK_DOMAIN_DBG3            0x5c0
#define UTL_PCLK_DOMAIN_DBG4            0x5c8

/* PCI-E Stack registers */
#define PHB_PCIE_SYSTEM_CONFIG          0x600
#define PHB_PCIE_BUS_NUMBER             0x608
#define PHB_PCIE_SYSTEM_TEST            0x618
#define PHB_PCIE_LINK_MANAGEMENT        0x630
#define   PHB_PCIE_LM_LINK_ACTIVE       PPC_BIT(8)
#define PHB_PCIE_DLP_TRAIN_CTL          0x640
#define   PHB_PCIE_DLP_TCTX_DISABLE     PPC_BIT(1)
#define   PHB_PCIE_DLP_TCRX_DISABLED    PPC_BIT(16)
#define   PHB_PCIE_DLP_INBAND_PRESENCE  PPC_BIT(19)
#define   PHB_PCIE_DLP_TC_DL_LINKUP     PPC_BIT(21)
#define   PHB_PCIE_DLP_TC_DL_PGRESET    PPC_BIT(22)
#define   PHB_PCIE_DLP_TC_DL_LINKACT    PPC_BIT(23)
#define PHB_PCIE_SLOP_LOOPBACK_STATUS   0x648
#define PHB_PCIE_SYS_LINK_INIT          0x668
#define PHB_PCIE_UTL_CONFIG             0x670
#define PHB_PCIE_DLP_CONTROL            0x678
#define PHB_PCIE_UTL_ERRLOG1            0x680
#define PHB_PCIE_UTL_ERRLOG2            0x688
#define PHB_PCIE_UTL_ERRLOG3            0x690
#define PHB_PCIE_UTL_ERRLOG4            0x698
#define PHB_PCIE_DLP_ERRLOG1            0x6a0
#define PHB_PCIE_DLP_ERRLOG2            0x6a8
#define PHB_PCIE_DLP_ERR_STATUS         0x6b0
#define PHB_PCIE_DLP_ERR_COUNTERS       0x6b8
#define PHB_PCIE_UTL_ERR_INJECT         0x6c0
#define PHB_PCIE_TLDLP_ERR_INJECT       0x6c8
#define PHB_PCIE_LANE_EQ_CNTL0          0x6d0
#define PHB_PCIE_LANE_EQ_CNTL1          0x6d8
#define PHB_PCIE_LANE_EQ_CNTL2          0x6e0
#define PHB_PCIE_LANE_EQ_CNTL3          0x6e8
#define PHB_PCIE_STRAPPING              0x700

/* Fundamental register set B */
#define PHB_VERSION                     0x800
#define PHB_RESET                       0x808
#define PHB_CONTROL                     0x810
#define   PHB_CTRL_IVE_128_BYTES        PPC_BIT(24)
#define PHB_AIB_RX_CRED_INIT_TIMER      0x818
#define PHB_AIB_RX_CMD_CRED             0x820
#define PHB_AIB_RX_DATA_CRED            0x828
#define PHB_AIB_TX_CMD_CRED             0x830
#define PHB_AIB_TX_DATA_CRED            0x838
#define PHB_AIB_TX_CHAN_MAPPING         0x840
#define PHB_AIB_TAG_ENABLE              0x858
#define PHB_AIB_FENCE_CTRL              0x860
#define PHB_TCE_TAG_ENABLE              0x868
#define PHB_TCE_WATERMARK               0x870
#define PHB_TIMEOUT_CTRL1               0x878
#define PHB_TIMEOUT_CTRL2               0x880
#define PHB_Q_DMA_R                     0x888
#define   PHB_Q_DMA_R_QUIESCE_DMA       PPC_BIT(0)
#define   PHB_Q_DMA_R_AUTORESET         PPC_BIT(1)
#define   PHB_Q_DMA_R_DMA_RESP_STATUS   PPC_BIT(4)
#define   PHB_Q_DMA_R_MMIO_RESP_STATUS  PPC_BIT(5)
#define   PHB_Q_DMA_R_TCE_RESP_STATUS   PPC_BIT(6)
#define PHB_AIB_TAG_STATUS              0x900
#define PHB_TCE_TAG_STATUS              0x908

/* FIR & Error registers */
#define PHB_LEM_FIR_ACCUM               0xc00
#define PHB_LEM_FIR_AND_MASK            0xc08
#define PHB_LEM_FIR_OR_MASK             0xc10
#define PHB_LEM_ERROR_MASK              0xc18
#define PHB_LEM_ERROR_AND_MASK          0xc20
#define PHB_LEM_ERROR_OR_MASK           0xc28
#define PHB_LEM_ACTION0                 0xc30
#define PHB_LEM_ACTION1                 0xc38
#define PHB_LEM_WOF                     0xc40
#define PHB_ERR_STATUS                  0xc80
#define PHB_ERR1_STATUS                 0xc88
#define PHB_ERR_INJECT                  0xc90
#define PHB_ERR_LEM_ENABLE              0xc98
#define PHB_ERR_IRQ_ENABLE              0xca0
#define PHB_ERR_FREEZE_ENABLE           0xca8
#define PHB_ERR_AIB_FENCE_ENABLE        0xcb0
#define PHB_ERR_LOG_0                   0xcc0
#define PHB_ERR_LOG_1                   0xcc8
#define PHB_ERR_STATUS_MASK             0xcd0
#define PHB_ERR1_STATUS_MASK            0xcd8

#define PHB_OUT_ERR_STATUS              0xd00
#define PHB_OUT_ERR1_STATUS             0xd08
#define PHB_OUT_ERR_INJECT              0xd10
#define PHB_OUT_ERR_LEM_ENABLE          0xd18
#define PHB_OUT_ERR_IRQ_ENABLE          0xd20
#define PHB_OUT_ERR_FREEZE_ENABLE       0xd28
#define PHB_OUT_ERR_AIB_FENCE_ENABLE    0xd30
#define PHB_OUT_ERR_LOG_0               0xd40
#define PHB_OUT_ERR_LOG_1               0xd48
#define PHB_OUT_ERR_STATUS_MASK         0xd50
#define PHB_OUT_ERR1_STATUS_MASK        0xd58

#define PHB_INA_ERR_STATUS              0xd80
#define PHB_INA_ERR1_STATUS             0xd88
#define PHB_INA_ERR_INJECT              0xd90
#define PHB_INA_ERR_LEM_ENABLE          0xd98
#define PHB_INA_ERR_IRQ_ENABLE          0xda0
#define PHB_INA_ERR_FREEZE_ENABLE       0xda8
#define PHB_INA_ERR_AIB_FENCE_ENABLE    0xdb0
#define PHB_INA_ERR_LOG_0               0xdc0
#define PHB_INA_ERR_LOG_1               0xdc8
#define PHB_INA_ERR_STATUS_MASK         0xdd0
#define PHB_INA_ERR1_STATUS_MASK        0xdd8

#define PHB_INB_ERR_STATUS              0xe00
#define PHB_INB_ERR1_STATUS             0xe08
#define PHB_INB_ERR_INJECT              0xe10
#define PHB_INB_ERR_LEM_ENABLE          0xe18
#define PHB_INB_ERR_IRQ_ENABLE          0xe20
#define PHB_INB_ERR_FREEZE_ENABLE       0xe28
#define PHB_INB_ERR_AIB_FENCE_ENABLE    0xe30
#define PHB_INB_ERR_LOG_0               0xe40
#define PHB_INB_ERR_LOG_1               0xe48
#define PHB_INB_ERR_STATUS_MASK         0xe50
#define PHB_INB_ERR1_STATUS_MASK        0xe58

/* Performance monitor & Debug registers */
#define PHB_TRACE_CONTROL               0xf80
#define PHB_PERFMON_CONFIG              0xf88
#define PHB_PERFMON_CTR0                0xf90
#define PHB_PERFMON_CTR1                0xf98
#define PHB_PERFMON_CTR2                0xfa0
#define PHB_PERFMON_CTR3                0xfa8
#define PHB_HOTPLUG_OVERRIDE            0xfb0
#define   PHB_HPOVR_FORCE_RESAMPLE      PPC_BIT(9)
#define   PHB_HPOVR_PRESENCE_A          PPC_BIT(10)
#define   PHB_HPOVR_PRESENCE_B          PPC_BIT(11)
#define   PHB_HPOVR_LINK_ACTIVE         PPC_BIT(12)
#define   PHB_HPOVR_LINK_BIFURCATED     PPC_BIT(13)
#define   PHB_HPOVR_LINK_LANE_SWAPPED   PPC_BIT(14)

/*
 * IODA2 on-chip tables
 */

#define IODA2_TBL_LIST          1
#define IODA2_TBL_LXIVT         2
#define IODA2_TBL_IVC_CAM       3
#define IODA2_TBL_RBA           4
#define IODA2_TBL_RCAM          5
#define IODA2_TBL_MRT           6
#define IODA2_TBL_PESTA         7
#define IODA2_TBL_PESTB         8
#define IODA2_TBL_TVT           9
#define IODA2_TBL_TCAM          10
#define IODA2_TBL_TDR           11
#define IODA2_TBL_M64BT         16
#define IODA2_TBL_M32DT         17
#define IODA2_TBL_PEEV          20

/* LXIVT */
#define IODA2_LXIVT_SERVER              PPC_BITMASK(8, 23)
#define IODA2_LXIVT_PRIORITY            PPC_BITMASK(24, 31)
#define IODA2_LXIVT_NODE_ID             PPC_BITMASK(56, 63)

/* IVT */
#define IODA2_IVT_SERVER                PPC_BITMASK(0, 23)
#define IODA2_IVT_PRIORITY              PPC_BITMASK(24, 31)
#define IODA2_IVT_GEN                   PPC_BITMASK(37, 38)
#define IODA2_IVT_P                     PPC_BITMASK(39, 39)
#define IODA2_IVT_Q                     PPC_BITMASK(47, 47)
#define IODA2_IVT_PE                    PPC_BITMASK(48, 63)

/* TVT */
#define IODA2_TVT_TABLE_ADDR            PPC_BITMASK(0, 47)
#define IODA2_TVT_NUM_LEVELS            PPC_BITMASK(48, 50)
#define   IODA2_TVE_1_LEVEL     0
#define   IODA2_TVE_2_LEVELS    1
#define   IODA2_TVE_3_LEVELS    2
#define   IODA2_TVE_4_LEVELS    3
#define   IODA2_TVE_5_LEVELS    4
#define IODA2_TVT_TCE_TABLE_SIZE        PPC_BITMASK(51, 55)
#define IODA2_TVT_IO_PSIZE              PPC_BITMASK(59, 63)

/* PESTA */
#define IODA2_PESTA_MMIO_FROZEN         PPC_BIT(0)

/* PESTB */
#define IODA2_PESTB_DMA_STOPPED         PPC_BIT(0)

/* M32DT */
#define IODA2_M32DT_PE                  PPC_BITMASK(8, 15)

/* M64BT */
#define IODA2_M64BT_ENABLE              PPC_BIT(0)
#define IODA2_M64BT_SINGLE_PE           PPC_BIT(1)
#define IODA2_M64BT_BASE                PPC_BITMASK(2, 31)
#define IODA2_M64BT_MASK                PPC_BITMASK(34, 63)
#define IODA2_M64BT_SINGLE_BASE         PPC_BITMASK(2, 26)
#define IODA2_M64BT_PE_HI               PPC_BITMASK(27, 31)
#define IODA2_M64BT_SINGLE_MASK         PPC_BITMASK(34, 58)
#define IODA2_M64BT_PE_LOW              PPC_BITMASK(59, 63)

/*
 * IODA2 in-memory tables
 */

/*
 * PEST
 *
 * 2x8 bytes entries, PEST0 and PEST1
 */

#define IODA2_PEST0_MMIO_CAUSE          PPC_BIT(2)
#define IODA2_PEST0_CFG_READ            PPC_BIT(3)
#define IODA2_PEST0_CFG_WRITE           PPC_BIT(4)
#define IODA2_PEST0_TTYPE               PPC_BITMASK(5, 7)
#define   PEST_TTYPE_DMA_WRITE          0
#define   PEST_TTYPE_MSI                1
#define   PEST_TTYPE_DMA_READ           2
#define   PEST_TTYPE_DMA_READ_RESP      3
#define   PEST_TTYPE_MMIO_LOAD          4
#define   PEST_TTYPE_MMIO_STORE         5
#define   PEST_TTYPE_OTHER              7
#define IODA2_PEST0_CA_RETURN           PPC_BIT(8)
#define IODA2_PEST0_UTL_RTOS_TIMEOUT    PPC_BIT(8) /* Same bit as CA return */
#define IODA2_PEST0_UR_RETURN           PPC_BIT(9)
#define IODA2_PEST0_UTL_NONFATAL        PPC_BIT(10)
#define IODA2_PEST0_UTL_FATAL           PPC_BIT(11)
#define IODA2_PEST0_PARITY_UE           PPC_BIT(13)
#define IODA2_PEST0_UTL_CORRECTABLE     PPC_BIT(14)
#define IODA2_PEST0_UTL_INTERRUPT       PPC_BIT(15)
#define IODA2_PEST0_MMIO_XLATE          PPC_BIT(16)
#define IODA2_PEST0_IODA2_ERROR         PPC_BIT(16) /* Same bit as MMIO xlate */
#define IODA2_PEST0_TCE_PAGE_FAULT      PPC_BIT(18)
#define IODA2_PEST0_TCE_ACCESS_FAULT    PPC_BIT(19)
#define IODA2_PEST0_DMA_RESP_TIMEOUT    PPC_BIT(20)
#define IODA2_PEST0_AIB_SIZE_INVALID    PPC_BIT(21)
#define IODA2_PEST0_LEM_BIT             PPC_BITMASK(26, 31)
#define IODA2_PEST0_RID                 PPC_BITMASK(32, 47)
#define IODA2_PEST0_MSI_DATA            PPC_BITMASK(48, 63)

#define IODA2_PEST1_FAIL_ADDR           PPC_BITMASK(3, 63)


#endif /* PCI_HOST_PNV_PHB3_REGS_H */
