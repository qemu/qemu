/*
 * Nuvoton NPCM7xx SMBus Module.
 *
 * Copyright 2020 Google LLC
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

#include "qemu/osdep.h"

#include "hw/i2c/npcm7xx_smbus.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#include "trace.h"

enum NPCM7xxSMBusCommonRegister {
    NPCM7XX_SMB_SDA     = 0x0,
    NPCM7XX_SMB_ST      = 0x2,
    NPCM7XX_SMB_CST     = 0x4,
    NPCM7XX_SMB_CTL1    = 0x6,
    NPCM7XX_SMB_ADDR1   = 0x8,
    NPCM7XX_SMB_CTL2    = 0xa,
    NPCM7XX_SMB_ADDR2   = 0xc,
    NPCM7XX_SMB_CTL3    = 0xe,
    NPCM7XX_SMB_CST2    = 0x18,
    NPCM7XX_SMB_CST3    = 0x19,
    NPCM7XX_SMB_VER     = 0x1f,
};

enum NPCM7xxSMBusBank0Register {
    NPCM7XX_SMB_ADDR3   = 0x10,
    NPCM7XX_SMB_ADDR7   = 0x11,
    NPCM7XX_SMB_ADDR4   = 0x12,
    NPCM7XX_SMB_ADDR8   = 0x13,
    NPCM7XX_SMB_ADDR5   = 0x14,
    NPCM7XX_SMB_ADDR9   = 0x15,
    NPCM7XX_SMB_ADDR6   = 0x16,
    NPCM7XX_SMB_ADDR10  = 0x17,
    NPCM7XX_SMB_CTL4    = 0x1a,
    NPCM7XX_SMB_CTL5    = 0x1b,
    NPCM7XX_SMB_SCLLT   = 0x1c,
    NPCM7XX_SMB_FIF_CTL = 0x1d,
    NPCM7XX_SMB_SCLHT   = 0x1e,
};

enum NPCM7xxSMBusBank1Register {
    NPCM7XX_SMB_FIF_CTS  = 0x10,
    NPCM7XX_SMB_FAIR_PER = 0x11,
    NPCM7XX_SMB_TXF_CTL  = 0x12,
    NPCM7XX_SMB_T_OUT    = 0x14,
    NPCM7XX_SMB_TXF_STS  = 0x1a,
    NPCM7XX_SMB_RXF_STS  = 0x1c,
    NPCM7XX_SMB_RXF_CTL  = 0x1e,
};

/* ST fields */
#define NPCM7XX_SMBST_STP           BIT(7)
#define NPCM7XX_SMBST_SDAST         BIT(6)
#define NPCM7XX_SMBST_BER           BIT(5)
#define NPCM7XX_SMBST_NEGACK        BIT(4)
#define NPCM7XX_SMBST_STASTR        BIT(3)
#define NPCM7XX_SMBST_NMATCH        BIT(2)
#define NPCM7XX_SMBST_MODE          BIT(1)
#define NPCM7XX_SMBST_XMIT          BIT(0)

/* CST fields */
#define NPCM7XX_SMBCST_ARPMATCH        BIT(7)
#define NPCM7XX_SMBCST_MATCHAF         BIT(6)
#define NPCM7XX_SMBCST_TGSCL           BIT(5)
#define NPCM7XX_SMBCST_TSDA            BIT(4)
#define NPCM7XX_SMBCST_GCMATCH         BIT(3)
#define NPCM7XX_SMBCST_MATCH           BIT(2)
#define NPCM7XX_SMBCST_BB              BIT(1)
#define NPCM7XX_SMBCST_BUSY            BIT(0)

/* CST2 fields */
#define NPCM7XX_SMBCST2_INTSTS         BIT(7)
#define NPCM7XX_SMBCST2_MATCH7F        BIT(6)
#define NPCM7XX_SMBCST2_MATCH6F        BIT(5)
#define NPCM7XX_SMBCST2_MATCH5F        BIT(4)
#define NPCM7XX_SMBCST2_MATCH4F        BIT(3)
#define NPCM7XX_SMBCST2_MATCH3F        BIT(2)
#define NPCM7XX_SMBCST2_MATCH2F        BIT(1)
#define NPCM7XX_SMBCST2_MATCH1F        BIT(0)

/* CST3 fields */
#define NPCM7XX_SMBCST3_EO_BUSY        BIT(7)
#define NPCM7XX_SMBCST3_MATCH10F       BIT(2)
#define NPCM7XX_SMBCST3_MATCH9F        BIT(1)
#define NPCM7XX_SMBCST3_MATCH8F        BIT(0)

/* CTL1 fields */
#define NPCM7XX_SMBCTL1_STASTRE     BIT(7)
#define NPCM7XX_SMBCTL1_NMINTE      BIT(6)
#define NPCM7XX_SMBCTL1_GCMEN       BIT(5)
#define NPCM7XX_SMBCTL1_ACK         BIT(4)
#define NPCM7XX_SMBCTL1_EOBINTE     BIT(3)
#define NPCM7XX_SMBCTL1_INTEN       BIT(2)
#define NPCM7XX_SMBCTL1_STOP        BIT(1)
#define NPCM7XX_SMBCTL1_START       BIT(0)

/* CTL2 fields */
#define NPCM7XX_SMBCTL2_SCLFRQ(rv)  extract8((rv), 1, 6)
#define NPCM7XX_SMBCTL2_ENABLE      BIT(0)

