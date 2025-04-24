/*
 * QEMU model of the Ibex SPI Controller
 * SPEC Reference: https://docs.opentitan.org/hw/ip/spi_host/doc/
 *
 * Copyright (C) 2022 Western Digital
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/registerfields.h"
#include "hw/ssi/ibex_spi_host.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "trace.h"

REG32(INTR_STATE, 0x00)
    FIELD(INTR_STATE, ERROR, 0, 1)
    FIELD(INTR_STATE, SPI_EVENT, 1, 1)
REG32(INTR_ENABLE, 0x04)
    FIELD(INTR_ENABLE, ERROR, 0, 1)
    FIELD(INTR_ENABLE, SPI_EVENT, 1, 1)
REG32(INTR_TEST, 0x08)
    FIELD(INTR_TEST, ERROR, 0, 1)
    FIELD(INTR_TEST, SPI_EVENT, 1, 1)
REG32(ALERT_TEST, 0x0c)
    FIELD(ALERT_TEST, FETAL_TEST, 0, 1)
REG32(CONTROL, 0x10)
    FIELD(CONTROL, RX_WATERMARK, 0, 8)
    FIELD(CONTROL, TX_WATERMARK, 1, 8)
    FIELD(CONTROL, OUTPUT_EN, 29, 1)
    FIELD(CONTROL, SW_RST, 30, 1)
    FIELD(CONTROL, SPIEN, 31, 1)
REG32(STATUS, 0x14)
    FIELD(STATUS, TXQD, 0, 8)
    FIELD(STATUS, RXQD, 18, 8)
    FIELD(STATUS, CMDQD, 16, 3)
    FIELD(STATUS, RXWM, 20, 1)
    FIELD(STATUS, BYTEORDER, 22, 1)
    FIELD(STATUS, RXSTALL, 23, 1)
    FIELD(STATUS, RXEMPTY, 24, 1)
    FIELD(STATUS, RXFULL, 25, 1)
    FIELD(STATUS, TXWM, 26, 1)
    FIELD(STATUS, TXSTALL, 27, 1)
    FIELD(STATUS, TXEMPTY, 28, 1)
    FIELD(STATUS, TXFULL, 29, 1)
    FIELD(STATUS, ACTIVE, 30, 1)
    FIELD(STATUS, READY, 31, 1)
REG32(CONFIGOPTS, 0x18)
    FIELD(CONFIGOPTS, CLKDIV_0, 0, 16)
    FIELD(CONFIGOPTS, CSNIDLE_0, 16, 4)
    FIELD(CONFIGOPTS, CSNTRAIL_0, 20, 4)
    FIELD(CONFIGOPTS, CSNLEAD_0, 24, 4)
    FIELD(CONFIGOPTS, FULLCYC_0, 29, 1)
    FIELD(CONFIGOPTS, CPHA_0, 30, 1)
    FIELD(CONFIGOPTS, CPOL_0, 31, 1)
REG32(CSID, 0x1c)
    FIELD(CSID, CSID, 0, 32)
REG32(COMMAND, 0x20)
    FIELD(COMMAND, LEN, 0, 8)
    FIELD(COMMAND, CSAAT, 9, 1)
    FIELD(COMMAND, SPEED, 10, 2)
    FIELD(COMMAND, DIRECTION, 12, 2)
REG32(ERROR_ENABLE, 0x2c)
    FIELD(ERROR_ENABLE, CMDBUSY, 0, 1)
    FIELD(ERROR_ENABLE, OVERFLOW, 1, 1)
    FIELD(ERROR_ENABLE, UNDERFLOW, 2, 1)
    FIELD(ERROR_ENABLE, CMDINVAL, 3, 1)
    FIELD(ERROR_ENABLE, CSIDINVAL, 4, 1)
REG32(ERROR_STATUS, 0x30)
    FIELD(ERROR_STATUS, CMDBUSY, 0, 1)
    FIELD(ERROR_STATUS, OVERFLOW, 1, 1)
    FIELD(ERROR_STATUS, UNDERFLOW, 2, 1)
    FIELD(ERROR_STATUS, CMDINVAL, 3, 1)
    FIELD(ERROR_STATUS, CSIDINVAL, 4, 1)
    FIELD(ERROR_STATUS, ACCESSINVAL, 5, 1)
REG32(EVENT_ENABLE, 0x34)
    FIELD(EVENT_ENABLE, RXFULL, 0, 1)
    FIELD(EVENT_ENABLE, TXEMPTY, 1, 1)
    FIELD(EVENT_ENABLE, RXWM, 2, 1)
    FIELD(EVENT_ENABLE, TXWM, 3, 1)
    FIELD(EVENT_ENABLE, READY, 4, 1)
    FIELD(EVENT_ENABLE, IDLE, 5, 1)

static inline uint8_t div4_round_up(uint8_t dividend)
{
    return (dividend + 3) / 4;
}

static void ibex_spi_rxfifo_reset(IbexSPIHostState *s)
{
    uint32_t data = s->regs[IBEX_SPI_HOST_STATUS];
    /* Empty the RX FIFO and assert RXEMPTY */
    fifo8_reset(&s->rx_fifo);
    data = FIELD_DP32(data, STATUS, RXFULL, 0);
    data = FIELD_DP32(data, STATUS, RXEMPTY, 1);
    s->regs[IBEX_SPI_HOST_STATUS] = data;
}

