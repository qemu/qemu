/*
 * QEMU PowerPC SPI model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_SPI_CONTROLLER_REGS_H
#define PNV_SPI_CONTROLLER_REGS_H

/*
 * Macros from target/ppc/cpu.h
 * These macros are copied from ppc target specific file target/ppc/cpu.h
 * as target/ppc/cpu.h cannot be included here.
 */
#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BIT8(bit)           (0x80 >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK8(bs, be)    ((PPC_BIT8(bs) - PPC_BIT8(be)) | PPC_BIT8(bs))
#define MASK_TO_LSH(m)          (__builtin_ffsll(m) - 1)
#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val) \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

/* Error Register */
#define ERROR_REG               0x00

/* counter_config_reg */
#define SPI_CTR_CFG_REG         0x01

/* config_reg */
#define CONFIG_REG1             0x02

/* clock_config_reset_control_ecc_enable_reg */
#define SPI_CLK_CFG_REG         0x03
#define SPI_CLK_CFG_HARD_RST    0x0084000000000000;
#define SPI_CLK_CFG_RST_CTRL    PPC_BITMASK(24, 27)

/* memory_mapping_reg */
#define SPI_MM_REG              0x04

/* transmit_data_reg */
#define SPI_XMIT_DATA_REG       0x05

/* receive_data_reg */
#define SPI_RCV_DATA_REG        0x06

/* sequencer_operation_reg */
#define SPI_SEQ_OP_REG          0x07

/* status_reg */
#define SPI_STS_REG             0x08
#define SPI_STS_RDR_FULL        PPC_BIT(0)
#define SPI_STS_RDR_OVERRUN     PPC_BIT(1)
#define SPI_STS_RDR_UNDERRUN    PPC_BIT(2)
#define SPI_STS_TDR_FULL        PPC_BIT(4)
#define SPI_STS_TDR_OVERRUN     PPC_BIT(5)
#define SPI_STS_TDR_UNDERRUN    PPC_BIT(6)
#define SPI_STS_SEQ_FSM         PPC_BITMASK(8, 15)
#define SPI_STS_SHIFTER_FSM     PPC_BITMASK(16, 27)
#define SPI_STS_SEQ_INDEX       PPC_BITMASK(28, 31)
#define SPI_STS_GEN_STATUS      PPC_BITMASK(32, 63)
#define SPI_STS_RDR             PPC_BITMASK(1, 3)
#define SPI_STS_TDR             PPC_BITMASK(5, 7)

#endif