/* CTL3 fields */
#define NPCM7XX_SMBCTL3_SCL_LVL     BIT(7)
#define NPCM7XX_SMBCTL3_SDA_LVL     BIT(6)
#define NPCM7XX_SMBCTL3_BNK_SEL     BIT(5)
#define NPCM7XX_SMBCTL3_400K_MODE   BIT(4)
#define NPCM7XX_SMBCTL3_IDL_START   BIT(3)
#define NPCM7XX_SMBCTL3_ARPMEN      BIT(2)
#define NPCM7XX_SMBCTL3_SCLFRQ(rv)  extract8((rv), 0, 2)

/* ADDR fields */
#define NPCM7XX_ADDR_EN             BIT(7)
#define NPCM7XX_ADDR_A(rv)          extract8((rv), 0, 6)

/* FIFO Mode Register Fields */
/* FIF_CTL fields */
#define NPCM7XX_SMBFIF_CTL_FIFO_EN          BIT(4)
#define NPCM7XX_SMBFIF_CTL_FAIR_RDY_IE      BIT(2)
#define NPCM7XX_SMBFIF_CTL_FAIR_RDY         BIT(1)
#define NPCM7XX_SMBFIF_CTL_FAIR_BUSY        BIT(0)
/* FIF_CTS fields */
#define NPCM7XX_SMBFIF_CTS_STR              BIT(7)
#define NPCM7XX_SMBFIF_CTS_CLR_FIFO         BIT(6)
#define NPCM7XX_SMBFIF_CTS_RFTE_IE          BIT(3)
#define NPCM7XX_SMBFIF_CTS_RXF_TXE          BIT(1)
/* TXF_CTL fields */
#define NPCM7XX_SMBTXF_CTL_THR_TXIE         BIT(6)
#define NPCM7XX_SMBTXF_CTL_TX_THR(rv)       extract8((rv), 0, 5)
/* T_OUT fields */
#define NPCM7XX_SMBT_OUT_ST                 BIT(7)
#define NPCM7XX_SMBT_OUT_IE                 BIT(6)
#define NPCM7XX_SMBT_OUT_CLKDIV(rv)         extract8((rv), 0, 6)
/* TXF_STS fields */
#define NPCM7XX_SMBTXF_STS_TX_THST          BIT(6)
#define NPCM7XX_SMBTXF_STS_TX_BYTES(rv)     extract8((rv), 0, 5)
/* RXF_STS fields */
#define NPCM7XX_SMBRXF_STS_RX_THST          BIT(6)
#define NPCM7XX_SMBRXF_STS_RX_BYTES(rv)     extract8((rv), 0, 5)
/* RXF_CTL fields */
#define NPCM7XX_SMBRXF_CTL_THR_RXIE         BIT(6)
#define NPCM7XX_SMBRXF_CTL_LAST             BIT(5)
#define NPCM7XX_SMBRXF_CTL_RX_THR(rv)       extract8((rv), 0, 5)

#define KEEP_OLD_BIT(o, n, b)       (((n) & (~(b))) | ((o) & (b)))
#define WRITE_ONE_CLEAR(o, n, b)    ((n) & (b) ? (o) & (~(b)) : (o))

#define NPCM7XX_SMBUS_ENABLED(s)    ((s)->ctl2 & NPCM7XX_SMBCTL2_ENABLE)
#define NPCM7XX_SMBUS_FIFO_ENABLED(s) ((s)->fif_ctl & \
                                       NPCM7XX_SMBFIF_CTL_FIFO_EN)

/* VERSION fields values, read-only. */
#define NPCM7XX_SMBUS_VERSION_NUMBER 1
#define NPCM7XX_SMBUS_VERSION_FIFO_SUPPORTED 1

/* Reset values */
#define NPCM7XX_SMB_ST_INIT_VAL     0x00
#define NPCM7XX_SMB_CST_INIT_VAL    0x10
#define NPCM7XX_SMB_CST2_INIT_VAL   0x00
#define NPCM7XX_SMB_CST3_INIT_VAL   0x00
#define NPCM7XX_SMB_CTL1_INIT_VAL   0x00
#define NPCM7XX_SMB_CTL2_INIT_VAL   0x00
#define NPCM7XX_SMB_CTL3_INIT_VAL   0xc0
#define NPCM7XX_SMB_CTL4_INIT_VAL   0x07
#define NPCM7XX_SMB_CTL5_INIT_VAL   0x00
#define NPCM7XX_SMB_ADDR_INIT_VAL   0x00
#define NPCM7XX_SMB_SCLLT_INIT_VAL  0x00
#define NPCM7XX_SMB_SCLHT_INIT_VAL  0x00
#define NPCM7XX_SMB_FIF_CTL_INIT_VAL 0x00
#define NPCM7XX_SMB_FIF_CTS_INIT_VAL 0x00
#define NPCM7XX_SMB_FAIR_PER_INIT_VAL 0x00
#define NPCM7XX_SMB_TXF_CTL_INIT_VAL 0x00
#define NPCM7XX_SMB_T_OUT_INIT_VAL 0x3f
#define NPCM7XX_SMB_TXF_STS_INIT_VAL 0x00
#define NPCM7XX_SMB_RXF_STS_INIT_VAL 0x00
#define NPCM7XX_SMB_RXF_CTL_INIT_VAL 0x01

