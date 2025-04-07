/*
 * QEMU PowerPC SPI model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ssi/pnv_spi.h"
#include "hw/ssi/pnv_spi_regs.h"
#include "hw/ssi/ssi.h"
#include <libfdt.h>
#include "hw/irq.h"
#include "trace.h"

#define PNV_SPI_OPCODE_LO_NIBBLE(x) (x & 0x0F)
#define PNV_SPI_MASKED_OPCODE(x) (x & 0xF0)
#define PNV_SPI_FIFO_SIZE 16
#define RDR_MATCH_FAILURE_LIMIT 16

/*
 * Macro from include/hw/ppc/fdt.h
 * fdt.h cannot be included here as it contain ppc target specific dependency.
 */
#define _FDT(exp)                                                  \
    do {                                                           \
        int _ret = (exp);                                          \
        if (_ret < 0) {                                            \
            qemu_log_mask(LOG_GUEST_ERROR,                         \
                    "error creating device tree: %s: %s",          \
                    #exp, fdt_strerror(_ret));                     \
            exit(1);                                               \
        }                                                          \
    } while (0)

static bool does_rdr_match(PnvSpi *s)
{
    /*
     * According to spec, the mask bits that are 0 are compared and the
     * bits that are 1 are ignored.
     */
    uint16_t rdr_match_mask = GETFIELD(SPI_MM_RDR_MATCH_MASK, s->regs[SPI_MM_REG]);
    uint16_t rdr_match_val = GETFIELD(SPI_MM_RDR_MATCH_VAL, s->regs[SPI_MM_REG]);

    if ((~rdr_match_mask & rdr_match_val) == ((~rdr_match_mask) &
            GETFIELD(PPC_BITMASK(48, 63), s->regs[SPI_RCV_DATA_REG]))) {
        return true;
    }
    return false;
}

static uint8_t get_from_offset(PnvSpi *s, uint8_t offset)
{
    uint8_t byte;

    /*
     * Offset is an index between 0 and PNV_SPI_REG_SIZE - 1
     * Check the offset before using it.
     */
    if (offset < PNV_SPI_REG_SIZE) {
        byte = (s->regs[SPI_XMIT_DATA_REG] >> (56 - offset * 8)) & 0xFF;
    } else {
        /*
         * Log an error and return a 0xFF since we have to assign something
         * to byte before returning.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid offset = %d used to get byte "
                      "from TDR\n", offset);
        byte = 0xff;
    }
    return byte;
}

static uint8_t read_from_frame(PnvSpi *s, uint8_t nr_bytes, uint8_t ecc_count,
                uint8_t shift_in_count)
{
    uint8_t byte;
    int count = 0;

    while (count < nr_bytes) {
        shift_in_count++;
        if ((ecc_count != 0) &&
            (shift_in_count == (PNV_SPI_REG_SIZE + ecc_count))) {
            shift_in_count = 0;
        } else if (!fifo8_is_empty(&s->rx_fifo)) {
            byte = fifo8_pop(&s->rx_fifo);
            trace_pnv_spi_shift_rx(byte, count);
            s->regs[SPI_RCV_DATA_REG] = (s->regs[SPI_RCV_DATA_REG] << 8) | byte;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: Reading empty RX_FIFO\n");
        }
        count++;
    } /* end of while */
    return shift_in_count;
}

static void spi_response(PnvSpi *s)
{
    uint8_t ecc_count;
    uint8_t shift_in_count;
    uint32_t rx_len;
    int i;

    /*
     * Processing here must handle:
     * - Which bytes in the payload we should move to the RDR
     * - Explicit mode counter configuration settings
     * - RDR full and RDR overrun status
     */

    /*
     * First check that the response payload is the exact same
     * number of bytes as the request payload was
     */
    rx_len = fifo8_num_used(&s->rx_fifo);
    if (rx_len != (s->N1_bytes + s->N2_bytes)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid response payload size in "
                       "bytes, expected %d, got %d\n",
                       (s->N1_bytes + s->N2_bytes), rx_len);
    } else {
        uint8_t ecc_control;
        trace_pnv_spi_rx_received(rx_len);
        trace_pnv_spi_log_Ncounts(s->N1_bits, s->N1_bytes, s->N1_tx,
                        s->N1_rx, s->N2_bits, s->N2_bytes, s->N2_tx, s->N2_rx);
        /*
         * Adding an ECC count let's us know when we have found a payload byte
         * that was shifted in but cannot be loaded into RDR.  Bits 29-30 of
         * clock_config_reset_control register equal to either 0b00 or 0b10
         * indicate that we are taking in data with ECC and either applying
         * the ECC or discarding it.
         */
        ecc_count = 0;
        ecc_control = GETFIELD(SPI_CLK_CFG_ECC_CTRL, s->regs[SPI_CLK_CFG_REG]);
        if (ecc_control == 0 || ecc_control == 2) {
            ecc_count = 1;
        }
        /*
         * Use the N1_rx and N2_rx counts to control shifting data from the
         * payload into the RDR.  Keep an overall count of the number of bytes
         * shifted into RDR so we can discard every 9th byte when ECC is
         * enabled.
         */
        shift_in_count = 0;
        /* Handle the N1 portion of the frame first */
        if (s->N1_rx != 0) {
            trace_pnv_spi_rx_read_N1frame();
            shift_in_count = read_from_frame(s, s->N1_bytes, ecc_count, shift_in_count);
        }
        /* Handle the N2 portion of the frame */
        if (s->N2_rx != 0) {
            /* pop out N1_bytes from rx_fifo if not already */
            if (s->N1_rx == 0) {
                for (i = 0; i < s->N1_bytes; i++) {
                    if (!fifo8_is_empty(&s->rx_fifo)) {
                        fifo8_pop(&s->rx_fifo);
                    } else {
                        qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: Reading empty"
                                                       " RX_FIFO\n");
                    }
                }
            }
            trace_pnv_spi_rx_read_N2frame();
            shift_in_count = read_from_frame(s, s->N2_bytes, ecc_count, shift_in_count);
        }
        if ((s->N1_rx + s->N2_rx) > 0) {
            /*
             * Data was received so handle RDR status.
             * It is easier to handle RDR_full and RDR_overrun status here
             * since the RDR register's shift_byte_in method is called
             * multiple times in a row. Controlling RDR status is done here
             * instead of in the RDR scoped methods for that reason.
             */
            if (GETFIELD(SPI_STS_RDR_FULL, s->status) == 1) {
                /*
                 * Data was shifted into the RDR before having been read
                 * causing previous data to have been overrun.
                 */
                s->status = SETFIELD(SPI_STS_RDR_OVERRUN, s->status, 1);
            } else {
                /*
                 * Set status to indicate that the received data register is
                 * full. This flag is only cleared once the RDR is unloaded.
                 */
                s->status = SETFIELD(SPI_STS_RDR_FULL, s->status, 1);
            }
        }
    } /* end of else */
} /* end of spi_response() */

