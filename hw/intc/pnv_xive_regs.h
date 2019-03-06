/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_XIVE_REGS_H
#define PPC_PNV_XIVE_REGS_H

/* IC register offsets 0x0 - 0x400 */
#define CQ_SWI_CMD_HIST         0x020
#define CQ_SWI_CMD_POLL         0x028
#define CQ_SWI_CMD_BCAST        0x030
#define CQ_SWI_CMD_ASSIGN       0x038
#define CQ_SWI_CMD_BLK_UPD      0x040
#define CQ_SWI_RSP              0x048
#define CQ_CFG_PB_GEN           0x050
#define   CQ_INT_ADDR_OPT       PPC_BITMASK(14, 15)
#define CQ_MSGSND               0x058
#define CQ_CNPM_SEL             0x078
#define CQ_IC_BAR               0x080
#define   CQ_IC_BAR_VALID       PPC_BIT(0)
#define   CQ_IC_BAR_64K         PPC_BIT(1)
#define CQ_TM1_BAR              0x90
#define CQ_TM2_BAR              0x0a0
#define   CQ_TM_BAR_VALID       PPC_BIT(0)
#define   CQ_TM_BAR_64K         PPC_BIT(1)
#define CQ_PC_BAR               0x0b0
#define  CQ_PC_BAR_VALID        PPC_BIT(0)
#define CQ_PC_BARM              0x0b8
#define  CQ_PC_BARM_MASK        PPC_BITMASK(26, 38)
#define CQ_VC_BAR               0x0c0
#define  CQ_VC_BAR_VALID        PPC_BIT(0)
#define CQ_VC_BARM              0x0c8
#define  CQ_VC_BARM_MASK        PPC_BITMASK(21, 37)
#define CQ_TAR                  0x0f0
#define  CQ_TAR_TBL_AUTOINC     PPC_BIT(0)
#define  CQ_TAR_TSEL            PPC_BITMASK(12, 15)
#define  CQ_TAR_TSEL_BLK        PPC_BIT(12)
#define  CQ_TAR_TSEL_MIG        PPC_BIT(13)
#define  CQ_TAR_TSEL_VDT        PPC_BIT(14)
#define  CQ_TAR_TSEL_EDT        PPC_BIT(15)
#define  CQ_TAR_TSEL_INDEX      PPC_BITMASK(26, 31)
#define CQ_TDR                  0x0f8
#define  CQ_TDR_VDT_VALID       PPC_BIT(0)
#define  CQ_TDR_VDT_BLK         PPC_BITMASK(11, 15)
#define  CQ_TDR_VDT_INDEX       PPC_BITMASK(28, 31)
#define  CQ_TDR_EDT_TYPE        PPC_BITMASK(0, 1)
#define  CQ_TDR_EDT_INVALID     0
#define  CQ_TDR_EDT_IPI         1
#define  CQ_TDR_EDT_EQ          2
#define  CQ_TDR_EDT_BLK         PPC_BITMASK(12, 15)
#define  CQ_TDR_EDT_INDEX       PPC_BITMASK(26, 31)
#define CQ_PBI_CTL              0x100
#define  CQ_PBI_PC_64K          PPC_BIT(5)
#define  CQ_PBI_VC_64K          PPC_BIT(6)
#define  CQ_PBI_LNX_TRIG        PPC_BIT(7)
#define  CQ_PBI_FORCE_TM_LOCAL  PPC_BIT(22)
#define CQ_PBO_CTL              0x108
#define CQ_AIB_CTL              0x110
#define CQ_RST_CTL              0x118
#define CQ_FIRMASK              0x198
#define CQ_FIRMASK_AND          0x1a0
#define CQ_FIRMASK_OR           0x1a8

/* PC LBS1 register offsets 0x400 - 0x800 */
#define PC_TCTXT_CFG            0x400
#define  PC_TCTXT_CFG_BLKGRP_EN         PPC_BIT(0)
#define  PC_TCTXT_CFG_TARGET_EN         PPC_BIT(1)
#define  PC_TCTXT_CFG_LGS_EN            PPC_BIT(2)
#define  PC_TCTXT_CFG_STORE_ACK         PPC_BIT(3)
#define  PC_TCTXT_CFG_HARD_CHIPID_BLK   PPC_BIT(8)
#define  PC_TCTXT_CHIPID_OVERRIDE       PPC_BIT(9)
#define  PC_TCTXT_CHIPID                PPC_BITMASK(12, 15)
#define  PC_TCTXT_INIT_AGE              PPC_BITMASK(30, 31)
#define PC_TCTXT_TRACK          0x408
#define  PC_TCTXT_TRACK_EN              PPC_BIT(0)
#define PC_TCTXT_INDIR0         0x420
#define  PC_TCTXT_INDIR_VALID           PPC_BIT(0)
#define  PC_TCTXT_INDIR_THRDID          PPC_BITMASK(9, 15)
#define PC_TCTXT_INDIR1         0x428
#define PC_TCTXT_INDIR2         0x430
#define PC_TCTXT_INDIR3         0x438
#define PC_THREAD_EN_REG0       0x440
#define PC_THREAD_EN_REG0_SET   0x448
#define PC_THREAD_EN_REG0_CLR   0x450
#define PC_THREAD_EN_REG1       0x460
#define PC_THREAD_EN_REG1_SET   0x468
#define PC_THREAD_EN_REG1_CLR   0x470
#define PC_GLOBAL_CONFIG        0x480
#define  PC_GCONF_INDIRECT      PPC_BIT(32)
#define  PC_GCONF_CHIPID_OVR    PPC_BIT(40)
#define  PC_GCONF_CHIPID        PPC_BITMASK(44, 47)
#define PC_VSD_TABLE_ADDR       0x488
#define PC_VSD_TABLE_DATA       0x490
#define PC_AT_KILL              0x4b0
#define  PC_AT_KILL_VALID       PPC_BIT(0)
#define  PC_AT_KILL_BLOCK_ID    PPC_BITMASK(27, 31)
#define  PC_AT_KILL_OFFSET      PPC_BITMASK(48, 60)
#define PC_AT_KILL_MASK         0x4b8

