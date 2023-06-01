/*
 * QEMU PowerPC XIVE2 interrupt controller model  (POWER10)
 *
 * Copyright (c) 2019-2022, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_XIVE2_REGS_H
#define PPC_PNV_XIVE2_REGS_H

/*
 * CQ Common Queue (PowerBus bridge) Registers
 */

/* XIVE2 Capabilities */
#define X_CQ_XIVE_CAP                           0x02
#define CQ_XIVE_CAP                             0x010
#define    CQ_XIVE_CAP_VERSION                  PPC_BITMASK(0, 3)
/* 4:6 reserved */
#define    CQ_XIVE_CAP_USER_INT_PRIO            PPC_BITMASK(8, 9)
#define       CQ_XIVE_CAP_USER_INT_PRIO_1       0
#define       CQ_XIVE_CAP_USER_INT_PRIO_1_2     1
#define       CQ_XIVE_CAP_USER_INT_PRIO_1_4     2
#define       CQ_XIVE_CAP_USER_INT_PRIO_1_8     3
#define    CQ_XIVE_CAP_VP_INT_PRIO              PPC_BITMASK(10, 11)
#define       CQ_XIVE_CAP_VP_INT_PRIO_1_8       0
#define       CQ_XIVE_CAP_VP_INT_PRIO_2_8       1
#define       CQ_XIVE_CAP_VP_INT_PRIO_4_8       2
#define       CQ_XIVE_CAP_VP_INT_PRIO_8         3
#define    CQ_XIVE_CAP_BLOCK_ID_WIDTH           PPC_BITMASK(12, 13)
#define    CQ_XIVE_CAP_VP_SAVE_RESTORE          PPC_BIT(38)

#define    CQ_XIVE_CAP_PHB_PQ_DISABLE           PPC_BIT(56)
#define    CQ_XIVE_CAP_PHB_ABT                  PPC_BIT(57)
#define    CQ_XIVE_CAP_EXPLOITATION_MODE        PPC_BIT(58)
#define    CQ_XIVE_CAP_STORE_EOI                PPC_BIT(59)

/* XIVE2 Configuration */
#define X_CQ_XIVE_CFG                           0x03
#define CQ_XIVE_CFG                             0x018

/* 0:7 reserved */
#define    CQ_XIVE_CFG_USER_INT_PRIO            PPC_BITMASK(8, 9)
#define    CQ_XIVE_CFG_VP_INT_PRIO              PPC_BITMASK(10, 11)
#define       CQ_XIVE_CFG_INT_PRIO_1            0
#define       CQ_XIVE_CFG_INT_PRIO_2            1
#define       CQ_XIVE_CFG_INT_PRIO_4            2
#define       CQ_XIVE_CFG_INT_PRIO_8            3
#define    CQ_XIVE_CFG_BLOCK_ID_WIDTH           PPC_BITMASK(12, 13)
#define       CQ_XIVE_CFG_BLOCK_ID_4BITS        0
#define       CQ_XIVE_CFG_BLOCK_ID_5BITS        1
#define       CQ_XIVE_CFG_BLOCK_ID_6BITS        2
#define       CQ_XIVE_CFG_BLOCK_ID_7BITS        3
#define    CQ_XIVE_CFG_HYP_HARD_RANGE           PPC_BITMASK(14, 15)
#define       CQ_XIVE_CFG_THREADID_7BITS        0
#define       CQ_XIVE_CFG_THREADID_8BITS        1
#define       CQ_XIVE_CFG_THREADID_9BITS        2
#define       CQ_XIVE_CFG_THREADID_10BITs       3
#define    CQ_XIVE_CFG_HYP_HARD_BLKID_OVERRIDE  PPC_BIT(16)
#define    CQ_XIVE_CFG_HYP_HARD_BLOCK_ID        PPC_BITMASK(17, 23)