static void transfer(PnvSpi *s)
{
    uint32_t tx, rx, payload_len;
    uint8_t rx_byte;

    payload_len = fifo8_num_used(&s->tx_fifo);
    for (int offset = 0; offset < payload_len; offset += s->transfer_len) {
        tx = 0;
        for (int i = 0; i < s->transfer_len; i++) {
            if ((offset + i) >= payload_len) {
                tx <<= 8;
            } else if (!fifo8_is_empty(&s->tx_fifo)) {
                tx = (tx << 8) | fifo8_pop(&s->tx_fifo);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: TX_FIFO underflow\n");
            }
        }
        rx = ssi_transfer(s->ssi_bus, tx);
        for (int i = 0; i < s->transfer_len; i++) {
            if ((offset + i) >= payload_len) {
                break;
            }
            rx_byte = (rx >> (8 * (s->transfer_len - 1) - i * 8)) & 0xFF;
            if (!fifo8_is_full(&s->rx_fifo)) {
                fifo8_push(&s->rx_fifo, rx_byte);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: RX_FIFO is full\n");
                break;
            }
        }
    }
    spi_response(s);
    /* Reset fifo for next frame */
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);
}

/*
 * Calculate the N1 counters based on passed in opcode and
 * internal register values.
 * The method assumes that the opcode is a Shift_N1 opcode
 * and doesn't test it.
 * The counters returned are:
 * N1 bits: Number of bits in the payload data that are significant
 * to the responder.
 * N1_bytes: Total count of payload bytes for the N1 (portion of the) frame.
 * N1_tx: Total number of bytes taken from TDR for N1
 * N1_rx: Total number of bytes taken from the payload for N1
 */
static void calculate_N1(PnvSpi *s, uint8_t opcode)
{
    /*
     * Shift_N1 opcode form: 0x3M
     * Implicit mode:
     * If M != 0 the shift count is M bytes and M is the number of tx bytes.
     * Forced Implicit mode:
     * M is the shift count but tx and rx is determined by the count control
     * register fields.  Note that we only check for forced Implicit mode when
     * M != 0 since the mode doesn't make sense when M = 0.
     * Explicit mode:
     * If M == 0 then shift count is number of bits defined in the
     * Counter Configuration Register's shift_count_N1 field.
     */
    if (PNV_SPI_OPCODE_LO_NIBBLE(opcode) == 0) {
        /* Explicit mode */
        s->N1_bits = GETFIELD(SPI_CTR_CFG_N1, s->regs[SPI_CTR_CFG_REG]);
        s->N1_bytes = (s->N1_bits + 7) / 8;
        s->N1_tx = 0;
        s->N1_rx = 0;
        /* If tx count control for N1 is set, load the tx value */
        if (GETFIELD(SPI_CTR_CFG_N1_CTRL_B2, s->regs[SPI_CTR_CFG_REG]) == 1) {
            s->N1_tx = s->N1_bytes;
        }
        /* If rx count control for N1 is set, load the rx value */
        if (GETFIELD(SPI_CTR_CFG_N1_CTRL_B3, s->regs[SPI_CTR_CFG_REG]) == 1) {
            s->N1_rx = s->N1_bytes;
        }
    } else {
        /* Implicit mode/Forced Implicit mode, use M field from opcode */
        s->N1_bytes = PNV_SPI_OPCODE_LO_NIBBLE(opcode);
        s->N1_bits = s->N1_bytes * 8;
        /*
         * Assume that we are going to transmit the count
         * (pure Implicit only)
         */
        s->N1_tx = s->N1_bytes;
        s->N1_rx = 0;
        /* Let Forced Implicit mode have an effect on the counts */
        if (GETFIELD(SPI_CTR_CFG_N1_CTRL_B1, s->regs[SPI_CTR_CFG_REG]) == 1) {
            /*
             * If Forced Implicit mode and count control doesn't
             * indicate transmit then reset the tx count to 0
             */
            if (GETFIELD(SPI_CTR_CFG_N1_CTRL_B2, s->regs[SPI_CTR_CFG_REG]) == 0) {
                s->N1_tx = 0;
            }
            /* If rx count control for N1 is set, load the rx value */
            if (GETFIELD(SPI_CTR_CFG_N1_CTRL_B3, s->regs[SPI_CTR_CFG_REG]) == 1) {
                s->N1_rx = s->N1_bytes;
            }
        }
    }
    /*
     * Enforce an upper limit on the size of N1 that is equal to the known size
     * of the shift register, 64 bits or 72 bits if ECC is enabled.
     * If the size exceeds 72 bits it is a user error so log an error,
     * cap the size at a max of 64 bits or 72 bits and set the sequencer FSM
     * error bit.
     */
    uint8_t ecc_control = GETFIELD(SPI_CLK_CFG_ECC_CTRL, s->regs[SPI_CLK_CFG_REG]);
    if (ecc_control == 0 || ecc_control == 2) {
        if (s->N1_bytes > (PNV_SPI_REG_SIZE + 1)) {
            qemu_log_mask(LOG_GUEST_ERROR, "Unsupported N1 shift size when "
                          "ECC enabled, bytes = 0x%x, bits = 0x%x\n",
                          s->N1_bytes, s->N1_bits);
            s->N1_bytes = PNV_SPI_REG_SIZE + 1;
            s->N1_bits = s->N1_bytes * 8;
        }
    } else if (s->N1_bytes > PNV_SPI_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "Unsupported N1 shift size, "
                      "bytes = 0x%x, bits = 0x%x\n", s->N1_bytes, s->N1_bits);
        s->N1_bytes = PNV_SPI_REG_SIZE;
        s->N1_bits = s->N1_bytes * 8;
    }
} /* end of calculate_N1 */