static uint8_t npcm7xx_smbus_get_version(void)
{
    return NPCM7XX_SMBUS_VERSION_FIFO_SUPPORTED << 7 |
           NPCM7XX_SMBUS_VERSION_NUMBER;
}

static void npcm7xx_smbus_update_irq(NPCM7xxSMBusState *s)
{
    int level;

    if (s->ctl1 & NPCM7XX_SMBCTL1_INTEN) {
        level = !!((s->ctl1 & NPCM7XX_SMBCTL1_NMINTE &&
                    s->st & NPCM7XX_SMBST_NMATCH) ||
                   (s->st & NPCM7XX_SMBST_BER) ||
                   (s->st & NPCM7XX_SMBST_NEGACK) ||
                   (s->st & NPCM7XX_SMBST_SDAST) ||
                   (s->ctl1 & NPCM7XX_SMBCTL1_STASTRE &&
                    s->st & NPCM7XX_SMBST_SDAST) ||
                   (s->ctl1 & NPCM7XX_SMBCTL1_EOBINTE &&
                    s->cst3 & NPCM7XX_SMBCST3_EO_BUSY) ||
                   (s->rxf_ctl & NPCM7XX_SMBRXF_CTL_THR_RXIE &&
                    s->rxf_sts & NPCM7XX_SMBRXF_STS_RX_THST) ||
                   (s->txf_ctl & NPCM7XX_SMBTXF_CTL_THR_TXIE &&
                    s->txf_sts & NPCM7XX_SMBTXF_STS_TX_THST) ||
                   (s->fif_cts & NPCM7XX_SMBFIF_CTS_RFTE_IE &&
                    s->fif_cts & NPCM7XX_SMBFIF_CTS_RXF_TXE));

        if (level) {
            s->cst2 |= NPCM7XX_SMBCST2_INTSTS;
        } else {
            s->cst2 &= ~NPCM7XX_SMBCST2_INTSTS;
        }
        qemu_set_irq(s->irq, level);
    }
}

static void npcm7xx_smbus_nack(NPCM7xxSMBusState *s)
{
    s->st &= ~NPCM7XX_SMBST_SDAST;
    s->st |= NPCM7XX_SMBST_NEGACK;
    s->status = NPCM7XX_SMBUS_STATUS_NEGACK;
}

static void npcm7xx_smbus_clear_buffer(NPCM7xxSMBusState *s)
{
    s->fif_cts &= ~NPCM7XX_SMBFIF_CTS_RXF_TXE;
    s->txf_sts = 0;
    s->rxf_sts = 0;
}