#define    CQ_XIVE_CFG_GEN1_TIMA_OS             PPC_BIT(24)
#define    CQ_XIVE_CFG_GEN1_TIMA_HYP            PPC_BIT(25)
#define    CQ_XIVE_CFG_GEN1_TIMA_HYP_BLK0       PPC_BIT(26) /* 0 if bit[25]=0 */
#define    CQ_XIVE_CFG_GEN1_TIMA_CROWD_DIS      PPC_BIT(27) /* 0 if bit[25]=0 */
#define    CQ_XIVE_CFG_GEN1_END_ESX             PPC_BIT(28)
#define    CQ_XIVE_CFG_EN_VP_SAVE_RESTORE       PPC_BIT(38) /* 0 if bit[25]=1 */
#define    CQ_XIVE_CFG_EN_VP_SAVE_REST_STRICT   PPC_BIT(39) /* 0 if bit[25]=1 */

/* Interrupt Controller Base Address Register - 512 pages (32M) */
#define X_CQ_IC_BAR                             0x08
#define CQ_IC_BAR                               0x040
#define    CQ_IC_BAR_VALID                      PPC_BIT(0)
#define    CQ_IC_BAR_64K                        PPC_BIT(1)
/* 2:7 reserved */
#define    CQ_IC_BAR_ADDR                       PPC_BITMASK(8, 42)
/* 43:63 reserved */

/* Thread Management Base Address Register - 4 pages */
#define X_CQ_TM_BAR                             0x09
#define CQ_TM_BAR                               0x048
#define    CQ_TM_BAR_VALID                      PPC_BIT(0)
#define    CQ_TM_BAR_64K                        PPC_BIT(1)
#define    CQ_TM_BAR_ADDR                       PPC_BITMASK(8, 49)

/* ESB Base Address Register */
#define X_CQ_ESB_BAR                            0x0A
#define CQ_ESB_BAR                              0x050
#define    CQ_BAR_VALID                         PPC_BIT(0)
#define    CQ_BAR_64K                           PPC_BIT(1)
/* 2:7 reserved */
#define    CQ_BAR_ADDR                          PPC_BITMASK(8, 39)
#define    CQ_BAR_SET_DIV                       PPC_BITMASK(56, 58)
#define    CQ_BAR_RANGE                         PPC_BITMASK(59, 63)
                                                /* 0 (16M) - 16 (16T) */

/* END Base Address Register */
#define X_CQ_END_BAR                            0x0B
#define CQ_END_BAR                              0x058

/* NVPG Base Address Register */
#define X_CQ_NVPG_BAR                           0x0C
#define CQ_NVPG_BAR                             0x060

/* NVC Base Address Register */
#define X_CQ_NVC_BAR                            0x0D
#define CQ_NVC_BAR                              0x068

/* Table Address Register */
#define X_CQ_TAR                                0x0E
#define CQ_TAR                                  0x070
#define     CQ_TAR_AUTOINC                      PPC_BIT(0)
#define     CQ_TAR_SELECT                       PPC_BITMASK(12, 15)
#define     CQ_TAR_ESB                          0       /* 0 - 15 */
#define     CQ_TAR_END                          2       /* 0 - 15 */
#define     CQ_TAR_NVPG                         3       /* 0 - 15 */
#define     CQ_TAR_NVC                          5       /* 0 - 15 */
#define     CQ_TAR_ENTRY_SELECT                 PPC_BITMASK(28, 31)

/* Table Data Register */
#define X_CQ_TDR                                0x0F
#define CQ_TDR                                  0x078
/* for the NVPG, NVC, ESB, END Set Translation Tables */
#define     CQ_TDR_VALID                        PPC_BIT(0)
#define     CQ_TDR_BLOCK_ID                     PPC_BITMASK(60, 63)

/*
 * Processor Cores Enabled for MsgSnd
 * Identifies which of the 32 possible core chiplets are enabled and
 * available to receive the MsgSnd command
 */
#define X_CQ_MSGSND                             0x10
#define CQ_MSGSND                               0x080

/* Interrupt Unit Reset Control */
#define X_CQ_RST_CTL                            0x12
#define CQ_RST_CTL                              0x090
#define     CQ_RST_SYNC_RESET                   PPC_BIT(0)      /* Write Only */
#define     CQ_RST_QUIESCE_PB                   PPC_BIT(1)      /* RW */
#define     CQ_RST_MASTER_IDLE                  PPC_BIT(2)      /* Read Only */
#define     CQ_RST_SAVE_IDLE                    PPC_BIT(3)      /* Read Only */
#define     CQ_RST_PB_BAR_RESET                 PPC_BIT(4)      /* Write Only */