/*
 * Shift_N1 operation handler method
 */
static bool operation_shiftn1(PnvSpi *s, uint8_t opcode, bool send_n1_alone)
{
    uint8_t n1_count;
    bool stop = false;
    /*
     * Use a combination of N1 counters to build the N1 portion of the
     * transmit payload.
     * We only care about transmit at this time since the request payload
     * only represents data going out on the controller output line.
     * Leave mode specific considerations in the calculate function since
     * all we really care about are counters that tell use exactly how
     * many bytes are in the payload and how many of those bytes to
     * include from the TDR into the payload.
     */
    calculate_N1(s, opcode);
    trace_pnv_spi_log_Ncounts(s->N1_bits, s->N1_bytes, s->N1_tx,
                    s->N1_rx, s->N2_bits, s->N2_bytes, s->N2_tx, s->N2_rx);
    /*
     * Zero out the N2 counters here in case there is no N2 operation following
     * the N1 operation in the sequencer.  This keeps leftover N2 information
     * from interfering with spi_response logic.
     */
    s->N2_bits = 0;
    s->N2_bytes = 0;
    s->N2_tx = 0;
    s->N2_rx = 0;
    /*
     * N1_bytes is the overall size of the N1 portion of the frame regardless of
     * whether N1 is used for tx, rx or both.  Loop over the size to build a
     * payload that is N1_bytes long.
     * N1_tx is the count of bytes to take from the TDR and "shift" into the
     * frame which means append those bytes to the payload for the N1 portion
     * of the frame.
     * If N1_tx is 0 or if the count exceeds the size of the TDR append 0xFF to
     * the frame until the overall N1 count is reached.
     */
    n1_count = 0;
    while (n1_count < s->N1_bytes) {
        /*
         * Assuming that if N1_tx is not equal to 0 then it is the same as
         * N1_bytes.
         */
        if ((s->N1_tx != 0) && (n1_count < PNV_SPI_REG_SIZE)) {

            if (GETFIELD(SPI_STS_TDR_FULL, s->status) == 1) {
                /*
                 * Note that we are only appending to the payload IF the TDR
                 * is full otherwise we don't touch the payload because we are
                 * going to NOT send the payload and instead tell the sequencer
                 * that called us to stop and wait for a TDR write so we have
                 * data to load into the payload.
                 */
                uint8_t n1_byte = 0x00;
                n1_byte = get_from_offset(s, n1_count);
                if (!fifo8_is_full(&s->tx_fifo)) {
                    trace_pnv_spi_tx_append("n1_byte", n1_byte, n1_count);
                    fifo8_push(&s->tx_fifo, n1_byte);
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: TX_FIFO is full\n");
                    break;
                }
            } else {
                /*
                 * We hit a shift_n1 opcode TX but the TDR is empty, tell the
                 * sequencer to stop and break this loop.
                 */
                trace_pnv_spi_sequencer_stop_requested("Shift N1"
                                "set for transmit but TDR is empty");
                stop = true;
                break;
            }
        } else {
            /*
             * Cases here:
             * - we are receiving during the N1 frame segment and the RDR
             *   is full so we need to stop until the RDR is read
             * - we are transmitting and we don't care about RDR status
             *   since we won't be loading RDR during the frame segment.
             * - we are receiving and the RDR is empty so we allow the operation
             *   to proceed.
             */
            if ((s->N1_rx != 0) && (GETFIELD(SPI_STS_RDR_FULL, s->status) == 1)) {
                trace_pnv_spi_sequencer_stop_requested("shift N1"
                                "set for receive but RDR is full");
                stop = true;
                break;
            } else if (!fifo8_is_full(&s->tx_fifo)) {
                trace_pnv_spi_tx_append_FF("n1_byte");
                fifo8_push(&s->tx_fifo, 0xff);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: TX_FIFO is full\n");
                break;
            }
        }
        n1_count++;
    } /* end of while */
    /*
     * If we are not stopping due to an empty TDR and we are doing an N1 TX
     * and the TDR is full we need to clear the TDR_full status.
     * Do this here instead of up in the loop above so we don't log the message
     * in every loop iteration.
     * Ignore the send_n1_alone flag, all that does is defer the TX until the N2
     * operation, which was found immediately after the current opcode.  The TDR
     * was unloaded and will be shifted so we have to clear the TDR_full status.
     */
    if (!stop && (s->N1_tx != 0) &&
        (GETFIELD(SPI_STS_TDR_FULL, s->status) == 1)) {
        s->status = SETFIELD(SPI_STS_TDR_FULL, s->status, 0);
    }
    /*
     * There are other reasons why the shifter would stop, such as a TDR empty
     * or RDR full condition with N1 set to receive.  If we haven't stopped due
     * to either one of those conditions then check if the send_n1_alone flag is
     * equal to False, indicating the next opcode is an N2 operation, AND if
     * the N2 counter reload switch (bit 0 of the N2 count control field) is
     * set.  This condition requires a pacing write to "kick" off the N2
     * shift which includes the N1 shift as well when send_n1_alone is False.
     */
    if (!stop && !send_n1_alone &&
       (GETFIELD(SPI_CTR_CFG_N2_CTRL_B0, s->regs[SPI_CTR_CFG_REG]) == 1)) {
        trace_pnv_spi_sequencer_stop_requested("N2 counter reload "
                        "active, stop N1 shift, TDR_underrun set to 1");
        stop = true;
        s->status = SETFIELD(SPI_STS_TDR_UNDERRUN, s->status, 1);
    }
    /*
     * If send_n1_alone is set AND we have a full TDR then this is the first and
     * last payload to send and we don't have an N2 frame segment to add to the
     * payload.
     */
    if (send_n1_alone && !stop) {
        /* We have a TX and a full TDR or an RX and an empty RDR */
        trace_pnv_spi_tx_request("Shifting N1 frame", fifo8_num_used(&s->tx_fifo));
        transfer(s);
        /* The N1 frame shift is complete so reset the N1 counters */
        s->N2_bits = 0;
        s->N2_bytes = 0;
        s->N2_tx = 0;
        s->N2_rx = 0;
    }
    return stop;
} /* end of operation_shiftn1() */

