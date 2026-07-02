/*
 * Field bitmasks and register structs definitions for FlexCAN
 *
 * This implementation is based on the following datasheet:
 * i.MX 6Dual/6Quad Applications Processor Reference Manual
 * Document Number: IMX6DQRM, Rev. 6, 05/2020
 *
 * Copyright (c) 2025 Matyas Bobek <matyas.bobek@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/bitops.h"

#ifndef HW_CAN_FLEXCAN_REGS_H
#define HW_CAN_FLEXCAN_REGS_H

#define FLEXCAN_GENMASK(h, l) (((~(uint32_t)0) >> (31 - (h) + (l))) << (l))

/**
 * The following macros were originally written for the Linux kernel by:
 *   Andrey Volkov <andrey@volkov.fr>
 *   Sascha Hauer <s.hauer@pengutronix.de>
 *   Marc Kleine-Budde <mkl@pengutronix.de>
 *   David Jander <david@protonic.nl>
 * and they have agreed to license them under GPL-2.0-or-later.
 */

/* FLEXCAN module configuration register (CANMCR) bits */
#define FLEXCAN_MCR_MDIS                BIT(31)
#define FLEXCAN_MCR_FRZ                 BIT(30)
#define FLEXCAN_MCR_FEN                 BIT(29)
#define FLEXCAN_MCR_HALT                BIT(28)
#define FLEXCAN_MCR_NOT_RDY             BIT(27)
#define FLEXCAN_MCR_WAK_MSK             BIT(26)
#define FLEXCAN_MCR_SOFTRST             BIT(25)
#define FLEXCAN_MCR_FRZ_ACK             BIT(24)
#define FLEXCAN_MCR_SUPV                BIT(23)
#define FLEXCAN_MCR_SLF_WAK             BIT(22)
#define FLEXCAN_MCR_WRN_EN              BIT(21)
#define FLEXCAN_MCR_LPM_ACK             BIT(20)
#define FLEXCAN_MCR_WAK_SRC             BIT(19)
#define FLEXCAN_MCR_DOZE                BIT(18)
#define FLEXCAN_MCR_SRX_DIS             BIT(17)
#define FLEXCAN_MCR_IRMQ                BIT(16)
#define FLEXCAN_MCR_LPRIO_EN            BIT(13)
#define FLEXCAN_MCR_AEN                 BIT(12)
#define FLEXCAN_MCR_FDEN                BIT(11)
#define FLEXCAN_MCR_MAXMB(x)            ((x) & 0x7f)
#define FLEXCAN_MCR_IDAM_A              (0x0 << 8)
#define FLEXCAN_MCR_IDAM_B              (0x1 << 8)
#define FLEXCAN_MCR_IDAM_C              (0x2 << 8)
#define FLEXCAN_MCR_IDAM_D              (0x3 << 8)
#define FLEXCAN_MCR_IDAM_MASK           (0x3 << 8)

/* FLEXCAN control register (CANCTRL) bits */
#define FLEXCAN_CTRL_PRESDIV(x)         (((x) & 0xFF) << 24)
#define FLEXCAN_CTRL_PRESDIV_MASK       FLEXCAN_CTRL_PRESDIV(UINT32_MAX)
#define FLEXCAN_CTRL_RJW(x)             (((x) & 0x03) << 22)
#define FLEXCAN_CTRL_RJW_MASK           FLEXCAN_CTRL_RJW(UINT32_MAX)
#define FLEXCAN_CTRL_PSEG1(x)           (((x) & 0x07) << 19)
#define FLEXCAN_CTRL_PSEG1_MASK         FLEXCAN_CTRL_PSEG1(UINT32_MAX)
#define FLEXCAN_CTRL_PSEG2(x)           (((x) & 0x07) << 16)
#define FLEXCAN_CTRL_PSEG2_MASK         FLEXCAN_CTRL_PSEG2(UINT32_MAX)
#define FLEXCAN_CTRL_BOFF_MSK           BIT(15)
#define FLEXCAN_CTRL_ERR_MSK            BIT(14)
#define FLEXCAN_CTRL_CLK_SRC            BIT(13)
#define FLEXCAN_CTRL_LPB                BIT(12)
#define FLEXCAN_CTRL_TWRN_MSK           BIT(11)
#define FLEXCAN_CTRL_RWRN_MSK           BIT(10)
#define FLEXCAN_CTRL_SMP                BIT(7)
#define FLEXCAN_CTRL_BOFF_REC           BIT(6)
#define FLEXCAN_CTRL_TSYN               BIT(5)
#define FLEXCAN_CTRL_LBUF               BIT(4)
#define FLEXCAN_CTRL_LOM                BIT(3)
#define FLEXCAN_CTRL_PROPSEG(x)         ((x) & 0x07)
#define FLEXCAN_CTRL_PROPSEG_MASK       FLEXCAN_CTRL_PROPSEG(UINT32_MAX)
#define FLEXCAN_CTRL_ERR_BUS            (FLEXCAN_CTRL_ERR_MSK)
#define FLEXCAN_CTRL_ERR_STATE \
        (FLEXCAN_CTRL_TWRN_MSK | FLEXCAN_CTRL_RWRN_MSK | \
         FLEXCAN_CTRL_BOFF_MSK)