/* PowerBus General Configuration */
#define X_CQ_CFG_PB_GEN                         0x14
#define CQ_CFG_PB_GEN                           0x0A0
#define    CQ_CFG_PB_GEN_PB_INIT                PPC_BIT(45)

/*
 * FIR
 *     (And-Mask)
 *     (Or-Mask)
 */
#define X_CQ_FIR                                0x30
#define X_CQ_FIR_AND                            0x31
#define X_CQ_FIR_OR                             0x32
#define CQ_FIR                                  0x180
#define CQ_FIR_AND                              0x188
#define CQ_FIR_OR                               0x190
#define  CQ_FIR_PB_RCMDX_CI_ERR1                PPC_BIT(19)
#define  CQ_FIR_VC_INFO_ERROR_0_2               PPC_BITMASK(61, 63)

/*
 * FIR Mask
 *     (And-Mask)
 *     (Or-Mask)
 */
#define X_CQ_FIRMASK                            0x33
#define X_CQ_FIRMASK_AND                        0x34
#define X_CQ_FIRMASK_OR                         0x35
#define CQ_FIRMASK                              0x198
#define CQ_FIRMASK_AND                          0x1A0
#define CQ_FIRMASK_OR                           0x1A8

/*
 * VC0
 */

/* VSD table address */
#define X_VC_VSD_TABLE_ADDR                     0x100
#define VC_VSD_TABLE_ADDR                       0x000
#define   VC_VSD_TABLE_AUTOINC                  PPC_BIT(0)
#define   VC_VSD_TABLE_SELECT                   PPC_BITMASK(12, 15)
#define   VC_VSD_TABLE_ADDRESS                  PPC_BITMASK(28, 31)

/* VSD table data */
#define X_VC_VSD_TABLE_DATA                     0x101
#define VC_VSD_TABLE_DATA                       0x008

/* AIB AT macro indirect kill */
#define X_VC_AT_MACRO_KILL                      0x102
#define VC_AT_MACRO_KILL                        0x010
#define  VC_AT_MACRO_KILL_VALID                 PPC_BIT(0)
#define  VC_AT_MACRO_KILL_VSD                   PPC_BITMASK(12, 15)
#define  VC_AT_MACRO_KILL_BLOCK_ID              PPC_BITMASK(28, 31)
#define  VC_AT_MACRO_KILL_OFFSET                PPC_BITMASK(48, 60)

/* AIB AT macro indirect kill mask (same bit definitions) */
#define X_VC_AT_MACRO_KILL_MASK                 0x103
#define VC_AT_MACRO_KILL_MASK                   0x018

/* Remote IRQs and ERQs configuration [n] (n = 0:6) */
#define X_VC_QUEUES_CFG_REM0                    0x117

#define VC_QUEUES_CFG_REM0                      0x0B8
#define VC_QUEUES_CFG_REM1                      0x0C0
#define VC_QUEUES_CFG_REM2                      0x0C8
#define VC_QUEUES_CFG_REM3                      0x0D0
#define VC_QUEUES_CFG_REM4                      0x0D8
#define VC_QUEUES_CFG_REM5                      0x0E0
#define VC_QUEUES_CFG_REM6                      0x0E8
#define  VC_QUEUES_CFG_MEMB_EN                  PPC_BIT(38)
#define  VC_QUEUES_CFG_MEMB_SZ                  PPC_BITMASK(42, 47)

/*
 * VC1
 */

/* ESBC cache flush control trigger */
#define X_VC_ESBC_FLUSH_CTRL                    0x140
#define VC_ESBC_FLUSH_CTRL                      0x200
#define  VC_ESBC_FLUSH_CTRL_POLL_VALID          PPC_BIT(0)
#define  VC_ESBC_FLUSH_CTRL_WANT_CACHE_DISABLE  PPC_BIT(2)