/*
 * Calculate the N2 counters based on passed in opcode and
 * internal register values.
 * The method assumes that the opcode is a Shift_N2 opcode
 * and doesn't test it.
 * The counters returned are:
 * N2 bits: Number of bits in the payload data that are significant
 * to the responder.
 * N2_bytes: Total count of payload bytes for the N2 frame.
 * N2_tx: Total number of bytes taken from TDR for N2
 * N2_rx: Total number of bytes taken from the payload for N2
 */
static void calculate_N2(PnvSpi *s, uint8_t opcode)
{
    /*
     * Shift_N2 opcode form: 0x4M
     * Implicit mode:
     * If M!=0 the shift count is M bytes and M is the number of rx bytes.
     * Forced Implicit mode:
     * M is the shift count but tx and rx is determined by the count control
     * register fields.  Note that we only check for Forced Implicit mode when
     * M != 0 since the mode doesn't make sense when M = 0.
     * Explicit mode:
     * If M==0 then shift count is number of bits defined in the
     * Counter Configuration Register's shift_count_N1 field.
     */
    if (PNV_SPI_OPCODE_LO_NIBBLE(opcode) == 0) {
        /* Explicit mode */
        s->N2_bits = GETFIELD(SPI_CTR_CFG_N2, s->regs[SPI_CTR_CFG_REG]);
        s->N2_bytes = (s->N2_bits + 7) / 8;
        s->N2_tx = 0;
        s->N2_rx = 0;
        /* If tx count control for N2 is set, load the tx value */
        if (GETFIELD(SPI_CTR_CFG_N2_CTRL_B2, s->regs[SPI_CTR_CFG_REG]) == 1) {
            s->N2_tx = s->N2_bytes;
        }
        /* If rx count control for N2 is set, load the rx value */
        if (GETFIELD(SPI_CTR_CFG_N2_CTRL_B3, s->regs[SPI_CTR_CFG_REG]) == 1) {
            s->N2_rx = s->N2_bytes;
        }
    } else {
        /* Implicit mode/Forced Implicit mode, use M field from opcode */
        s->N2_bytes = PNV_SPI_OPCODE_LO_NIBBLE(opcode);
        s->N2_bits = s->N2_bytes * 8;
        /* Assume that we are going to receive the count */
        s->N2_rx = s->N2_bytes;
        s->N2_tx = 0;
        /* Let Forced Implicit mode have an effect on the counts */
        if (GETFIELD(SPI_CTR_CFG_N2_CTRL_B1, s->regs[SPI_CTR_CFG_REG]) == 1) {
            /*
             * If Forced Implicit mode and count control doesn't
             * indicate a receive then reset the rx count to 0
             */
            if (GETFIELD(SPI_CTR_CFG_N2_CTRL_B3, s->regs[SPI_CTR_CFG_REG]) == 0) {
                s->N2_rx = 0;
            }
            /* If tx count control for N2 is set, load the tx value */
            if (GETFIELD(SPI_CTR_CFG_N2_CTRL_B2, s->regs[SPI_CTR_CFG_REG]) == 1) {
                s->N2_tx = s->N2_bytes;
            }
        }
    }
    /*
     * Enforce an upper limit on the size of N1 that is equal to the
     * known size of the shift register, 64 bits or 72 bits if ECC
     * is enabled.
     * If the size exceeds 72 bits it is a user error so log an error,
     * cap the size at a max of 64 bits or 72 bits and set the sequencer FSM
     * error bit.
     */
    uint8_t ecc_control = GETFIELD(SPI_CLK_CFG_ECC_CTRL, s->regs[SPI_CLK_CFG_REG]);
    if (ecc_control == 0 || ecc_control == 2) {
        if (s->N2_bytes > (PNV_SPI_REG_SIZE + 1)) {
            /* Unsupported N2 shift size when ECC enabled */
            s->N2_bytes = PNV_SPI_REG_SIZE + 1;
            s->N2_bits = s->N2_bytes * 8;
        }
    } else if (s->N2_bytes > PNV_SPI_REG_SIZE) {
        /* Unsupported N2 shift size */
        s->N2_bytes = PNV_SPI_REG_SIZE;
        s->N2_bits = s->N2_bytes * 8;
    }
} /* end of calculate_N2 */

/*
 * Shift_N2 operation handler method
 */