static void ibex_spi_txfifo_reset(IbexSPIHostState *s)
{
    uint32_t data = s->regs[IBEX_SPI_HOST_STATUS];
    /* Empty the TX FIFO and assert TXEMPTY */
    fifo8_reset(&s->tx_fifo);
    data = FIELD_DP32(data, STATUS, TXFULL, 0);
    data = FIELD_DP32(data, STATUS, TXEMPTY, 1);
    s->regs[IBEX_SPI_HOST_STATUS] = data;
}

static void ibex_spi_host_reset(DeviceState *dev)
{
    IbexSPIHostState *s = IBEX_SPI_HOST(dev);
    trace_ibex_spi_host_reset("Resetting Ibex SPI");

    /* SPI Host Register Reset */
    s->regs[IBEX_SPI_HOST_INTR_STATE]   = 0x00;
    s->regs[IBEX_SPI_HOST_INTR_ENABLE]  = 0x00;
    s->regs[IBEX_SPI_HOST_INTR_TEST]    = 0x00;
    s->regs[IBEX_SPI_HOST_ALERT_TEST]   = 0x00;
    s->regs[IBEX_SPI_HOST_CONTROL]      = 0x7f;
    s->regs[IBEX_SPI_HOST_STATUS]       = 0x00;
    s->regs[IBEX_SPI_HOST_CONFIGOPTS]   = 0x00;
    s->regs[IBEX_SPI_HOST_CSID]         = 0x00;
    s->regs[IBEX_SPI_HOST_COMMAND]      = 0x00;
    /* RX/TX Modelled by FIFO */
    s->regs[IBEX_SPI_HOST_RXDATA]       = 0x00;
    s->regs[IBEX_SPI_HOST_TXDATA]       = 0x00;

    s->regs[IBEX_SPI_HOST_ERROR_ENABLE] = 0x1F;
    s->regs[IBEX_SPI_HOST_ERROR_STATUS] = 0x00;
    s->regs[IBEX_SPI_HOST_EVENT_ENABLE] = 0x00;

    ibex_spi_rxfifo_reset(s);
    ibex_spi_txfifo_reset(s);

    s->init_status = true;
}

/*
 * Check if we need to trigger an interrupt.
 * The two interrupts lines (host_err and event) can
 * be enabled separately in 'IBEX_SPI_HOST_INTR_ENABLE'.
 *
 * Interrupts are triggered based on the ones
 * enabled in the `IBEX_SPI_HOST_EVENT_ENABLE` and `IBEX_SPI_HOST_ERROR_ENABLE`.
 */