/* ESBC cache flush poll trigger */
#define X_VC_ESBC_FLUSH_POLL                    0x141
#define VC_ESBC_FLUSH_POLL                      0x208
#define  VC_ESBC_FLUSH_POLL_BLOCK_ID            PPC_BITMASK(0, 3)
#define  VC_ESBC_FLUSH_POLL_OFFSET              PPC_BITMASK(4, 31)  /* 28-bit */
#define  VC_ESBC_FLUSH_POLL_BLOCK_ID_MASK       PPC_BITMASK(32, 35)
#define  VC_ESBC_FLUSH_POLL_OFFSET_MASK         PPC_BITMASK(36, 63) /* 28-bit */

/* ESBC configuration */
#define X_VC_ESBC_CFG                           0x148
#define VC_ESBC_CFG                             0x240

/* EASC flush control register */
#define X_VC_EASC_FLUSH_CTRL                    0x160
#define VC_EASC_FLUSH_CTRL                      0x300
#define  VC_EASC_FLUSH_CTRL_POLL_VALID          PPC_BIT(0)
#define  VC_EASC_FLUSH_CTRL_WANT_CACHE_DISABLE  PPC_BIT(2)

/* EASC flush poll register */
#define X_VC_EASC_FLUSH_POLL                    0x161
#define VC_EASC_FLUSH_POLL                      0x308
#define  VC_EASC_FLUSH_POLL_BLOCK_ID            PPC_BITMASK(0, 3)
#define  VC_EASC_FLUSH_POLL_OFFSET              PPC_BITMASK(4, 31)  /* 28-bit */
#define  VC_EASC_FLUSH_POLL_BLOCK_ID_MASK       PPC_BITMASK(32, 35)
#define  VC_EASC_FLUSH_POLL_OFFSET_MASK         PPC_BITMASK(36, 63) /* 28-bit */

/*
 * VC2
 */

/* ENDC flush control register */
#define X_VC_ENDC_FLUSH_CTRL                    0x180
#define VC_ENDC_FLUSH_CTRL                      0x400
#define  VC_ENDC_FLUSH_CTRL_POLL_VALID          PPC_BIT(0)
#define  VC_ENDC_FLUSH_CTRL_WANT_CACHE_DISABLE  PPC_BIT(2)
#define  VC_ENDC_FLUSH_CTRL_WANT_INVALIDATE     PPC_BIT(3)
#define  VC_ENDC_FLUSH_CTRL_INJECT_INVALIDATE   PPC_BIT(7)

/* ENDC flush poll register */
#define X_VC_ENDC_FLUSH_POLL                    0x181
#define VC_ENDC_FLUSH_POLL                      0x408
#define  VC_ENDC_FLUSH_POLL_BLOCK_ID            PPC_BITMASK(4, 7)
#define  VC_ENDC_FLUSH_POLL_OFFSET              PPC_BITMASK(8, 31)  /* 24-bit */
#define  VC_ENDC_FLUSH_POLL_BLOCK_ID_MASK       PPC_BITMASK(36, 39)
#define  VC_ENDC_FLUSH_POLL_OFFSET_MASK         PPC_BITMASK(40, 63) /* 24-bit */

/* ENDC Sync done */
#define X_VC_ENDC_SYNC_DONE                     0x184
#define VC_ENDC_SYNC_DONE                       0x420
#define   VC_ENDC_SYNC_POLL_DONE                PPC_BITMASK(0, 6)
#define   VC_ENDC_SYNC_QUEUE_IPI                PPC_BIT(0)
#define   VC_ENDC_SYNC_QUEUE_HWD                PPC_BIT(1)
#define   VC_ENDC_SYNC_QUEUE_NXC                PPC_BIT(2)
#define   VC_ENDC_SYNC_QUEUE_INT                PPC_BIT(3)
#define   VC_ENDC_SYNC_QUEUE_OS                 PPC_BIT(4)
#define   VC_ENDC_SYNC_QUEUE_POOL               PPC_BIT(5)
#define   VC_ENDC_SYNC_QUEUE_HARD               PPC_BIT(6)
#define   VC_QUEUE_COUNT                        7