static bool operation_shiftn2(PnvSpi *s, uint8_t opcode)
{
    uint8_t n2_count;
    bool stop = false;
    /*
     * Use a combination of N2 counters to build the N2 portion of the
     * transmit payload.
     */
    calculate_N2(s, opcode);
    trace_pnv_spi_log_Ncounts(s->N1_bits, s->N1_bytes, s->N1_tx,
                    s->N1_rx, s->N2_bits, s->N2_bytes, s->N2_tx, s->N2_rx);
    /*
     * The only difference between this code and the code for shift N1 is
     * that this code has to account for the possible presence of N1 transmit
     * bytes already taken from the TDR.
     * If there are bytes to be transmitted for the N2 portion of the frame
     * and there are still bytes in TDR that have not been copied into the
     * TX data of the payload, this code will handle transmitting those
     * remaining bytes.
     * If for some reason the transmit count(s) add up to more than the size
     * of the TDR we will just append 0xFF to the transmit payload data until
     * the payload is N1 + N2 bytes long.
     */
    n2_count = 0;
    while (n2_count < s->N2_bytes) {
        /*
         * If the RDR is full and we need to RX just bail out, letting the
         * code continue will end up building the payload twice in the same
         * buffer since RDR full causes a sequence stop and restart.
         */
        if ((s->N2_rx != 0) && (GETFIELD(SPI_STS_RDR_FULL, s->status) == 1)) {
            trace_pnv_spi_sequencer_stop_requested("shift N2 set"
                            "for receive but RDR is full");
            stop = true;
            break;
        }
        if ((s->N2_tx != 0) && ((s->N1_tx + n2_count) < PNV_SPI_REG_SIZE)) {
            /* Always append data for the N2 segment if it is set for TX */
            uint8_t n2_byte = 0x00;
            n2_byte = get_from_offset(s, (s->N1_tx + n2_count));
            if (!fifo8_is_full(&s->tx_fifo)) {
                trace_pnv_spi_tx_append("n2_byte", n2_byte, (s->N1_tx + n2_count));
                fifo8_push(&s->tx_fifo, n2_byte);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: TX_FIFO is full\n");
                break;
            }
        } else if (!fifo8_is_full(&s->tx_fifo)) {
            /*
             * Regardless of whether or not N2 is set for TX or RX, we need
             * the number of bytes in the payload to match the overall length
             * of the operation.
             */
            trace_pnv_spi_tx_append_FF("n2_byte");
            fifo8_push(&s->tx_fifo, 0xff);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: TX_FIFO is full\n");
            break;
        }
        n2_count++;
    } /* end of while */
    if (!stop) {
        /* We have a TX and a full TDR or an RX and an empty RDR */
        trace_pnv_spi_tx_request("Shifting N2 frame", fifo8_num_used(&s->tx_fifo));
        transfer(s);
        /*
         * If we are doing an N2 TX and the TDR is full we need to clear the
         * TDR_full status. Do this here instead of up in the loop above so we
         * don't log the message in every loop iteration.
         */
        if ((s->N2_tx != 0) && (GETFIELD(SPI_STS_TDR_FULL, s->status) == 1)) {
            s->status = SETFIELD(SPI_STS_TDR_FULL, s->status, 0);
        }
        /*
         * The N2 frame shift is complete so reset the N2 counters.
         * Reset the N1 counters also in case the frame was a combination of
         * N1 and N2 segments.
         */
        s->N2_bits = 0;
        s->N2_bytes = 0;
        s->N2_tx = 0;
        s->N2_rx = 0;
        s->N1_bits = 0;
        s->N1_bytes = 0;
        s->N1_tx = 0;
        s->N1_rx = 0;
    }
    return stop;
} /*  end of operation_shiftn2()*/