static void npcm7xx_smbus_send_byte(NPCM7xxSMBusState *s, uint8_t value)
{
    int rv = i2c_send(s->bus, value);

    if (rv) {
        npcm7xx_smbus_nack(s);
    } else {
        s->st |= NPCM7XX_SMBST_SDAST;
        if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
            s->fif_cts |= NPCM7XX_SMBFIF_CTS_RXF_TXE;
            if (NPCM7XX_SMBTXF_STS_TX_BYTES(s->txf_sts) ==
                NPCM7XX_SMBTXF_CTL_TX_THR(s->txf_ctl)) {
                s->txf_sts = NPCM7XX_SMBTXF_STS_TX_THST;
            } else {
                s->txf_sts = 0;
            }
        }
    }
    trace_npcm7xx_smbus_send_byte((DEVICE(s)->canonical_path), value, !rv);
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_recv_byte(NPCM7xxSMBusState *s)
{
    s->sda = i2c_recv(s->bus);
    s->st |= NPCM7XX_SMBST_SDAST;
    if (s->st & NPCM7XX_SMBCTL1_ACK) {
        trace_npcm7xx_smbus_nack(DEVICE(s)->canonical_path);
        i2c_nack(s->bus);
        s->st &= NPCM7XX_SMBCTL1_ACK;
    }
    trace_npcm7xx_smbus_recv_byte((DEVICE(s)->canonical_path), s->sda);
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_recv_fifo(NPCM7xxSMBusState *s)
{
    uint8_t expected_bytes = NPCM7XX_SMBRXF_CTL_RX_THR(s->rxf_ctl);
    uint8_t received_bytes = NPCM7XX_SMBRXF_STS_RX_BYTES(s->rxf_sts);
    uint8_t pos;

    if (received_bytes == expected_bytes) {
        return;
    }

    while (received_bytes < expected_bytes &&
           received_bytes < NPCM7XX_SMBUS_FIFO_SIZE) {
        pos = (s->rx_cur + received_bytes) % NPCM7XX_SMBUS_FIFO_SIZE;
        s->rx_fifo[pos] = i2c_recv(s->bus);
        trace_npcm7xx_smbus_recv_byte((DEVICE(s)->canonical_path),
                                      s->rx_fifo[pos]);
        ++received_bytes;
    }

    trace_npcm7xx_smbus_recv_fifo((DEVICE(s)->canonical_path),
                                  received_bytes, expected_bytes);
    s->rxf_sts = received_bytes;
    if (unlikely(received_bytes < expected_bytes)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid rx_thr value: 0x%02x\n",
                      DEVICE(s)->canonical_path, expected_bytes);
        return;
    }

    s->rxf_sts |= NPCM7XX_SMBRXF_STS_RX_THST;
    if (s->rxf_ctl & NPCM7XX_SMBRXF_CTL_LAST) {
        trace_npcm7xx_smbus_nack(DEVICE(s)->canonical_path);
        i2c_nack(s->bus);
        s->rxf_ctl &= ~NPCM7XX_SMBRXF_CTL_LAST;
    }
    if (received_bytes == NPCM7XX_SMBUS_FIFO_SIZE) {
        s->st |= NPCM7XX_SMBST_SDAST;
        s->fif_cts |= NPCM7XX_SMBFIF_CTS_RXF_TXE;
    } else if (!(s->rxf_ctl & NPCM7XX_SMBRXF_CTL_THR_RXIE)) {
        s->st |= NPCM7XX_SMBST_SDAST;
    } else {
        s->st &= ~NPCM7XX_SMBST_SDAST;
    }
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_read_byte_fifo(NPCM7xxSMBusState *s)
{
    uint8_t received_bytes = NPCM7XX_SMBRXF_STS_RX_BYTES(s->rxf_sts);

    if (received_bytes == 0) {
        npcm7xx_smbus_recv_fifo(s);
        return;
    }

    s->sda = s->rx_fifo[s->rx_cur];
    s->rx_cur = (s->rx_cur + 1u) % NPCM7XX_SMBUS_FIFO_SIZE;
    --s->rxf_sts;
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_start(NPCM7xxSMBusState *s)
{
    /*
     * We can start the bus if one of these is true:
     * 1. The bus is idle (so we can request it)
     * 2. We are the occupier (it's a repeated start condition.)
     */
    int available = !i2c_bus_busy(s->bus) ||
                    s->status != NPCM7XX_SMBUS_STATUS_IDLE;

    if (available) {
        s->st |= NPCM7XX_SMBST_MODE | NPCM7XX_SMBST_XMIT | NPCM7XX_SMBST_SDAST;
        s->cst |= NPCM7XX_SMBCST_BUSY;
        if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
            s->fif_cts |= NPCM7XX_SMBFIF_CTS_RXF_TXE;
        }
    } else {
        s->st &= ~NPCM7XX_SMBST_MODE;
        s->cst &= ~NPCM7XX_SMBCST_BUSY;
        s->st |= NPCM7XX_SMBST_BER;
    }

    trace_npcm7xx_smbus_start(DEVICE(s)->canonical_path, available);
    s->cst |= NPCM7XX_SMBCST_BB;
    s->status = NPCM7XX_SMBUS_STATUS_IDLE;
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_send_address(NPCM7xxSMBusState *s, uint8_t value)
{
    int recv;
    int rv;

    recv = value & BIT(0);
    rv = i2c_start_transfer(s->bus, value >> 1, recv);
    trace_npcm7xx_smbus_send_address(DEVICE(s)->canonical_path,
                                     value >> 1, recv, !rv);
    if (rv) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: requesting i2c bus for 0x%02x failed: %d\n",
                      DEVICE(s)->canonical_path, value, rv);
        /* Failed to start transfer. NACK to reject.*/
        if (recv) {
            s->st &= ~NPCM7XX_SMBST_XMIT;
        } else {
            s->st |= NPCM7XX_SMBST_XMIT;
        }
        npcm7xx_smbus_nack(s);
        npcm7xx_smbus_update_irq(s);
        return;
    }

    s->st &= ~NPCM7XX_SMBST_NEGACK;
    if (recv) {
        s->status = NPCM7XX_SMBUS_STATUS_RECEIVING;
        s->st &= ~NPCM7XX_SMBST_XMIT;
    } else {
        s->status = NPCM7XX_SMBUS_STATUS_SENDING;
        s->st |= NPCM7XX_SMBST_XMIT;
    }

    if (s->ctl1 & NPCM7XX_SMBCTL1_STASTRE) {
        s->st |= NPCM7XX_SMBST_STASTR;
        if (!recv) {
            s->st |= NPCM7XX_SMBST_SDAST;
        }
    } else if (recv) {
        s->st |= NPCM7XX_SMBST_SDAST;
        if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
            npcm7xx_smbus_recv_fifo(s);
        } else {
            npcm7xx_smbus_recv_byte(s);
        }
    } else if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
        s->st |= NPCM7XX_SMBST_SDAST;
        s->fif_cts |= NPCM7XX_SMBFIF_CTS_RXF_TXE;
    }
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_execute_stop(NPCM7xxSMBusState *s)
{
    i2c_end_transfer(s->bus);
    s->st = 0;
    s->cst = 0;
    s->status = NPCM7XX_SMBUS_STATUS_IDLE;
    s->cst3 |= NPCM7XX_SMBCST3_EO_BUSY;
    trace_npcm7xx_smbus_stop(DEVICE(s)->canonical_path);
    npcm7xx_smbus_update_irq(s);
}


static void npcm7xx_smbus_stop(NPCM7xxSMBusState *s)
{
    if (s->st & NPCM7XX_SMBST_MODE) {
        switch (s->status) {
        case NPCM7XX_SMBUS_STATUS_RECEIVING:
        case NPCM7XX_SMBUS_STATUS_STOPPING_LAST_RECEIVE:
            s->status = NPCM7XX_SMBUS_STATUS_STOPPING_LAST_RECEIVE;
            break;

        case NPCM7XX_SMBUS_STATUS_NEGACK:
            s->status = NPCM7XX_SMBUS_STATUS_STOPPING_NEGACK;
            break;

        default:
            npcm7xx_smbus_execute_stop(s);
            break;
        }
    }
}