/* PC LBS2 register offsets */
#define PC_VPC_CACHE_ENABLE     0x708
#define  PC_VPC_CACHE_EN_MASK   PPC_BITMASK(0, 31)
#define PC_VPC_SCRUB_TRIG       0x710
#define PC_VPC_SCRUB_MASK       0x718
#define  PC_SCRUB_VALID         PPC_BIT(0)
#define  PC_SCRUB_WANT_DISABLE  PPC_BIT(1)
#define  PC_SCRUB_WANT_INVAL    PPC_BIT(2)
#define  PC_SCRUB_BLOCK_ID      PPC_BITMASK(27, 31)
#define  PC_SCRUB_OFFSET        PPC_BITMASK(45, 63)
#define PC_VPC_CWATCH_SPEC      0x738
#define  PC_VPC_CWATCH_CONFLICT PPC_BIT(0)
#define  PC_VPC_CWATCH_FULL     PPC_BIT(8)
#define  PC_VPC_CWATCH_BLOCKID  PPC_BITMASK(27, 31)
#define  PC_VPC_CWATCH_OFFSET   PPC_BITMASK(45, 63)
#define PC_VPC_CWATCH_DAT0      0x740
#define PC_VPC_CWATCH_DAT1      0x748
#define PC_VPC_CWATCH_DAT2      0x750
#define PC_VPC_CWATCH_DAT3      0x758
#define PC_VPC_CWATCH_DAT4      0x760
#define PC_VPC_CWATCH_DAT5      0x768
#define PC_VPC_CWATCH_DAT6      0x770
#define PC_VPC_CWATCH_DAT7      0x778