static void ibex_spi_host_irq(IbexSPIHostState *s)
{
    uint32_t intr_test_reg = s->regs[IBEX_SPI_HOST_INTR_TEST];
    uint32_t intr_en_reg = s->regs[IBEX_SPI_HOST_INTR_ENABLE];
    uint32_t intr_state_reg = s->regs[IBEX_SPI_HOST_INTR_STATE];

    uint32_t err_en_reg = s->regs[IBEX_SPI_HOST_ERROR_ENABLE];
    uint32_t event_en_reg = s->regs[IBEX_SPI_HOST_EVENT_ENABLE];
    uint32_t err_status_reg = s->regs[IBEX_SPI_HOST_ERROR_STATUS];
    uint32_t status_reg = s->regs[IBEX_SPI_HOST_STATUS];


    bool error_en = FIELD_EX32(intr_en_reg, INTR_ENABLE, ERROR);
    bool event_en = FIELD_EX32(intr_en_reg, INTR_ENABLE, SPI_EVENT);
    bool err_pending = FIELD_EX32(intr_state_reg, INTR_STATE, ERROR);
    bool status_pending = FIELD_EX32(intr_state_reg, INTR_STATE, SPI_EVENT);

    int err_irq = 0, event_irq = 0;

    /* Error IRQ enabled and Error IRQ Cleared */
    if (error_en && !err_pending) {
        /* Event enabled, Interrupt Test Error */
        if (FIELD_EX32(intr_test_reg, INTR_TEST,  ERROR)) {
            err_irq = 1;
        } else if (FIELD_EX32(err_en_reg, ERROR_ENABLE,  CMDBUSY) &&
                   FIELD_EX32(err_status_reg, ERROR_STATUS,  CMDBUSY)) {
            /* Wrote to COMMAND when not READY */
            err_irq = 1;
        } else if (FIELD_EX32(err_en_reg, ERROR_ENABLE,  CMDINVAL)  &&
                   FIELD_EX32(err_status_reg, ERROR_STATUS,  CMDINVAL)) {
            /* Invalid command segment */
            err_irq = 1;
        } else if (FIELD_EX32(err_en_reg, ERROR_ENABLE,  CSIDINVAL) &&
                   FIELD_EX32(err_status_reg, ERROR_STATUS,  CSIDINVAL)) {
            /* Invalid value for CSID */
            err_irq = 1;
        }
        if (err_irq) {
            s->regs[IBEX_SPI_HOST_INTR_STATE] |= R_INTR_STATE_ERROR_MASK;
        }
    }

    qemu_set_irq(s->host_err, err_irq);

    /* Event IRQ Enabled and Event IRQ Cleared */
    if (event_en && !status_pending) {
        if (FIELD_EX32(intr_test_reg, INTR_STATE,  SPI_EVENT)) {
            /* Event enabled, Interrupt Test Event */
            event_irq = 1;
        } else if (FIELD_EX32(event_en_reg, EVENT_ENABLE,  READY) &&
                   FIELD_EX32(status_reg, STATUS, READY)) {
            /* SPI Host ready for next command */
            event_irq = 1;
        } else if (FIELD_EX32(event_en_reg, EVENT_ENABLE,  TXEMPTY) &&
                   FIELD_EX32(status_reg, STATUS,  TXEMPTY)) {
            /* SPI TXEMPTY, TXFIFO drained */
            event_irq = 1;
        } else if (FIELD_EX32(event_en_reg, EVENT_ENABLE,  RXFULL) &&
                   FIELD_EX32(status_reg, STATUS,  RXFULL)) {
            /* SPI RXFULL, RXFIFO  full */
            event_irq = 1;
        }
        if (event_irq) {
            s->regs[IBEX_SPI_HOST_INTR_STATE] |= R_INTR_STATE_SPI_EVENT_MASK;
        }
    }

    qemu_set_irq(s->event, event_irq);
}

