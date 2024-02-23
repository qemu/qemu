/*
 * PowerNV I2C Controller Register Definitions
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_I2C_REGS_H
#define PNV_I2C_REGS_H

/* I2C FIFO register */
#define I2C_FIFO_REG                    0x4
#define I2C_FIFO                        PPC_BITMASK(0, 7)

/* I2C command register */
#define I2C_CMD_REG                     0x5
#define I2C_CMD_WITH_START              PPC_BIT(0)
#define I2C_CMD_WITH_ADDR               PPC_BIT(1)
#define I2C_CMD_READ_CONT               PPC_BIT(2)
#define I2C_CMD_WITH_STOP               PPC_BIT(3)
#define I2C_CMD_INTR_STEERING           PPC_BITMASK(6, 7) /* P9 */
#define   I2C_CMD_INTR_STEER_HOST       1
#define   I2C_CMD_INTR_STEER_OCC        2
#define I2C_CMD_DEV_ADDR                PPC_BITMASK(8, 14)
#define I2C_CMD_READ_NOT_WRITE          PPC_BIT(15)
#define I2C_CMD_LEN_BYTES               PPC_BITMASK(16, 31)
#define I2C_MAX_TFR_LEN                 0xfff0ull

/* I2C mode register */
#define I2C_MODE_REG                    0x6
#define I2C_MODE_BIT_RATE_DIV           PPC_BITMASK(0, 15)
#define I2C_MODE_PORT_NUM               PPC_BITMASK(16, 21)
#define I2C_MODE_ENHANCED               PPC_BIT(28)
#define I2C_MODE_DIAGNOSTIC             PPC_BIT(29)
#define I2C_MODE_PACING_ALLOW           PPC_BIT(30)
#define I2C_MODE_WRAP                   PPC_BIT(31)

/* I2C watermark register */
#define I2C_WATERMARK_REG               0x7
#define I2C_WATERMARK_HIGH              PPC_BITMASK(16, 19)
#define I2C_WATERMARK_LOW               PPC_BITMASK(24, 27)

/*
 * I2C interrupt mask and condition registers
 *
 * NB: The function of 0x9 and 0xa changes depending on whether you're reading
 *     or writing to them. When read they return the interrupt condition bits
 *     and on writes they update the interrupt mask register.
 *
 *  The bit definitions are the same for all the interrupt registers.
 */
#define I2C_INTR_MASK_REG               0x8

#define I2C_INTR_RAW_COND_REG           0x9 /* read */
#define I2C_INTR_MASK_OR_REG            0x9 /* write*/

#define I2C_INTR_COND_REG               0xa /* read */
#define I2C_INTR_MASK_AND_REG           0xa /* write */

#define I2C_INTR_ALL                    PPC_BITMASK(16, 31)
#define I2C_INTR_INVALID_CMD            PPC_BIT(16)
#define I2C_INTR_LBUS_PARITY_ERR        PPC_BIT(17)
#define I2C_INTR_BKEND_OVERRUN_ERR      PPC_BIT(18)
#define I2C_INTR_BKEND_ACCESS_ERR       PPC_BIT(19)
#define I2C_INTR_ARBT_LOST_ERR          PPC_BIT(20)
#define I2C_INTR_NACK_RCVD_ERR          PPC_BIT(21)
#define I2C_INTR_DATA_REQ               PPC_BIT(22)
#define I2C_INTR_CMD_COMP               PPC_BIT(23)
#define I2C_INTR_STOP_ERR               PPC_BIT(24)
#define I2C_INTR_I2C_BUSY               PPC_BIT(25)
#define I2C_INTR_NOT_I2C_BUSY           PPC_BIT(26)
#define I2C_INTR_SCL_EQ_1               PPC_BIT(28)
#define I2C_INTR_SCL_EQ_0               PPC_BIT(29)
#define I2C_INTR_SDA_EQ_1               PPC_BIT(30)
#define I2C_INTR_SDA_EQ_0               PPC_BIT(31)