/* VC0 register offsets 0x800 - 0xFFF */
#define VC_GLOBAL_CONFIG        0x800
#define  VC_GCONF_INDIRECT      PPC_BIT(32)
#define VC_VSD_TABLE_ADDR       0x808
#define VC_VSD_TABLE_DATA       0x810
#define VC_IVE_ISB_BLOCK_MODE   0x818
#define VC_EQD_BLOCK_MODE       0x820
#define VC_VPS_BLOCK_MODE       0x828
#define VC_IRQ_CONFIG_IPI       0x840
#define  VC_IRQ_CONFIG_MEMB_EN  PPC_BIT(45)
#define  VC_IRQ_CONFIG_MEMB_SZ  PPC_BITMASK(46, 51)
#define VC_IRQ_CONFIG_HW        0x848
#define VC_IRQ_CONFIG_CASCADE1  0x850
#define VC_IRQ_CONFIG_CASCADE2  0x858
#define VC_IRQ_CONFIG_REDIST    0x860
#define VC_IRQ_CONFIG_IPI_CASC  0x868
#define  VC_AIB_TX_ORDER_TAG2_REL_TF    PPC_BIT(20)
#define VC_AIB_TX_ORDER_TAG2    0x890
#define VC_AT_MACRO_KILL        0x8b0
#define VC_AT_MACRO_KILL_MASK   0x8b8
#define  VC_KILL_VALID          PPC_BIT(0)
#define  VC_KILL_TYPE           PPC_BITMASK(14, 15)
#define   VC_KILL_IRQ   0
#define   VC_KILL_IVC   1
#define   VC_KILL_SBC   2
#define   VC_KILL_EQD   3
#define  VC_KILL_BLOCK_ID       PPC_BITMASK(27, 31)
#define  VC_KILL_OFFSET         PPC_BITMASK(48, 60)
#define VC_EQC_CACHE_ENABLE     0x908
#define  VC_EQC_CACHE_EN_MASK   PPC_BITMASK(0, 15)
#define VC_EQC_SCRUB_TRIG       0x910
#define VC_EQC_SCRUB_MASK       0x918
#define VC_EQC_CONFIG           0x920
#define X_VC_EQC_CONFIG         0x214 /* XSCOM register */
#define  VC_EQC_CONF_SYNC_IPI           PPC_BIT(32)
#define  VC_EQC_CONF_SYNC_HW            PPC_BIT(33)
#define  VC_EQC_CONF_SYNC_ESC1          PPC_BIT(34)
#define  VC_EQC_CONF_SYNC_ESC2          PPC_BIT(35)
#define  VC_EQC_CONF_SYNC_REDI          PPC_BIT(36)
#define  VC_EQC_CONF_EQP_INTERLEAVE     PPC_BIT(38)
#define  VC_EQC_CONF_ENABLE_END_s_BIT   PPC_BIT(39)
#define  VC_EQC_CONF_ENABLE_END_u_BIT   PPC_BIT(40)
#define  VC_EQC_CONF_ENABLE_END_c_BIT   PPC_BIT(41)
#define  VC_EQC_CONF_ENABLE_MORE_QSZ    PPC_BIT(42)
#define  VC_EQC_CONF_SKIP_ESCALATE      PPC_BIT(43)
#define VC_EQC_CWATCH_SPEC      0x928
#define  VC_EQC_CWATCH_CONFLICT PPC_BIT(0)
#define  VC_EQC_CWATCH_FULL     PPC_BIT(8)
#define  VC_EQC_CWATCH_BLOCKID  PPC_BITMASK(28, 31)
#define  VC_EQC_CWATCH_OFFSET   PPC_BITMASK(40, 63)
#define VC_EQC_CWATCH_DAT0      0x930
#define VC_EQC_CWATCH_DAT1      0x938
#define VC_EQC_CWATCH_DAT2      0x940
#define VC_EQC_CWATCH_DAT3      0x948
#define VC_IVC_SCRUB_TRIG       0x990
#define VC_IVC_SCRUB_MASK       0x998
#define VC_SBC_SCRUB_TRIG       0xa10
#define VC_SBC_SCRUB_MASK       0xa18
#define  VC_SCRUB_VALID         PPC_BIT(0)
#define  VC_SCRUB_WANT_DISABLE  PPC_BIT(1)
#define  VC_SCRUB_WANT_INVAL    PPC_BIT(2) /* EQC and SBC only */
#define  VC_SCRUB_BLOCK_ID      PPC_BITMASK(28, 31)
#define  VC_SCRUB_OFFSET        PPC_BITMASK(40, 63)
#define VC_IVC_CACHE_ENABLE     0x988
#define  VC_IVC_CACHE_EN_MASK   PPC_BITMASK(0, 15)
#define VC_SBC_CACHE_ENABLE     0xa08
#define  VC_SBC_CACHE_EN_MASK   PPC_BITMASK(0, 15)
#define VC_IVC_CACHE_SCRUB_TRIG 0x990
#define VC_IVC_CACHE_SCRUB_MASK 0x998
#define VC_SBC_CACHE_ENABLE     0xa08
#define VC_SBC_CACHE_SCRUB_TRIG 0xa10
#define VC_SBC_CACHE_SCRUB_MASK 0xa18
#define VC_SBC_CONFIG           0xa20
#define  VC_SBC_CONF_CPLX_CIST  PPC_BIT(44)
#define  VC_SBC_CONF_CIST_BOTH  PPC_BIT(45)
#define  VC_SBC_CONF_NO_UPD_PRF PPC_BIT(59)

/* VC1 register offsets */

/* VSD Table address register definitions (shared) */
#define VST_ADDR_AUTOINC        PPC_BIT(0)
#define VST_TABLE_SELECT        PPC_BITMASK(13, 15)
#define  VST_TSEL_IVT   0
#define  VST_TSEL_SBE   1
#define  VST_TSEL_EQDT  2
#define  VST_TSEL_VPDT  3
#define  VST_TSEL_IRQ   4       /* VC only */
#define VST_TABLE_BLOCK        PPC_BITMASK(27, 31)

/* Number of queue overflow pages */
#define VC_QUEUE_OVF_COUNT      6

/*
 * Bits in a VSD entry.
 *
 * Note: the address is naturally aligned,  we don't use a PPC_BITMASK,
 *       but just a mask to apply to the address before OR'ing it in.
 *
 * Note: VSD_FIRMWARE is a SW bit ! It hijacks an unused bit in the
 *       VSD and is only meant to be used in indirect mode !
 */
#define VSD_MODE                PPC_BITMASK(0, 1)
#define  VSD_MODE_SHARED        1
#define  VSD_MODE_EXCLUSIVE     2
#define  VSD_MODE_FORWARD       3
#define VSD_ADDRESS_MASK        0x0ffffffffffff000ull
#define VSD_MIGRATION_REG       PPC_BITMASK(52, 55)
#define VSD_INDIRECT            PPC_BIT(56)
#define VSD_TSIZE               PPC_BITMASK(59, 63)
#define VSD_FIRMWARE            PPC_BIT(2) /* Read warning above */

#define VC_EQC_SYNC_MASK         \
        (VC_EQC_CONF_SYNC_IPI  | \
         VC_EQC_CONF_SYNC_HW   | \
         VC_EQC_CONF_SYNC_ESC1 | \
         VC_EQC_CONF_SYNC_ESC2 | \
         VC_EQC_CONF_SYNC_REDI)


#endif /* PPC_PNV_XIVE_REGS_H */