static uint8_t npcm7xx_smbus_read_sda(NPCM7xxSMBusState *s)
{
    uint8_t value = s->sda;

    switch (s->status) {
    case NPCM7XX_SMBUS_STATUS_STOPPING_LAST_RECEIVE:
        if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
            if (NPCM7XX_SMBRXF_STS_RX_BYTES(s->rxf_sts) <= 1) {
                npcm7xx_smbus_execute_stop(s);
            }
            if (NPCM7XX_SMBRXF_STS_RX_BYTES(s->rxf_sts) == 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: read to SDA with an empty rx-fifo buffer, "
                              "result undefined: %u\n",
                              DEVICE(s)->canonical_path, s->sda);
                break;
            }
            npcm7xx_smbus_read_byte_fifo(s);
            value = s->sda;
        } else {
            npcm7xx_smbus_execute_stop(s);
        }
        break;

    case NPCM7XX_SMBUS_STATUS_RECEIVING:
        if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
            npcm7xx_smbus_read_byte_fifo(s);
            value = s->sda;
        } else {
            npcm7xx_smbus_recv_byte(s);
        }
        break;

    default:
        /* Do nothing */
        break;
    }

    return value;
}

static void npcm7xx_smbus_write_sda(NPCM7xxSMBusState *s, uint8_t value)
{
    s->sda = value;
    if (s->st & NPCM7XX_SMBST_MODE) {
        switch (s->status) {
        case NPCM7XX_SMBUS_STATUS_IDLE:
            npcm7xx_smbus_send_address(s, value);
            break;
        case NPCM7XX_SMBUS_STATUS_SENDING:
            npcm7xx_smbus_send_byte(s, value);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to SDA in invalid status %d: %u\n",
                          DEVICE(s)->canonical_path, s->status, value);
            break;
        }
    }
}