static void operation_sequencer(PnvSpi *s)
{
    /*
     * Loop through each sequencer operation ID and perform the requested
     *  operations.
     * Flag for indicating if we should send the N1 frame or wait to combine
     * it with a preceding N2 frame.
     */
    bool send_n1_alone = true;
    bool stop = false; /* Flag to stop the sequencer */
    uint8_t opcode = 0;
    uint8_t masked_opcode = 0;
    uint8_t seq_index;

    /*
     * Clear the sequencer FSM error bit - general_SPI_status[3]
     * before starting a sequence.
     */
    s->status = SETFIELD(SPI_STS_GEN_STATUS_B3, s->status, 0);
    /*
     * If the FSM is idle set the sequencer index to 0
     * (new/restarted sequence)
     */
    if (GETFIELD(SPI_STS_SEQ_FSM, s->status) == SEQ_STATE_IDLE) {
        s->status = SETFIELD(SPI_STS_SEQ_INDEX, s->status, 0);
    }
    /*
     * SPI_STS_SEQ_INDEX of status register is kept in seq_index variable and
     * updated back to status register at the end of operation_sequencer().
     */
    seq_index = GETFIELD(SPI_STS_SEQ_INDEX, s->status);
    /*
     * There are only 8 possible operation IDs to iterate through though
     * some operations may cause more than one frame to be sequenced.
     */
    while (seq_index < NUM_SEQ_OPS) {
        opcode = s->seq_op[seq_index];
        /* Set sequencer state to decode */
        s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_DECODE);
        /*
         * Only the upper nibble of the operation ID is needed to know what
         * kind of operation is requested.
         */
        masked_opcode = PNV_SPI_MASKED_OPCODE(opcode);
        switch (masked_opcode) {
        /*
         * Increment the operation index in each case instead of just
         * once at the end in case an operation like the branch
         * operation needs to change the index.
         */
        case SEQ_OP_STOP:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            /* A stop operation in any position stops the sequencer */
            trace_pnv_spi_sequencer_op("STOP", seq_index);

            stop = true;
            s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_IDLE);
            s->loop_counter_1 = 0;
            s->loop_counter_2 = 0;
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_IDLE);
            break;

        case SEQ_OP_SELECT_SLAVE:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            trace_pnv_spi_sequencer_op("SELECT_SLAVE", seq_index);
            /*
             * This device currently only supports a single responder
             * connection at position 0.  De-selecting a responder is fine
             * and expected at the end of a sequence but selecting any
             * responder other than 0 should cause an error.
             */
            s->responder_select = PNV_SPI_OPCODE_LO_NIBBLE(opcode);
            if (s->responder_select == 0) {
                trace_pnv_spi_shifter_done();
                qemu_set_irq(s->cs_line[0], 1);
                seq_index++;
                s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_DONE);
            } else if (s->responder_select != 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "Slave selection other than 1 "
                              "not supported, select = 0x%x\n", s->responder_select);
                trace_pnv_spi_sequencer_stop_requested("invalid responder select");
                s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_IDLE);
                stop = true;
            } else {
                /*
                 * Only allow an FSM_START state when a responder is
                 * selected
                 */
                s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_START);
                trace_pnv_spi_shifter_stating();
                qemu_set_irq(s->cs_line[0], 0);
                /*
                 * A Shift_N2 operation is only valid after a Shift_N1
                 * according to the spec. The spec doesn't say if that means
                 * immediately after or just after at any point. We will track
                 * the occurrence of a Shift_N1 to enforce this requirement in
                 * the most generic way possible by assuming that the rule
                 * applies once a valid responder select has occurred.
                 */
                s->shift_n1_done = false;
                seq_index++;
                s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status,
                                SEQ_STATE_INDEX_INCREMENT);
            }
            break;

        case SEQ_OP_SHIFT_N1:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            trace_pnv_spi_sequencer_op("SHIFT_N1", seq_index);
            /*
             * Only allow a shift_n1 when the state is not IDLE or DONE.
             * In either of those two cases the sequencer is not in a proper
             * state to perform shift operations because the sequencer has:
             * - processed a responder deselect (DONE)
             * - processed a stop opcode (IDLE)
             * - encountered an error (IDLE)
             */
            if ((GETFIELD(SPI_STS_SHIFTER_FSM, s->status) == FSM_IDLE) ||
                (GETFIELD(SPI_STS_SHIFTER_FSM, s->status) == FSM_DONE)) {
                qemu_log_mask(LOG_GUEST_ERROR, "Shift_N1 not allowed in "
                              "shifter state = 0x%llx", GETFIELD(
                        SPI_STS_SHIFTER_FSM, s->status));
                /*
                 * Set sequencer FSM error bit 3 (general_SPI_status[3])
                 * in status reg.
                 */
                s->status = SETFIELD(SPI_STS_GEN_STATUS_B3, s->status, 1);
                trace_pnv_spi_sequencer_stop_requested("invalid shifter state");
                stop = true;
            } else {
                /*
                 * Look for the special case where there is a shift_n1 set for
                 * transmit and it is followed by a shift_n2 set for transmit
                 * AND the combined transmit length of the two operations is
                 * less than or equal to the size of the TDR register. In this
                 * case we want to use both this current shift_n1 opcode and the
                 * following shift_n2 opcode to assemble the frame for
                 * transmission to the responder without requiring a refill of
                 * the TDR between the two operations.
                 */
                if ((seq_index != 7) &&
                    PNV_SPI_MASKED_OPCODE(s->seq_op[(seq_index + 1)]) ==
                    SEQ_OP_SHIFT_N2) {
                    send_n1_alone = false;
                }
                s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_SHIFT_N1);
                stop = operation_shiftn1(s, opcode, send_n1_alone);
                if (stop) {
                    /*
                     *  The operation code says to stop, this can occur if:
                     * (1) RDR is full and the N1 shift is set for receive
                     * (2) TDR was empty at the time of the N1 shift so we need
                     * to wait for data.
                     * (3) Neither 1 nor 2 are occurring and we aren't sending
                     * N1 alone and N2 counter reload is set (bit 0 of the N2
                     * counter reload field).  In this case TDR_underrun will
                     * will be set and the Payload has been loaded so it is
                     * ok to advance the sequencer.
                     */
                    if (GETFIELD(SPI_STS_TDR_UNDERRUN, s->status)) {
                        s->shift_n1_done = true;
                        s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status,
                                                  FSM_SHIFT_N2);
                        seq_index++;
                    } else {
                        /*
                         * This is case (1) or (2) so the sequencer needs to
                         * wait and NOT go to the next sequence yet.
                         */
                        s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_WAIT);
                    }
                } else {
                    /* Ok to move on to the next index */
                    s->shift_n1_done = true;
                    seq_index++;
                    s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status,
                                    SEQ_STATE_INDEX_INCREMENT);
                }
            }
            break;

        case SEQ_OP_SHIFT_N2:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            trace_pnv_spi_sequencer_op("SHIFT_N2", seq_index);
            if (!s->shift_n1_done) {
                qemu_log_mask(LOG_GUEST_ERROR, "Shift_N2 is not allowed if a "
                              "Shift_N1 is not done, shifter state = 0x%llx",
                              GETFIELD(SPI_STS_SHIFTER_FSM, s->status));
                /*
                 * In case the sequencer actually stops if an N2 shift is
                 * requested before any N1 shift is done. Set sequencer FSM
                 * error bit 3 (general_SPI_status[3]) in status reg.
                 */
                s->status = SETFIELD(SPI_STS_GEN_STATUS_B3, s->status, 1);
                trace_pnv_spi_sequencer_stop_requested("shift_n2 w/no shift_n1 done");
                stop = true;
            } else {
                /* Ok to do a Shift_N2 */
                s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_SHIFT_N2);
                stop = operation_shiftn2(s, opcode);
                /*
                 * If the operation code says to stop set the shifter state to
                 * wait and stop
                 */
                if (stop) {
                    s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_WAIT);
                } else {
                    /* Ok to move on to the next index */
                    seq_index++;
                    s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status,
                                    SEQ_STATE_INDEX_INCREMENT);
                }
            }
            break;

        case SEQ_OP_BRANCH_IFNEQ_RDR:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            trace_pnv_spi_sequencer_op("BRANCH_IFNEQ_RDR", seq_index);
            /*
             * The memory mapping register RDR match value is compared against
             * the 16 rightmost bytes of the RDR (potentially with masking).
             * Since this comparison is performed against the contents of the
             * RDR then a receive must have previously occurred otherwise
             * there is no data to compare and the operation cannot be
             * completed and will stop the sequencer until RDR full is set to
             * 1.
             */
            if (GETFIELD(SPI_STS_RDR_FULL, s->status) == 1) {
                bool rdr_matched = false;
                rdr_matched = does_rdr_match(s);
                if (rdr_matched) {
                    trace_pnv_spi_RDR_match("success");
                    s->fail_count = 0;
                    /* A match occurred, increment the sequencer index. */
                    seq_index++;
                    s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status,
                                    SEQ_STATE_INDEX_INCREMENT);
                } else {
                    trace_pnv_spi_RDR_match("failed");
                    s->fail_count++;
                    /*
                     * Branch the sequencer to the index coded into the op
                     * code.
                     */
                    seq_index = PNV_SPI_OPCODE_LO_NIBBLE(opcode);
                }
                if (s->fail_count >= RDR_MATCH_FAILURE_LIMIT) {
                    qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi: RDR match failure"
                                  " limit crossed %d times hence requesting "
                                  "sequencer to stop.\n",
                                  RDR_MATCH_FAILURE_LIMIT);
                    stop = true;
                }
                /*
                 * Regardless of where the branch ended up we want the
                 * sequencer to continue shifting so we have to clear
                 * RDR_full.
                 */
                s->status = SETFIELD(SPI_STS_RDR_FULL, s->status, 0);
            } else {
                trace_pnv_spi_sequencer_stop_requested("RDR not"
                                "full for 0x6x opcode");
                stop = true;
                s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_WAIT);
            }
            break;

        case SEQ_OP_TRANSFER_TDR:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            qemu_log_mask(LOG_GUEST_ERROR, "Transfer TDR is not supported\n");
            seq_index++;
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_INDEX_INCREMENT);
            break;

        case SEQ_OP_BRANCH_IFNEQ_INC_1:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            trace_pnv_spi_sequencer_op("BRANCH_IFNEQ_INC_1", seq_index);
            /*
             * The spec says the loop should execute count compare + 1 times.
             * However we learned from engineering that we really only loop
             * count_compare times, count compare = 0 makes this op code a
             * no-op
             */
            if (s->loop_counter_1 !=
                GETFIELD(SPI_CTR_CFG_CMP1, s->regs[SPI_CTR_CFG_REG])) {
                /*
                 * Next index is the lower nibble of the branch operation ID,
                 * mask off all but the first three bits so we don't try to
                 * access beyond the sequencer_operation_reg boundary.
                 */
                seq_index = PNV_SPI_OPCODE_LO_NIBBLE(opcode);
                s->loop_counter_1++;
            } else {
                /* Continue to next index if loop counter is reached */
                seq_index++;
                s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status,
                                SEQ_STATE_INDEX_INCREMENT);
            }
            break;

        case SEQ_OP_BRANCH_IFNEQ_INC_2:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            trace_pnv_spi_sequencer_op("BRANCH_IFNEQ_INC_2", seq_index);
            uint8_t condition2 = GETFIELD(SPI_CTR_CFG_CMP2,
                              s->regs[SPI_CTR_CFG_REG]);
            /*
             * The spec says the loop should execute count compare + 1 times.
             * However we learned from engineering that we really only loop
             * count_compare times, count compare = 0 makes this op code a
             * no-op
             */
            if (s->loop_counter_2 != condition2) {
                /*
                 * Next index is the lower nibble of the branch operation ID,
                 * mask off all but the first three bits so we don't try to
                 * access beyond the sequencer_operation_reg boundary.
                 */
                seq_index = PNV_SPI_OPCODE_LO_NIBBLE(opcode);
                s->loop_counter_2++;
            } else {
                /* Continue to next index if loop counter is reached */
                seq_index++;
                s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status,
                                SEQ_STATE_INDEX_INCREMENT);
            }
            break;

        default:
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_EXECUTE);
            /* Ignore unsupported operations. */
            seq_index++;
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_INDEX_INCREMENT);
            break;
        } /* end of switch */
        /*
         * If we used all 8 opcodes without seeing a 00 - STOP in the sequence
         * we need to go ahead and end things as if there was a STOP at the
         * end.
         */
        if (seq_index == NUM_SEQ_OPS) {
            /* All 8 opcodes completed, sequencer idling */
            s->status = SETFIELD(SPI_STS_SHIFTER_FSM, s->status, FSM_IDLE);
            seq_index = 0;
            s->loop_counter_1 = 0;
            s->loop_counter_2 = 0;
            s->status = SETFIELD(SPI_STS_SEQ_FSM, s->status, SEQ_STATE_IDLE);
            break;
        }
        /* Break the loop if a stop was requested */
        if (stop) {
            break;
        }
    } /* end of while */
    /* Update sequencer index field in status.*/
    s->status = SETFIELD(SPI_STS_SEQ_INDEX, s->status, seq_index);
} /* end of operation_sequencer() */