static void ibex_spi_host_transfer(IbexSPIHostState *s)
{
    uint32_t rx, tx, data;
    /* Get num of one byte transfers */
    uint8_t segment_len = FIELD_EX32(s->regs[IBEX_SPI_HOST_COMMAND],
                                     COMMAND,  LEN);

    while (segment_len > 0) {
        if (fifo8_is_empty(&s->tx_fifo)) {
            /* Assert Stall */
            s->regs[IBEX_SPI_HOST_STATUS] |= R_STATUS_TXSTALL_MASK;
            break;
        } else if (fifo8_is_full(&s->rx_fifo)) {
            /* Assert Stall */
            s->regs[IBEX_SPI_HOST_STATUS] |= R_STATUS_RXSTALL_MASK;
            break;
        } else {
            tx = fifo8_pop(&s->tx_fifo);
        }

        rx = ssi_transfer(s->ssi, tx);

        trace_ibex_spi_host_transfer(tx, rx);

        if (!fifo8_is_full(&s->rx_fifo)) {
            fifo8_push(&s->rx_fifo, rx);
        } else {
            /* Assert RXFULL */
            s->regs[IBEX_SPI_HOST_STATUS] |= R_STATUS_RXFULL_MASK;
        }
        --segment_len;
    }

    data = s->regs[IBEX_SPI_HOST_STATUS];
    /* Assert Ready */
    data = FIELD_DP32(data, STATUS, READY, 1);
    /* Set RXQD */
    data = FIELD_DP32(data, STATUS, RXQD, div4_round_up(segment_len));
    /* Set TXQD */
    data = FIELD_DP32(data, STATUS, TXQD, fifo8_num_used(&s->tx_fifo) / 4);
    /* Clear TXFULL */
    data = FIELD_DP32(data, STATUS, TXFULL, 0);
    /* Reset RXEMPTY */
    data = FIELD_DP32(data, STATUS, RXEMPTY, 0);
    /* Update register status */
    s->regs[IBEX_SPI_HOST_STATUS] = data;
    /* Drop remaining bytes that exceed segment_len */
    ibex_spi_txfifo_reset(s);

    ibex_spi_host_irq(s);
}

static uint64_t ibex_spi_host_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    IbexSPIHostState *s = opaque;
    uint32_t rc = 0;
    uint8_t rx_byte = 0;

    trace_ibex_spi_host_read(addr, size);

    /* Match reg index */
    addr = addr >> 2;
    switch (addr) {
    /* Skipping any W/O registers */
    case IBEX_SPI_HOST_INTR_STATE...IBEX_SPI_HOST_INTR_ENABLE:
    case IBEX_SPI_HOST_CONTROL...IBEX_SPI_HOST_STATUS:
        rc = s->regs[addr];
        break;
    case IBEX_SPI_HOST_CSID:
        rc = s->regs[addr];
        break;
    case IBEX_SPI_HOST_CONFIGOPTS:
        rc = s->config_opts[s->regs[IBEX_SPI_HOST_CSID]];
        break;
    case IBEX_SPI_HOST_TXDATA:
        rc = s->regs[addr];
        break;
    case IBEX_SPI_HOST_RXDATA:
        /* Clear RXFULL */
        s->regs[IBEX_SPI_HOST_STATUS] &= ~R_STATUS_RXFULL_MASK;

        for (int i = 0; i < 4; ++i) {
            if (fifo8_is_empty(&s->rx_fifo)) {
                /* Assert RXEMPTY, no IRQ */
                s->regs[IBEX_SPI_HOST_STATUS] |= R_STATUS_RXEMPTY_MASK;
                s->regs[IBEX_SPI_HOST_ERROR_STATUS] |=
                                                R_ERROR_STATUS_UNDERFLOW_MASK;
                return rc;
            }
            rx_byte = fifo8_pop(&s->rx_fifo);
            rc |= rx_byte << (i * 8);
        }
        break;
    case IBEX_SPI_HOST_ERROR_ENABLE...IBEX_SPI_HOST_EVENT_ENABLE:
        rc = s->regs[addr];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Bad offset 0x%" HWADDR_PRIx "\n",
                      addr << 2);
    }
    return rc;
}