static void npcm7xx_smbus_write_st(NPCM7xxSMBusState *s, uint8_t value)
{
    s->st = WRITE_ONE_CLEAR(s->st, value, NPCM7XX_SMBST_STP);
    s->st = WRITE_ONE_CLEAR(s->st, value, NPCM7XX_SMBST_BER);
    s->st = WRITE_ONE_CLEAR(s->st, value, NPCM7XX_SMBST_STASTR);
    s->st = WRITE_ONE_CLEAR(s->st, value, NPCM7XX_SMBST_NMATCH);

    if (value & NPCM7XX_SMBST_NEGACK) {
        s->st &= ~NPCM7XX_SMBST_NEGACK;
        if (s->status == NPCM7XX_SMBUS_STATUS_STOPPING_NEGACK) {
            npcm7xx_smbus_execute_stop(s);
        }
    }

    if (value & NPCM7XX_SMBST_STASTR &&
        s->status == NPCM7XX_SMBUS_STATUS_RECEIVING) {
        if (NPCM7XX_SMBUS_FIFO_ENABLED(s)) {
            npcm7xx_smbus_recv_fifo(s);
        } else {
            npcm7xx_smbus_recv_byte(s);
        }
    }

    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_write_cst(NPCM7xxSMBusState *s, uint8_t value)
{
    uint8_t new_value = s->cst;

    s->cst = WRITE_ONE_CLEAR(new_value, value, NPCM7XX_SMBCST_BB);
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_write_cst3(NPCM7xxSMBusState *s, uint8_t value)
{
    s->cst3 = WRITE_ONE_CLEAR(s->cst3, value, NPCM7XX_SMBCST3_EO_BUSY);
    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_write_ctl1(NPCM7xxSMBusState *s, uint8_t value)
{
    s->ctl1 = KEEP_OLD_BIT(s->ctl1, value,
            NPCM7XX_SMBCTL1_START | NPCM7XX_SMBCTL1_STOP | NPCM7XX_SMBCTL1_ACK);

    if (value & NPCM7XX_SMBCTL1_START) {
        npcm7xx_smbus_start(s);
    }

    if (value & NPCM7XX_SMBCTL1_STOP) {
        npcm7xx_smbus_stop(s);
    }

    npcm7xx_smbus_update_irq(s);
}

static void npcm7xx_smbus_write_ctl2(NPCM7xxSMBusState *s, uint8_t value)
{
    s->ctl2 = value;

    if (!NPCM7XX_SMBUS_ENABLED(s)) {
        /* Disable this SMBus module. */
        s->ctl1 = 0;
        s->st = 0;
        s->cst3 = s->cst3 & (~NPCM7XX_SMBCST3_EO_BUSY);
        s->cst = 0;
        npcm7xx_smbus_clear_buffer(s);
    }
}

static void npcm7xx_smbus_write_ctl3(NPCM7xxSMBusState *s, uint8_t value)
{
    uint8_t old_ctl3 = s->ctl3;

    /* Write to SDA and SCL bits are ignored. */
    s->ctl3 =  KEEP_OLD_BIT(old_ctl3, value,
                            NPCM7XX_SMBCTL3_SCL_LVL | NPCM7XX_SMBCTL3_SDA_LVL);
}

static void npcm7xx_smbus_write_fif_ctl(NPCM7xxSMBusState *s, uint8_t value)
{
    uint8_t new_ctl = value;

    new_ctl = KEEP_OLD_BIT(s->fif_ctl, new_ctl, NPCM7XX_SMBFIF_CTL_FAIR_RDY);
    new_ctl = WRITE_ONE_CLEAR(new_ctl, value, NPCM7XX_SMBFIF_CTL_FAIR_RDY);
    new_ctl = KEEP_OLD_BIT(s->fif_ctl, new_ctl, NPCM7XX_SMBFIF_CTL_FAIR_BUSY);
    s->fif_ctl = new_ctl;
}

static void npcm7xx_smbus_write_fif_cts(NPCM7xxSMBusState *s, uint8_t value)
{
    s->fif_cts = WRITE_ONE_CLEAR(s->fif_cts, value, NPCM7XX_SMBFIF_CTS_STR);
    s->fif_cts = WRITE_ONE_CLEAR(s->fif_cts, value, NPCM7XX_SMBFIF_CTS_RXF_TXE);
    s->fif_cts = KEEP_OLD_BIT(value, s->fif_cts, NPCM7XX_SMBFIF_CTS_RFTE_IE);

    if (value & NPCM7XX_SMBFIF_CTS_CLR_FIFO) {
        npcm7xx_smbus_clear_buffer(s);
    }
}

static void npcm7xx_smbus_write_txf_ctl(NPCM7xxSMBusState *s, uint8_t value)
{
    s->txf_ctl = value;
}

static void npcm7xx_smbus_write_t_out(NPCM7xxSMBusState *s, uint8_t value)
{
    uint8_t new_t_out = value;

    if ((value & NPCM7XX_SMBT_OUT_ST) || (!(s->t_out & NPCM7XX_SMBT_OUT_ST))) {
        new_t_out &= ~NPCM7XX_SMBT_OUT_ST;
    } else {
        new_t_out |= NPCM7XX_SMBT_OUT_ST;
    }

    s->t_out = new_t_out;
}

static void npcm7xx_smbus_write_txf_sts(NPCM7xxSMBusState *s, uint8_t value)
{
    s->txf_sts = WRITE_ONE_CLEAR(s->txf_sts, value, NPCM7XX_SMBTXF_STS_TX_THST);
}

static void npcm7xx_smbus_write_rxf_sts(NPCM7xxSMBusState *s, uint8_t value)
{
    if (value & NPCM7XX_SMBRXF_STS_RX_THST) {
        s->rxf_sts &= ~NPCM7XX_SMBRXF_STS_RX_THST;
        if (s->status == NPCM7XX_SMBUS_STATUS_RECEIVING) {
            npcm7xx_smbus_recv_fifo(s);
        }
    }
}

static void npcm7xx_smbus_write_rxf_ctl(NPCM7xxSMBusState *s, uint8_t value)
{
    uint8_t new_ctl = value;

    if (!(value & NPCM7XX_SMBRXF_CTL_LAST)) {
        new_ctl = KEEP_OLD_BIT(s->rxf_ctl, new_ctl, NPCM7XX_SMBRXF_CTL_LAST);
    }
    s->rxf_ctl = new_ctl;
}

static uint64_t npcm7xx_smbus_read(void *opaque, hwaddr offset, unsigned size)
{
    NPCM7xxSMBusState *s = opaque;
    uint64_t value = 0;
    uint8_t bank = s->ctl3 & NPCM7XX_SMBCTL3_BNK_SEL;

    /* The order of the registers are their order in memory. */
    switch (offset) {
    case NPCM7XX_SMB_SDA:
        value = npcm7xx_smbus_read_sda(s);
        break;

    case NPCM7XX_SMB_ST:
        value = s->st;
        break;

    case NPCM7XX_SMB_CST:
        value = s->cst;
        break;

    case NPCM7XX_SMB_CTL1:
        value = s->ctl1;
        break;

    case NPCM7XX_SMB_ADDR1:
        value = s->addr[0];
        break;

    case NPCM7XX_SMB_CTL2:
        value = s->ctl2;
        break;

    case NPCM7XX_SMB_ADDR2:
        value = s->addr[1];
        break;

    case NPCM7XX_SMB_CTL3:
        value = s->ctl3;
        break;

    case NPCM7XX_SMB_CST2:
        value = s->cst2;
        break;

    case NPCM7XX_SMB_CST3:
        value = s->cst3;
        break;

    case NPCM7XX_SMB_VER:
        value = npcm7xx_smbus_get_version();
        break;

    /* This register is either invalid or banked at this point. */
    default:
        if (bank) {
            /* Bank 1 */
            switch (offset) {
            case NPCM7XX_SMB_FIF_CTS:
                value = s->fif_cts;
                break;

            case NPCM7XX_SMB_FAIR_PER:
                value = s->fair_per;
                break;

            case NPCM7XX_SMB_TXF_CTL:
                value = s->txf_ctl;
                break;

            case NPCM7XX_SMB_T_OUT:
                value = s->t_out;
                break;

            case NPCM7XX_SMB_TXF_STS:
                value = s->txf_sts;
                break;

            case NPCM7XX_SMB_RXF_STS:
                value = s->rxf_sts;
                break;

            case NPCM7XX_SMB_RXF_CTL:
                value = s->rxf_ctl;
                break;

            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                        DEVICE(s)->canonical_path, offset);
                break;
            }
        } else {
            /* Bank 0 */
            switch (offset) {
            case NPCM7XX_SMB_ADDR3:
                value = s->addr[2];
                break;

            case NPCM7XX_SMB_ADDR7:
                value = s->addr[6];
                break;

            case NPCM7XX_SMB_ADDR4:
                value = s->addr[3];
                break;

            case NPCM7XX_SMB_ADDR8:
                value = s->addr[7];
                break;

            case NPCM7XX_SMB_ADDR5:
                value = s->addr[4];
                break;

            case NPCM7XX_SMB_ADDR9:
                value = s->addr[8];
                break;

            case NPCM7XX_SMB_ADDR6:
                value = s->addr[5];
                break;

            case NPCM7XX_SMB_ADDR10:
                value = s->addr[9];
                break;

            case NPCM7XX_SMB_CTL4:
                value = s->ctl4;
                break;

            case NPCM7XX_SMB_CTL5:
                value = s->ctl5;
                break;

            case NPCM7XX_SMB_SCLLT:
                value = s->scllt;
                break;

            case NPCM7XX_SMB_FIF_CTL:
                value = s->fif_ctl;
                break;

            case NPCM7XX_SMB_SCLHT:
                value = s->sclht;
                break;

            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                        DEVICE(s)->canonical_path, offset);
                break;
            }
        }
        break;
    }

    trace_npcm7xx_smbus_read(DEVICE(s)->canonical_path, offset, value, size);

    return value;
}

static void npcm7xx_smbus_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    NPCM7xxSMBusState *s = opaque;
    uint8_t bank = s->ctl3 & NPCM7XX_SMBCTL3_BNK_SEL;

    trace_npcm7xx_smbus_write(DEVICE(s)->canonical_path, offset, value, size);

    /* The order of the registers are their order in memory. */
    switch (offset) {
    case NPCM7XX_SMB_SDA:
        npcm7xx_smbus_write_sda(s, value);
        break;

    case NPCM7XX_SMB_ST:
        npcm7xx_smbus_write_st(s, value);
        break;

    case NPCM7XX_SMB_CST:
        npcm7xx_smbus_write_cst(s, value);
        break;

    case NPCM7XX_SMB_CTL1:
        npcm7xx_smbus_write_ctl1(s, value);
        break;

    case NPCM7XX_SMB_ADDR1:
        s->addr[0] = value;
        break;

    case NPCM7XX_SMB_CTL2:
        npcm7xx_smbus_write_ctl2(s, value);
        break;

    case NPCM7XX_SMB_ADDR2:
        s->addr[1] = value;
        break;

    case NPCM7XX_SMB_CTL3:
        npcm7xx_smbus_write_ctl3(s, value);
        break;

    case NPCM7XX_SMB_CST2:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: write to read-only reg: offset 0x%" HWADDR_PRIx "\n",
                DEVICE(s)->canonical_path, offset);
        break;

    case NPCM7XX_SMB_CST3:
        npcm7xx_smbus_write_cst3(s, value);
        break;

    case NPCM7XX_SMB_VER:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: write to read-only reg: offset 0x%" HWADDR_PRIx "\n",
                DEVICE(s)->canonical_path, offset);
        break;

    /* This register is either invalid or banked at this point. */
    default:
        if (bank) {
            /* Bank 1 */
            switch (offset) {
            case NPCM7XX_SMB_FIF_CTS:
                npcm7xx_smbus_write_fif_cts(s, value);
                break;

            case NPCM7XX_SMB_FAIR_PER:
                s->fair_per = value;
                break;

            case NPCM7XX_SMB_TXF_CTL:
                npcm7xx_smbus_write_txf_ctl(s, value);
                break;

            case NPCM7XX_SMB_T_OUT:
                npcm7xx_smbus_write_t_out(s, value);
                break;

            case NPCM7XX_SMB_TXF_STS:
                npcm7xx_smbus_write_txf_sts(s, value);
                break;

            case NPCM7XX_SMB_RXF_STS:
                npcm7xx_smbus_write_rxf_sts(s, value);
                break;

            case NPCM7XX_SMB_RXF_CTL:
                npcm7xx_smbus_write_rxf_ctl(s, value);
                break;

            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                        DEVICE(s)->canonical_path, offset);
                break;
            }
        } else {
            /* Bank 0 */
            switch (offset) {
            case NPCM7XX_SMB_ADDR3:
                s->addr[2] = value;
                break;

            case NPCM7XX_SMB_ADDR7:
                s->addr[6] = value;
                break;

            case NPCM7XX_SMB_ADDR4:
                s->addr[3] = value;
                break;

            case NPCM7XX_SMB_ADDR8:
                s->addr[7] = value;
                break;

            case NPCM7XX_SMB_ADDR5:
                s->addr[4] = value;
                break;

            case NPCM7XX_SMB_ADDR9:
                s->addr[8] = value;
                break;

            case NPCM7XX_SMB_ADDR6:
                s->addr[5] = value;
                break;

            case NPCM7XX_SMB_ADDR10:
                s->addr[9] = value;
                break;

            case NPCM7XX_SMB_CTL4:
                s->ctl4 = value;
                break;

            case NPCM7XX_SMB_CTL5:
                s->ctl5 = value;
                break;

            case NPCM7XX_SMB_SCLLT:
                s->scllt = value;
                break;

            case NPCM7XX_SMB_FIF_CTL:
                npcm7xx_smbus_write_fif_ctl(s, value);
                break;

            case NPCM7XX_SMB_SCLHT:
                s->sclht = value;
                break;

            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                        DEVICE(s)->canonical_path, offset);
                break;
            }
        }
        break;
    }
}