/* I2C status register */
#define I2C_RESET_I2C_REG               0xb /* write */
#define I2C_RESET_ERRORS                0xc
#define I2C_STAT_REG                    0xb /* read */
#define I2C_STAT_INVALID_CMD            PPC_BIT(0)
#define I2C_STAT_LBUS_PARITY_ERR        PPC_BIT(1)
#define I2C_STAT_BKEND_OVERRUN_ERR      PPC_BIT(2)
#define I2C_STAT_BKEND_ACCESS_ERR       PPC_BIT(3)
#define I2C_STAT_ARBT_LOST_ERR          PPC_BIT(4)
#define I2C_STAT_NACK_RCVD_ERR          PPC_BIT(5)
#define I2C_STAT_DATA_REQ               PPC_BIT(6)
#define I2C_STAT_CMD_COMP               PPC_BIT(7)
#define I2C_STAT_STOP_ERR               PPC_BIT(8)
#define I2C_STAT_UPPER_THRS             PPC_BITMASK(9, 15)
#define I2C_STAT_ANY_I2C_INTR           PPC_BIT(16)
#define I2C_STAT_PORT_HISTORY_BUSY      PPC_BIT(19)
#define I2C_STAT_SCL_INPUT_LEVEL        PPC_BIT(20)
#define I2C_STAT_SDA_INPUT_LEVEL        PPC_BIT(21)
#define I2C_STAT_PORT_BUSY              PPC_BIT(22)
#define I2C_STAT_INTERFACE_BUSY         PPC_BIT(23)
#define I2C_STAT_FIFO_ENTRY_COUNT       PPC_BITMASK(24, 31)

#define I2C_STAT_ANY_ERR (I2C_STAT_INVALID_CMD | I2C_STAT_LBUS_PARITY_ERR | \
                          I2C_STAT_BKEND_OVERRUN_ERR | \
                          I2C_STAT_BKEND_ACCESS_ERR | I2C_STAT_ARBT_LOST_ERR | \
                          I2C_STAT_NACK_RCVD_ERR | I2C_STAT_STOP_ERR)


#define I2C_INTR_ACTIVE \
        ((I2C_STAT_ANY_ERR >> 16) | I2C_INTR_CMD_COMP | I2C_INTR_DATA_REQ)

/* Pseudo-status used for timeouts */
#define I2C_STAT_PSEUDO_TIMEOUT         PPC_BIT(63)

/* I2C extended status register */
#define I2C_EXTD_STAT_REG               0xc
#define I2C_EXTD_STAT_FIFO_SIZE         PPC_BITMASK(0, 7)
#define I2C_EXTD_STAT_MSM_CURSTATE      PPC_BITMASK(11, 15)
#define I2C_EXTD_STAT_SCL_IN_SYNC       PPC_BIT(16)
#define I2C_EXTD_STAT_SDA_IN_SYNC       PPC_BIT(17)
#define I2C_EXTD_STAT_S_SCL             PPC_BIT(18)
#define I2C_EXTD_STAT_S_SDA             PPC_BIT(19)
#define I2C_EXTD_STAT_M_SCL             PPC_BIT(20)
#define I2C_EXTD_STAT_M_SDA             PPC_BIT(21)
#define I2C_EXTD_STAT_HIGH_WATER        PPC_BIT(22)
#define I2C_EXTD_STAT_LOW_WATER         PPC_BIT(23)
#define I2C_EXTD_STAT_I2C_BUSY          PPC_BIT(24)
#define I2C_EXTD_STAT_SELF_BUSY         PPC_BIT(25)
#define I2C_EXTD_STAT_I2C_VERSION       PPC_BITMASK(27, 31)

/* I2C residual front end/back end length */
#define I2C_RESIDUAL_LEN_REG            0xd
#define I2C_RESIDUAL_FRONT_END          PPC_BITMASK(0, 15)
#define I2C_RESIDUAL_BACK_END           PPC_BITMASK(16, 31)

/* Port busy register */
#define I2C_PORT_BUSY_REG               0xe
#define I2C_SET_S_SCL_REG               0xd
#define I2C_RESET_S_SCL_REG             0xf
#define I2C_SET_S_SDA_REG               0x10
#define I2C_RESET_S_SDA_REG             0x11

#define PNV_I2C_FIFO_SIZE 8
#define PNV_I2C_MAX_BUSSES 64

#endif /* PNV_I2C_REGS_H */