#define FLEXCAN_CTRL_ERR_ALL \
        (FLEXCAN_CTRL_ERR_BUS | FLEXCAN_CTRL_ERR_STATE)

/* FLEXCAN control register 2 (CTRL2) bits */
#define FLEXCAN_CTRL2_ECRWRE            BIT(29)
#define FLEXCAN_CTRL2_WRMFRZ            BIT(28)
#define FLEXCAN_CTRL2_RFFN(x)           (((x) & 0x0f) << 24)
#define FLEXCAN_CTRL2_TASD(x)           (((x) & 0x1f) << 19)
#define FLEXCAN_CTRL2_MRP               BIT(18)
#define FLEXCAN_CTRL2_RRS               BIT(17)
#define FLEXCAN_CTRL2_EACEN             BIT(16)
#define FLEXCAN_CTRL2_ISOCANFDEN        BIT(12)

/* FLEXCAN memory error control register (MECR) bits */
#define FLEXCAN_MECR_ECRWRDIS           BIT(31)
#define FLEXCAN_MECR_HANCEI_MSK         BIT(19)
#define FLEXCAN_MECR_FANCEI_MSK         BIT(18)
#define FLEXCAN_MECR_CEI_MSK            BIT(16)
#define FLEXCAN_MECR_HAERRIE            BIT(15)
#define FLEXCAN_MECR_FAERRIE            BIT(14)
#define FLEXCAN_MECR_EXTERRIE           BIT(13)
#define FLEXCAN_MECR_RERRDIS            BIT(9)
#define FLEXCAN_MECR_ECCDIS             BIT(8)
#define FLEXCAN_MECR_NCEFAFRZ           BIT(7)

/* FLEXCAN error and status register (ESR) bits */
#define FLEXCAN_ESR_SYNCH               BIT(18)
#define FLEXCAN_ESR_TWRN_INT            BIT(17)
#define FLEXCAN_ESR_RWRN_INT            BIT(16)
#define FLEXCAN_ESR_BIT1_ERR            BIT(15)
#define FLEXCAN_ESR_BIT0_ERR            BIT(14)
#define FLEXCAN_ESR_ACK_ERR             BIT(13)
#define FLEXCAN_ESR_CRC_ERR             BIT(12)
#define FLEXCAN_ESR_FRM_ERR             BIT(11)
#define FLEXCAN_ESR_STF_ERR             BIT(10)
#define FLEXCAN_ESR_TX_WRN              BIT(9)
#define FLEXCAN_ESR_RX_WRN              BIT(8)
#define FLEXCAN_ESR_IDLE                BIT(7)
#define FLEXCAN_ESR_BOFF_INT            BIT(2)
#define FLEXCAN_ESR_ERR_INT             BIT(1)
#define FLEXCAN_ESR_WAK_INT             BIT(0)

/* FLEXCAN Bit Timing register (CBT) bits */
#define FLEXCAN_CBT_BTF                 BIT(31)
#define FLEXCAN_CBT_EPRESDIV_MASK       FLEXCAN_GENMASK(30, 21)
#define FLEXCAN_CBT_ERJW_MASK           FLEXCAN_GENMASK(20, 16)
#define FLEXCAN_CBT_EPROPSEG_MASK       FLEXCAN_GENMASK(15, 10)
#define FLEXCAN_CBT_EPSEG1_MASK         FLEXCAN_GENMASK(9, 5)
#define FLEXCAN_CBT_EPSEG2_MASK         FLEXCAN_GENMASK(4, 0)

/* FLEXCAN FD control register (FDCTRL) bits */
#define FLEXCAN_FDCTRL_FDRATE           BIT(31)
#define FLEXCAN_FDCTRL_MBDSR1           FLEXCAN_GENMASK(20, 19)
#define FLEXCAN_FDCTRL_MBDSR0           FLEXCAN_GENMASK(17, 16)
#define FLEXCAN_FDCTRL_MBDSR_8          0x0
#define FLEXCAN_FDCTRL_MBDSR_12         0x1
#define FLEXCAN_FDCTRL_MBDSR_32         0x2
#define FLEXCAN_FDCTRL_MBDSR_64         0x3
#define FLEXCAN_FDCTRL_TDCEN            BIT(15)
#define FLEXCAN_FDCTRL_TDCFAIL          BIT(14)
#define FLEXCAN_FDCTRL_TDCOFF           FLEXCAN_GENMASK(12, 8)
#define FLEXCAN_FDCTRL_TDCVAL           FLEXCAN_GENMASK(5, 0)