static void ibex_spi_host_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    IbexSPIHostState *s = opaque;
    uint32_t val32 = val64;
    uint32_t shift_mask = 0xff, status = 0, data = 0;
    uint8_t txqd_len;

    trace_ibex_spi_host_write(addr, size, val64);

    /* Match reg index */
    addr = addr >> 2;

    switch (addr) {
    /* Skipping any R/O registers */
    case IBEX_SPI_HOST_INTR_STATE:
        /* rw1c status register */
        if (FIELD_EX32(val32, INTR_STATE, ERROR)) {
            data = FIELD_DP32(data, INTR_STATE, ERROR, 0);
        }
        if (FIELD_EX32(val32, INTR_STATE, SPI_EVENT)) {
            data = FIELD_DP32(data, INTR_STATE, SPI_EVENT, 0);
        }
        s->regs[addr] = data;
        break;
    case IBEX_SPI_HOST_INTR_ENABLE:
        s->regs[addr] = val32;
        break;
    case IBEX_SPI_HOST_INTR_TEST:
        s->regs[addr] = val32;
        ibex_spi_host_irq(s);
        break;
    case IBEX_SPI_HOST_ALERT_TEST:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                        "%s: SPI_ALERT_TEST is not supported\n", __func__);
        break;
    case IBEX_SPI_HOST_CONTROL:
        s->regs[addr] = val32;

        if (val32 & R_CONTROL_SW_RST_MASK)  {
            ibex_spi_host_reset((DeviceState *)s);
            /* Clear active if any */
            s->regs[IBEX_SPI_HOST_STATUS] &=  ~R_STATUS_ACTIVE_MASK;
        }

        if (val32 & R_CONTROL_OUTPUT_EN_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: CONTROL_OUTPUT_EN is not supported\n", __func__);
        }
        break;
    case IBEX_SPI_HOST_CONFIGOPTS:
        /* Update the respective config-opts register based on CSIDth index */
        s->config_opts[s->regs[IBEX_SPI_HOST_CSID]] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: CONFIGOPTS Hardware settings not supported\n",
                         __func__);
        break;
    case IBEX_SPI_HOST_CSID:
        if (val32 >= s->num_cs) {
            /* CSID exceeds max num_cs */
            s->regs[IBEX_SPI_HOST_ERROR_STATUS] |=
                                                R_ERROR_STATUS_CSIDINVAL_MASK;
            ibex_spi_host_irq(s);
            return;
        }
        s->regs[addr] = val32;
        break;
    case IBEX_SPI_HOST_COMMAND:
        s->regs[addr] = val32;

        /* STALL, IP not enabled */
        if (!(FIELD_EX32(s->regs[IBEX_SPI_HOST_CONTROL],
                         CONTROL, SPIEN))) {
            return;
        }

        /* SPI not ready, IRQ Error */
        if (!(FIELD_EX32(s->regs[IBEX_SPI_HOST_STATUS],
                         STATUS, READY))) {
            s->regs[IBEX_SPI_HOST_ERROR_STATUS] |= R_ERROR_STATUS_CMDBUSY_MASK;
            ibex_spi_host_irq(s);
            return;
        }

        /* Assert Not Ready */
        s->regs[IBEX_SPI_HOST_STATUS] &= ~R_STATUS_READY_MASK;

        if (FIELD_EX32(val32, COMMAND, DIRECTION) != BIDIRECTIONAL_TRANSFER) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Rx Only/Tx Only are not supported\n", __func__);
        }

        if (val32 & R_COMMAND_CSAAT_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: CSAAT is not supported\n", __func__);
        }
        if (val32 & R_COMMAND_SPEED_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: SPEED is not supported\n", __func__);
        }

        /* Set Transfer Callback */
        timer_mod(s->fifo_trigger_handle,
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                    (TX_INTERRUPT_TRIGGER_DELAY_NS));

        break;
    case IBEX_SPI_HOST_TXDATA:
        /*
         * This is a hardware `feature` where
         * the first word written to TXDATA after init is omitted entirely
         */
        if (s->init_status) {
            s->init_status = false;
            return;
        }

        for (int i = 0; i < 4; ++i) {
            /* Attempting to write when TXFULL */
            if (fifo8_is_full(&s->tx_fifo)) {
                /* Assert RXEMPTY, no IRQ */
                s->regs[IBEX_SPI_HOST_STATUS] |= R_STATUS_TXFULL_MASK;
                s->regs[IBEX_SPI_HOST_ERROR_STATUS] |=
                                                 R_ERROR_STATUS_OVERFLOW_MASK;
                ibex_spi_host_irq(s);
                return;
            }
            /* Byte ordering is set by the IP */
            status = s->regs[IBEX_SPI_HOST_STATUS];
            if (FIELD_EX32(status, STATUS, BYTEORDER) == 0) {
                /* LE: LSB transmitted first (default for ibex processor) */
                shift_mask = 0xff << (i * 8);
            } else {
                /* BE: MSB transmitted first */
                qemu_log_mask(LOG_UNIMP,
                             "%s: Big endian is not supported\n", __func__);
            }

            fifo8_push(&s->tx_fifo, (val32 & shift_mask) >> (i * 8));
        }
        status = s->regs[IBEX_SPI_HOST_STATUS];
        /* Reset TXEMPTY */
        status = FIELD_DP32(status, STATUS, TXEMPTY, 0);
        /* Update TXQD */
        txqd_len = FIELD_EX32(status, STATUS, TXQD);
        /* Partial bytes (size < 4) are padded, in words. */
        txqd_len += 1;
        status = FIELD_DP32(status, STATUS, TXQD, txqd_len);
        /* Assert Ready */
        status = FIELD_DP32(status, STATUS, READY, 1);
        /* Update register status */
        s->regs[IBEX_SPI_HOST_STATUS] = status;
        break;
    case IBEX_SPI_HOST_ERROR_ENABLE:
        s->regs[addr] = val32;

        if (val32 & R_ERROR_ENABLE_CMDINVAL_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Segment Length is not supported\n", __func__);
        }
        break;
    case IBEX_SPI_HOST_ERROR_STATUS:
    /*
     *  Indicates any errors that have occurred.
     *  When an error occurs, the corresponding bit must be cleared
     *  here before issuing any further commands
     */
        status = s->regs[addr];
        /* rw1c status register */
        if (FIELD_EX32(val32, ERROR_STATUS, CMDBUSY)) {
            status = FIELD_DP32(status, ERROR_STATUS, CMDBUSY, 0);
        }
        if (FIELD_EX32(val32, ERROR_STATUS, OVERFLOW)) {
            status = FIELD_DP32(status, ERROR_STATUS, OVERFLOW, 0);
        }
        if (FIELD_EX32(val32, ERROR_STATUS, UNDERFLOW)) {
            status = FIELD_DP32(status, ERROR_STATUS, UNDERFLOW, 0);
        }
        if (FIELD_EX32(val32, ERROR_STATUS, CMDINVAL)) {
            status = FIELD_DP32(status, ERROR_STATUS, CMDINVAL, 0);
        }
        if (FIELD_EX32(val32, ERROR_STATUS, CSIDINVAL)) {
            status = FIELD_DP32(status, ERROR_STATUS, CSIDINVAL, 0);
        }
        if (FIELD_EX32(val32, ERROR_STATUS, ACCESSINVAL)) {
            status = FIELD_DP32(status, ERROR_STATUS, ACCESSINVAL, 0);
        }
        s->regs[addr] = status;
        break;
    case IBEX_SPI_HOST_EVENT_ENABLE:
    /* Controls which classes of SPI events raise an interrupt. */
        s->regs[addr] = val32;

        if (val32 & R_EVENT_ENABLE_RXWM_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: RXWM is not supported\n", __func__);
        }
        if (val32 & R_EVENT_ENABLE_TXWM_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: TXWM is not supported\n", __func__);
        }

        if (val32 & R_EVENT_ENABLE_IDLE_MASK)  {
            qemu_log_mask(LOG_UNIMP,
                          "%s: IDLE is not supported\n", __func__);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Bad offset 0x%" HWADDR_PRIx "\n",
                      addr << 2);
    }
}

