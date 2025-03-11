/*
 * QEMU PowerPC SPI model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model Supports a connection to a single SPI responder.
 * Introduced for P10 to provide access to SPI seeproms, TPM, flash device
 * and an ADC controller.
 *
 * All SPI function control is mapped into the SPI register space to enable
 * full control by firmware.
 *
 * SPI Controller has sequencer and shift engine. The SPI shift engine
 * performs serialization and de-serialization according to the control by
 * the sequencer and according to the setup defined in the configuration
 * registers and the SPI sequencer implements the main control logic.
 */

#ifndef PPC_PNV_SPI_H
#define PPC_PNV_SPI_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"
#include "qemu/fifo8.h"

#define TYPE_PNV_SPI "pnv-spi"
OBJECT_DECLARE_SIMPLE_TYPE(PnvSpi, PNV_SPI)

#define PNV_SPI_REG_SIZE 8
#define PNV_SPI_REGS 7

#define TYPE_PNV_SPI_BUS "spi"
typedef struct PnvSpi {
    SysBusDevice parent_obj;

    SSIBus *ssi_bus;
    qemu_irq *cs_line;
    MemoryRegion    xscom_spic_regs;
    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
    uint8_t fail_count; /* RDR Match failure counter */
    /* SPI object number */
    uint32_t        spic_num;
    uint32_t        chip_id;
    uint8_t         transfer_len;
    uint8_t         responder_select;
    /* To verify if shift_n1 happens prior to shift_n2 */
    bool            shift_n1_done;
    /* Loop counter for branch operation opcode Ex/Fx */
    uint8_t         loop_counter_1;
    uint8_t         loop_counter_2;
    /* N1/N2_bits specifies the size of the N1/N2 segment of a frame in bits.*/
    uint8_t         N1_bits;
    uint8_t         N2_bits;
    /* Number of bytes in a payload for the N1/N2 frame segment.*/
    uint8_t         N1_bytes;
    uint8_t         N2_bytes;
    /* Number of N1/N2 bytes marked for transmit */
    uint8_t         N1_tx;
    uint8_t         N2_tx;
    /* Number of N1/N2 bytes marked for receive */
    uint8_t         N1_rx;
    uint8_t         N2_rx;

    /* SPI registers */
    uint64_t        regs[PNV_SPI_REGS];
    uint8_t         seq_op[PNV_SPI_REG_SIZE];
    uint64_t        status;
} PnvSpi;
#endif /* PPC_PNV_SPI_H */