/* ENDC cache watch specification 0  */
#define X_VC_ENDC_WATCH0_SPEC                   0x1A0
#define VC_ENDC_WATCH0_SPEC                     0x500
#define   VC_ENDC_WATCH_CONFLICT                PPC_BIT(0)
#define   VC_ENDC_WATCH_FULL                    PPC_BIT(8)
#define   VC_ENDC_WATCH_BLOCK_ID                PPC_BITMASK(28, 31)
#define   VC_ENDC_WATCH_INDEX                   PPC_BITMASK(40, 63)

/* ENDC cache watch data 0 */
#define X_VC_ENDC_WATCH0_DATA0                  0x1A4
#define X_VC_ENDC_WATCH0_DATA1                  0x1A5
#define X_VC_ENDC_WATCH0_DATA2                  0x1A6
#define X_VC_ENDC_WATCH0_DATA3                  0x1A7

#define VC_ENDC_WATCH0_DATA0                    0x520
#define VC_ENDC_WATCH0_DATA1                    0x528
#define VC_ENDC_WATCH0_DATA2                    0x530
#define VC_ENDC_WATCH0_DATA3                    0x538

/*
 * PC LSB1
 */

/* VSD table address register */
#define X_PC_VSD_TABLE_ADDR                     0x200
#define PC_VSD_TABLE_ADDR                       0x000
#define   PC_VSD_TABLE_AUTOINC                  PPC_BIT(0)
#define   PC_VSD_TABLE_SELECT                   PPC_BITMASK(12, 15)
#define   PC_VSD_TABLE_ADDRESS                  PPC_BITMASK(28, 31)

/* VSD table data register */
#define X_PC_VSD_TABLE_DATA                     0x201
#define PC_VSD_TABLE_DATA                       0x008

/* AT indirect kill register */
#define X_PC_AT_KILL                            0x202
#define PC_AT_KILL                              0x010
#define     PC_AT_KILL_VALID                    PPC_BIT(0)
#define     PC_AT_KILL_VSD_TYPE                 PPC_BITMASK(24, 27)
/* Only NVP, NVG, NVC */
#define     PC_AT_KILL_BLOCK_ID                 PPC_BITMASK(28, 31)
#define     PC_AT_KILL_OFFSET                   PPC_BITMASK(48, 60)

/* AT indirect kill mask register */
#define X_PC_AT_KILL_MASK                       0x203
#define PC_AT_KILL_MASK                         0x018
#define     PC_AT_KILL_MASK_VSD_TYPE            PPC_BITMASK(24, 27)
#define     PC_AT_KILL_MASK_BLOCK_ID            PPC_BITMASK(28, 31)
#define     PC_AT_KILL_MASK_OFFSET              PPC_BITMASK(48, 60)

/*
 * PC LSB2
 */

/* NxC Cache flush control */
#define X_PC_NXC_FLUSH_CTRL                     0x280
#define PC_NXC_FLUSH_CTRL                       0x400
#define  PC_NXC_FLUSH_CTRL_POLL_VALID           PPC_BIT(0)
#define  PC_NXC_FLUSH_CTRL_WANT_CACHE_DISABLE   PPC_BIT(2)
#define  PC_NXC_FLUSH_CTRL_WANT_INVALIDATE      PPC_BIT(3)
#define  PC_NXC_FLUSH_CTRL_INJECT_INVALIDATE    PPC_BIT(7)

/* NxC Cache flush poll */
#define X_PC_NXC_FLUSH_POLL                     0x281
#define PC_NXC_FLUSH_POLL                       0x408
#define  PC_NXC_FLUSH_POLL_NXC_TYPE             PPC_BITMASK(2, 3)
#define    PC_NXC_FLUSH_POLL_NXC_TYPE_NVP       0
#define    PC_NXC_FLUSH_POLL_NXC_TYPE_NVG       2
#define    PC_NXC_FLUSH_POLL_NXC_TYPE_NVC       3
#define  PC_NXC_FLUSH_POLL_BLOCK_ID             PPC_BITMASK(4, 7)
#define  PC_NXC_FLUSH_POLL_OFFSET               PPC_BITMASK(8, 31)  /* 24-bit */
#define  PC_NXC_FLUSH_POLL_NXC_TYPE_MASK        PPC_BITMASK(34, 35) /* 0: Ign */
#define  PC_NXC_FLUSH_POLL_BLOCK_ID_MASK        PPC_BITMASK(36, 39)
#define  PC_NXC_FLUSH_POLL_OFFSET_MASK          PPC_BITMASK(40, 63) /* 24-bit */