static const MemoryRegionOps ibex_spi_ops = {
    .read = ibex_spi_host_read,
    .write = ibex_spi_host_write,
    /* Ibex default LE */
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const Property ibex_spi_properties[] = {
    DEFINE_PROP_UINT32("num_cs", IbexSPIHostState, num_cs, 1),
};

static const VMStateDescription vmstate_ibex = {
    .name = TYPE_IBEX_SPI_HOST,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IbexSPIHostState, IBEX_SPI_HOST_MAX_REGS),
        VMSTATE_VARRAY_UINT32(config_opts, IbexSPIHostState,
                              num_cs, 0, vmstate_info_uint32, uint32_t),
        VMSTATE_FIFO8(rx_fifo, IbexSPIHostState),
        VMSTATE_FIFO8(tx_fifo, IbexSPIHostState),
        VMSTATE_TIMER_PTR(fifo_trigger_handle, IbexSPIHostState),
        VMSTATE_BOOL(init_status, IbexSPIHostState),
        VMSTATE_END_OF_LIST()
    }
};

static void fifo_trigger_update(void *opaque)
{
    IbexSPIHostState *s = opaque;
    ibex_spi_host_transfer(s);
}

static void ibex_spi_host_realize(DeviceState *dev, Error **errp)
{
    IbexSPIHostState *s = IBEX_SPI_HOST(dev);
    int i;

    s->ssi = ssi_create_bus(dev, "ssi");
    s->cs_lines = g_new0(qemu_irq, s->num_cs);

    for (i = 0; i < s->num_cs; ++i) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->cs_lines[i]);
    }

    /* Setup CONFIGOPTS Multi-register */
    s->config_opts = g_new0(uint32_t, s->num_cs);

    /* Setup FIFO Interrupt Timer */
    s->fifo_trigger_handle = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          fifo_trigger_update, s);

    /* FIFO sizes as per OT Spec */
    fifo8_create(&s->tx_fifo, IBEX_SPI_HOST_TXFIFO_LEN);
    fifo8_create(&s->rx_fifo, IBEX_SPI_HOST_RXFIFO_LEN);
}

static void ibex_spi_host_init(Object *obj)
{
    IbexSPIHostState *s = IBEX_SPI_HOST(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->host_err);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->event);

    memory_region_init_io(&s->mmio, obj, &ibex_spi_ops, s,
                          TYPE_IBEX_SPI_HOST, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void ibex_spi_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = ibex_spi_host_realize;
    device_class_set_legacy_reset(dc, ibex_spi_host_reset);
    dc->vmsd = &vmstate_ibex;
    device_class_set_props(dc, ibex_spi_properties);
}

static const TypeInfo ibex_spi_host_info = {
    .name          = TYPE_IBEX_SPI_HOST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbexSPIHostState),
    .instance_init = ibex_spi_host_init,
    .class_init    = ibex_spi_host_class_init,
};

static void ibex_spi_host_register_types(void)
{
    type_register_static(&ibex_spi_host_info);
}

type_init(ibex_spi_host_register_types)