/*
 * The SPIC engine and its internal sequencer can be interrupted and reset by
 * a hardware signal, the sbe_spicst_hard_reset bits from Pervasive
 * Miscellaneous Register of sbe_register_bo device.
 * Reset immediately aborts any SPI transaction in progress and returns the
 * sequencer and state machines to idle state.
 * The configuration register values are not changed. The status register is
 * not reset. The engine registers are not reset.
 * The SPIC engine reset does not have any affect on the attached devices.
 * Reset handling of any attached devices is beyond the scope of the engine.
 */
static void do_reset(DeviceState *dev)
{
    PnvSpi *s = PNV_SPI(dev);
    DeviceState *ssi_dev;

    trace_pnv_spi_reset();

    /* Connect cs irq */
    ssi_dev = ssi_get_cs(s->ssi_bus, 0);
    if (ssi_dev) {
        qemu_irq cs_line = qdev_get_gpio_in_named(ssi_dev, SSI_GPIO_CS, 0);
        qdev_connect_gpio_out_named(DEVICE(s), "cs", 0, cs_line);
    }

    /* Reset all N1 and N2 counters, and other constants */
    s->N2_bits = 0;
    s->N2_bytes = 0;
    s->N2_tx = 0;
    s->N2_rx = 0;
    s->N1_bits = 0;
    s->N1_bytes = 0;
    s->N1_tx = 0;
    s->N1_rx = 0;
    s->loop_counter_1 = 0;
    s->loop_counter_2 = 0;
    /* Disconnected from responder */
    qemu_set_irq(s->cs_line[0], 1);
}