static const MemoryRegionOps npcm7xx_smbus_ops = {
    .read = npcm7xx_smbus_read,
    .write = npcm7xx_smbus_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
        .unaligned = false,
    },
};

static void npcm7xx_smbus_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxSMBusState *s = NPCM7XX_SMBUS(obj);

    s->st = NPCM7XX_SMB_ST_INIT_VAL;
    s->cst = NPCM7XX_SMB_CST_INIT_VAL;
    s->cst2 = NPCM7XX_SMB_CST2_INIT_VAL;
    s->cst3 = NPCM7XX_SMB_CST3_INIT_VAL;
    s->ctl1 = NPCM7XX_SMB_CTL1_INIT_VAL;
    s->ctl2 = NPCM7XX_SMB_CTL2_INIT_VAL;
    s->ctl3 = NPCM7XX_SMB_CTL3_INIT_VAL;
    s->ctl4 = NPCM7XX_SMB_CTL4_INIT_VAL;
    s->ctl5 = NPCM7XX_SMB_CTL5_INIT_VAL;

    for (int i = 0; i < NPCM7XX_SMBUS_NR_ADDRS; ++i) {
        s->addr[i] = NPCM7XX_SMB_ADDR_INIT_VAL;
    }
    s->scllt = NPCM7XX_SMB_SCLLT_INIT_VAL;
    s->sclht = NPCM7XX_SMB_SCLHT_INIT_VAL;

    s->fif_ctl = NPCM7XX_SMB_FIF_CTL_INIT_VAL;
    s->fif_cts = NPCM7XX_SMB_FIF_CTS_INIT_VAL;
    s->fair_per = NPCM7XX_SMB_FAIR_PER_INIT_VAL;
    s->txf_ctl = NPCM7XX_SMB_TXF_CTL_INIT_VAL;
    s->t_out = NPCM7XX_SMB_T_OUT_INIT_VAL;
    s->txf_sts = NPCM7XX_SMB_TXF_STS_INIT_VAL;
    s->rxf_sts = NPCM7XX_SMB_RXF_STS_INIT_VAL;
    s->rxf_ctl = NPCM7XX_SMB_RXF_CTL_INIT_VAL;

    npcm7xx_smbus_clear_buffer(s);
    s->status = NPCM7XX_SMBUS_STATUS_IDLE;
    s->rx_cur = 0;
}