/* FLEXCAN FD Bit Timing register (FDCBT) bits */
#define FLEXCAN_FDCBT_FPRESDIV_MASK     FLEXCAN_GENMASK(29, 20)
#define FLEXCAN_FDCBT_FRJW_MASK         FLEXCAN_GENMASK(18, 16)
#define FLEXCAN_FDCBT_FPROPSEG_MASK     FLEXCAN_GENMASK(14, 10)
#define FLEXCAN_FDCBT_FPSEG1_MASK       FLEXCAN_GENMASK(7, 5)
#define FLEXCAN_FDCBT_FPSEG2_MASK       FLEXCAN_GENMASK(2, 0)

/* FLEXCAN CRC Register (CRCR) bits */
#define FLEXCAN_CRCR_MBCRC_MASK         FLEXCAN_GENMASK(22, 16)
#define FLEXCAN_CRCR_MBCRC(x)           (((x) & FLEXCAN_CRCR_MBCRC_MASK) << 16)
#define FLEXCAN_CRCR_TXCRC_MASK         FLEXCAN_GENMASK(14, 0)
#define FLEXCAN_CRCR_TXCRC(x)           ((x) & FLEXCAN_CRCR_TXCRC_MASK)

/* FLEXCAN interrupt flag register (IFLAG) bits */
/* Errata ERR005829 step7: Reserve first valid MB */
#define I_FIFO_OVERFLOW  7
#define I_FIFO_WARN      6
#define I_FIFO_AVAILABLE 5

#define FLEXCAN_TX_MB_RESERVED_RX_FIFO  8
#define FLEXCAN_TX_MB_RESERVED_RX_MAILBOX       0
#define FLEXCAN_RX_MB_RX_MAILBOX_FIRST  (FLEXCAN_TX_MB_RESERVED_RX_MAILBOX + 1)
#define FLEXCAN_IFLAG_MB(x)             BIT_ULL(x)
#define FLEXCAN_IFLAG_RX_FIFO_OVERFLOW  BIT(I_FIFO_OVERFLOW)
#define FLEXCAN_IFLAG_RX_FIFO_WARN      BIT(I_FIFO_WARN)
#define FLEXCAN_IFLAG_RX_FIFO_AVAILABLE BIT(I_FIFO_AVAILABLE)

/* FLEXCAN message buffers */
#define FLEXCAN_MB_CODE_RX_BUSY_BIT     (0x1 << 24)
#define FLEXCAN_MB_CODE_RX_INACTIVE     (0x0 << 24)
#define FLEXCAN_MB_CODE_RX_EMPTY        (0x4 << 24)
#define FLEXCAN_MB_CODE_RX_FULL         (0x2 << 24)
#define FLEXCAN_MB_CODE_RX_OVERRUN      (0x6 << 24)
#define FLEXCAN_MB_CODE_RX_RANSWER      (0xa << 24)

#define FLEXCAN_MB_CODE_TX_INACTIVE     (0x8 << 24)
#define FLEXCAN_MB_CODE_TX_ABORT        (0x9 << 24)
#define FLEXCAN_MB_CODE_TX_DATA         (0xc << 24)
#define FLEXCAN_MB_CODE_TX_TANSWER      (0xe << 24)

#define FLEXCAN_MB_CODE(x)              (((x) & 0xF) << 24)
#define FLEXCAN_MB_CODE_MASK            FLEXCAN_MB_CODE(UINT32_MAX)

#define FLEXCAN_MB_CNT_EDL              BIT(31)
#define FLEXCAN_MB_CNT_BRS              BIT(30)
#define FLEXCAN_MB_CNT_ESI              BIT(29)
#define FLEXCAN_MB_CNT_SRR              BIT(22)
#define FLEXCAN_MB_CNT_IDE              BIT(21)
#define FLEXCAN_MB_CNT_RTR              BIT(20)
#define FLEXCAN_MB_CNT_LENGTH(x)        (((x) & 0xF) << 16)
#define FLEXCAN_MB_CNT_TIMESTAMP(x)     ((x) & 0xFFFF)
#define FLEXCAN_MB_CNT_TIMESTAMP_MASK   FLEXCAN_MB_CNT_TIMESTAMP(UINT32_MAX)

#endif