/* NxC Cache Watch 0 Specification */
#define X_PC_NXC_WATCH0_SPEC                    0x2A0
#define PC_NXC_WATCH0_SPEC                      0x500
#define   PC_NXC_WATCH_CONFLICT                 PPC_BIT(0)
#define   PC_NXC_WATCH_FULL                     PPC_BIT(8)
#define   PC_NXC_WATCH_NXC_TYPE                 PPC_BITMASK(26, 27)
#define     PC_NXC_WATCH_NXC_NVP                0
#define     PC_NXC_WATCH_NXC_NVG                2
#define     PC_NXC_WATCH_NXC_NVC                3
#define   PC_NXC_WATCH_BLOCK_ID                 PPC_BITMASK(28, 31)
#define   PC_NXC_WATCH_INDEX                    PPC_BITMASK(40, 63)

/* NxC Cache Watch 0 Data */
#define X_PC_NXC_WATCH0_DATA0                   0x2A4
#define X_PC_NXC_WATCH0_DATA1                   0x2A5
#define X_PC_NXC_WATCH0_DATA2                   0x2A6
#define X_PC_NXC_WATCH0_DATA3                   0x2A7

#define PC_NXC_WATCH0_DATA0                     0x520
#define PC_NXC_WATCH0_DATA1                     0x528
#define PC_NXC_WATCH0_DATA2                     0x530
#define PC_NXC_WATCH0_DATA3                     0x538

/*
 * TCTXT Registers
 */

/* Physical Thread Enable0 register */
#define X_TCTXT_EN0                             0x300
#define TCTXT_EN0                               0x000

/* Physical Thread Enable0 Set register */
#define X_TCTXT_EN0_SET                         0x302
#define TCTXT_EN0_SET                           0x010

/* Physical Thread Enable0 Reset register */
#define X_TCTXT_EN0_RESET                       0x303
#define TCTXT_EN0_RESET                         0x018

/* Physical Thread Enable1 register */
#define X_TCTXT_EN1                             0x304
#define TCTXT_EN1                               0x020

/* Physical Thread Enable1 Set register */
#define X_TCTXT_EN1_SET                         0x306
#define TCTXT_EN1_SET                           0x030

/* Physical Thread Enable1 Reset register */
#define X_TCTXT_EN1_RESET                       0x307
#define TCTXT_EN1_RESET                         0x038

/* TCTXT Config register */
#define X_TCTXT_CFG                             0x328
#define TCTXT_CFG                               0x140

/*
 * VSD Tables
 */
#define VST_ESB                  0
#define VST_EAS                  1 /* No used by PC */
#define VST_END                  2
#define VST_NVP                  3
#define VST_NVG                  4
#define VST_NVC                  5
#define VST_IC                   6 /* No used by PC */
#define VST_SYNC                 7
#define VST_ERQ                  8 /* No used by PC */

/*
 * Bits in a VSD entry.
 *
 * Note: the address is naturally aligned, we don't use a PPC_BITMASK,
 *       but just a mask to apply to the address before OR'ing it in.
 *
 * Note: VSD_FIRMWARE is a SW bit ! It hijacks an unused bit in the
 *       VSD and is only meant to be used in indirect mode !
 */
#define VSD_MODE                PPC_BITMASK(0, 1)
#define  VSD_MODE_SHARED        1
#define  VSD_MODE_EXCLUSIVE     2
#define  VSD_MODE_FORWARD       3
#define VSD_FIRMWARE            PPC_BIT(2) /* Read warning */
#define VSD_FIRMWARE2           PPC_BIT(3) /* unused */
#define VSD_RESERVED            PPC_BITMASK(4, 7) /* P10 reserved */
#define VSD_ADDRESS_MASK        0x00fffffffffff000ull
#define VSD_MIGRATION_REG       PPC_BITMASK(52, 55)
#define VSD_INDIRECT            PPC_BIT(56)
#define VSD_TSIZE               PPC_BITMASK(59, 63)

#endif /* PPC_PNV_XIVE2_REGS_H */