static void npcm7xx_smbus_hold_reset(Object *obj)
{
    NPCM7xxSMBusState *s = NPCM7XX_SMBUS(obj);

    qemu_irq_lower(s->irq);
}

static void npcm7xx_smbus_init(Object *obj)
{
    NPCM7xxSMBusState *s = NPCM7XX_SMBUS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, obj, &npcm7xx_smbus_ops, s,
                          "regs", 4 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);

    s->bus = i2c_init_bus(DEVICE(s), "i2c-bus");
}

static const VMStateDescription vmstate_npcm7xx_smbus = {
    .name = "npcm7xx-smbus",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(sda, NPCM7xxSMBusState),
        VMSTATE_UINT8(st, NPCM7xxSMBusState),
        VMSTATE_UINT8(cst, NPCM7xxSMBusState),
        VMSTATE_UINT8(cst2, NPCM7xxSMBusState),
        VMSTATE_UINT8(cst3, NPCM7xxSMBusState),
        VMSTATE_UINT8(ctl1, NPCM7xxSMBusState),
        VMSTATE_UINT8(ctl2, NPCM7xxSMBusState),
        VMSTATE_UINT8(ctl3, NPCM7xxSMBusState),
        VMSTATE_UINT8(ctl4, NPCM7xxSMBusState),
        VMSTATE_UINT8(ctl5, NPCM7xxSMBusState),
        VMSTATE_UINT8_ARRAY(addr, NPCM7xxSMBusState, NPCM7XX_SMBUS_NR_ADDRS),
        VMSTATE_UINT8(scllt, NPCM7xxSMBusState),
        VMSTATE_UINT8(sclht, NPCM7xxSMBusState),
        VMSTATE_UINT8(fif_ctl, NPCM7xxSMBusState),
        VMSTATE_UINT8(fif_cts, NPCM7xxSMBusState),
        VMSTATE_UINT8(fair_per, NPCM7xxSMBusState),
        VMSTATE_UINT8(txf_ctl, NPCM7xxSMBusState),
        VMSTATE_UINT8(t_out, NPCM7xxSMBusState),
        VMSTATE_UINT8(txf_sts, NPCM7xxSMBusState),
        VMSTATE_UINT8(rxf_sts, NPCM7xxSMBusState),
        VMSTATE_UINT8(rxf_ctl, NPCM7xxSMBusState),
        VMSTATE_UINT8_ARRAY(rx_fifo, NPCM7xxSMBusState,
                            NPCM7XX_SMBUS_FIFO_SIZE),
        VMSTATE_UINT8(rx_cur, NPCM7xxSMBusState),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_smbus_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx System Management Bus";
    dc->vmsd = &vmstate_npcm7xx_smbus;
    rc->phases.enter = npcm7xx_smbus_enter_reset;
    rc->phases.hold = npcm7xx_smbus_hold_reset;
}

static const TypeInfo npcm7xx_smbus_types[] = {
    {
        .name = TYPE_NPCM7XX_SMBUS,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM7xxSMBusState),
        .class_init = npcm7xx_smbus_class_init,
        .instance_init = npcm7xx_smbus_init,
    },
};
DEFINE_TYPES(npcm7xx_smbus_types);