static uint64_t pnv_spi_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvSpi *s = PNV_SPI(opaque);
    uint32_t reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case ERROR_REG:
    case SPI_CTR_CFG_REG:
    case CONFIG_REG1:
    case SPI_CLK_CFG_REG:
    case SPI_MM_REG:
    case SPI_XMIT_DATA_REG:
        val = s->regs[reg];
        break;
    case SPI_RCV_DATA_REG:
        val = s->regs[reg];
        trace_pnv_spi_read_RDR(val);
        s->status = SETFIELD(SPI_STS_RDR_FULL, s->status, 0);
        if (GETFIELD(SPI_STS_SHIFTER_FSM, s->status) == FSM_WAIT) {
            trace_pnv_spi_start_sequencer();
            operation_sequencer(s);
        }
        break;
    case SPI_SEQ_OP_REG:
        val = 0;
        for (int i = 0; i < PNV_SPI_REG_SIZE; i++) {
            val = (val << 8) | s->seq_op[i];
        }
        break;
    case SPI_STS_REG:
        val = s->status;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi_regs: Invalid xscom "
                 "read at 0x%" PRIx32 "\n", reg);
    }

    trace_pnv_spi_read(addr, val);
    return val;
}

static void pnv_spi_xscom_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvSpi *s = PNV_SPI(opaque);
    uint32_t reg = addr >> 3;

    trace_pnv_spi_write(addr, val);

    switch (reg) {
    case ERROR_REG:
    case SPI_CTR_CFG_REG:
    case CONFIG_REG1:
    case SPI_MM_REG:
    case SPI_RCV_DATA_REG:
        s->regs[reg] = val;
        break;
    case SPI_CLK_CFG_REG:
        /*
         * To reset the SPI controller write the sequence 0x5 0xA to
         * reset_control field
         */
        if ((GETFIELD(SPI_CLK_CFG_RST_CTRL, s->regs[SPI_CLK_CFG_REG]) == 0x5)
             && (GETFIELD(SPI_CLK_CFG_RST_CTRL, val) == 0xA)) {
                /* SPI controller reset sequence completed, resetting */
            s->regs[reg] = SPI_CLK_CFG_HARD_RST;
        } else {
            s->regs[reg] = val;
        }
        break;
    case SPI_XMIT_DATA_REG:
        /*
         * Writing to the transmit data register causes the transmit data
         * register full status bit in the status register to be set.  Writing
         * when the transmit data register full status bit is already set
         * causes a "Resource Not Available" condition.  This is not possible
         * in the model since writes to this register are not asynchronous to
         * the operation sequence like it would be in hardware.
         */
        s->regs[reg] = val;
        trace_pnv_spi_write_TDR(val);
        s->status = SETFIELD(SPI_STS_TDR_FULL, s->status, 1);
        s->status = SETFIELD(SPI_STS_TDR_UNDERRUN, s->status, 0);
        trace_pnv_spi_start_sequencer();
        operation_sequencer(s);
        break;
    case SPI_SEQ_OP_REG:
        for (int i = 0; i < PNV_SPI_REG_SIZE; i++) {
            s->seq_op[i] = (val >> (56 - i * 8)) & 0xFF;
        }
        break;
    case SPI_STS_REG:
        /* other fields are ignore_write */
        s->status = SETFIELD(SPI_STS_RDR_OVERRUN, s->status,
                                  GETFIELD(SPI_STS_RDR, val));
        s->status = SETFIELD(SPI_STS_TDR_OVERRUN, s->status,
                                  GETFIELD(SPI_STS_TDR, val));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pnv_spi_regs: Invalid xscom "
                 "write at 0x%" PRIx32 "\n", reg);
    }
}

static const MemoryRegionOps pnv_spi_xscom_ops = {
    .read = pnv_spi_xscom_read,
    .write = pnv_spi_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static const Property pnv_spi_properties[] = {
    DEFINE_PROP_UINT32("spic_num", PnvSpi, spic_num, 0),
    DEFINE_PROP_UINT32("chip-id", PnvSpi, chip_id, 0),
    DEFINE_PROP_UINT8("transfer_len", PnvSpi, transfer_len, 4),
};

static void pnv_spi_realize(DeviceState *dev, Error **errp)
{
    PnvSpi *s = PNV_SPI(dev);
    g_autofree char *name = g_strdup_printf("chip%d." TYPE_PNV_SPI_BUS ".%d",
                    s->chip_id, s->spic_num);
    s->ssi_bus = ssi_create_bus(dev, name);
    s->cs_line = g_new0(qemu_irq, 1);
    qdev_init_gpio_out_named(DEVICE(s), s->cs_line, "cs", 1);

    fifo8_create(&s->tx_fifo, PNV_SPI_FIFO_SIZE);
    fifo8_create(&s->rx_fifo, PNV_SPI_FIFO_SIZE);

    /* spi scoms */
    pnv_xscom_region_init(&s->xscom_spic_regs, OBJECT(s), &pnv_spi_xscom_ops,
                          s, "xscom-spi", PNV10_XSCOM_PIB_SPIC_SIZE);
}

static int pnv_spi_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    PnvSpi *s = PNV_SPI(dev);
    g_autofree char *name;
    int s_offset;
    const char compat[] = "ibm,power10-spi";
    uint32_t spic_pcba = PNV10_XSCOM_PIB_SPIC_BASE +
        s->spic_num * PNV10_XSCOM_PIB_SPIC_SIZE;
    uint32_t reg[] = {
        cpu_to_be32(spic_pcba),
        cpu_to_be32(PNV10_XSCOM_PIB_SPIC_SIZE)
    };
    name = g_strdup_printf("pnv_spi@%x", spic_pcba);
    s_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(s_offset);

    _FDT(fdt_setprop(fdt, s_offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop(fdt, s_offset, "compatible", compat, sizeof(compat)));
    _FDT((fdt_setprop_cell(fdt, s_offset, "spic_num#", s->spic_num)));
    return 0;
}

static void pnv_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_spi_dt_xscom;

    dc->desc = "PowerNV SPI";
    dc->realize = pnv_spi_realize;
    device_class_set_legacy_reset(dc, do_reset);
    device_class_set_props(dc, pnv_spi_properties);
}

static const TypeInfo pnv_spi_info = {
    .name          = TYPE_PNV_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PnvSpi),
    .class_init    = pnv_spi_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_spi_register_types(void)
{
    type_register_static(&pnv_spi_info);
}

type_init(pnv_spi_register_types);
