/*
 * QTests for Nuvoton NPCM7xx SMBus Modules.
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
#include "qemu/bitops.h"
#include "libqos/i2c.h"
#include "libqtest.h"
#include "hw/sensor/tmp105_regs.h"

#define NR_SMBUS_DEVICES    16
#define SMBUS_ADDR(x)       (0xf0080000 + 0x1000 * (x))
#define SMBUS_IRQ(x)        (64 + (x))

#define EVB_DEVICE_ADDR     0x48
#define INVALID_DEVICE_ADDR 0x01

const int evb_bus_list[] = {0, 1, 2, 6};

/* Offsets */
enum CommonRegister {
    OFFSET_SDA     = 0x0,
    OFFSET_ST      = 0x2,
    OFFSET_CST     = 0x4,
    OFFSET_CTL1    = 0x6,
    OFFSET_ADDR1   = 0x8,
    OFFSET_CTL2    = 0xa,
    OFFSET_ADDR2   = 0xc,
    OFFSET_CTL3    = 0xe,
    OFFSET_CST2    = 0x18,
    OFFSET_CST3    = 0x19,
};

enum NPCM7xxSMBusBank0Register {
    OFFSET_ADDR3   = 0x10,
    OFFSET_ADDR7   = 0x11,
    OFFSET_ADDR4   = 0x12,
    OFFSET_ADDR8   = 0x13,
    OFFSET_ADDR5   = 0x14,
    OFFSET_ADDR9   = 0x15,
    OFFSET_ADDR6   = 0x16,
    OFFSET_ADDR10  = 0x17,
    OFFSET_CTL4    = 0x1a,
    OFFSET_CTL5    = 0x1b,
    OFFSET_SCLLT   = 0x1c,
    OFFSET_FIF_CTL = 0x1d,
    OFFSET_SCLHT   = 0x1e,
};

enum NPCM7xxSMBusBank1Register {
    OFFSET_FIF_CTS  = 0x10,
    OFFSET_FAIR_PER = 0x11,
    OFFSET_TXF_CTL  = 0x12,
    OFFSET_T_OUT    = 0x14,
    OFFSET_TXF_STS  = 0x1a,
    OFFSET_RXF_STS  = 0x1c,
    OFFSET_RXF_CTL  = 0x1e,
};

/* ST fields */
#define ST_STP              BIT(7)
#define ST_SDAST            BIT(6)
#define ST_BER              BIT(5)
#define ST_NEGACK           BIT(4)
#define ST_STASTR           BIT(3)
#define ST_NMATCH           BIT(2)
#define ST_MODE             BIT(1)
#define ST_XMIT             BIT(0)

/* CST fields */
#define CST_ARPMATCH        BIT(7)
#define CST_MATCHAF         BIT(6)
#define CST_TGSCL           BIT(5)
#define CST_TSDA            BIT(4)
#define CST_GCMATCH         BIT(3)
#define CST_MATCH           BIT(2)
#define CST_BB              BIT(1)
#define CST_BUSY            BIT(0)

/* CST2 fields */
#define CST2_INSTTS         BIT(7)
#define CST2_MATCH7F        BIT(6)
#define CST2_MATCH6F        BIT(5)
#define CST2_MATCH5F        BIT(4)
#define CST2_MATCH4F        BIT(3)
#define CST2_MATCH3F        BIT(2)
#define CST2_MATCH2F        BIT(1)
#define CST2_MATCH1F        BIT(0)

/* CST3 fields */
#define CST3_EO_BUSY        BIT(7)
#define CST3_MATCH10F       BIT(2)
#define CST3_MATCH9F        BIT(1)
#define CST3_MATCH8F        BIT(0)

/* CTL1 fields */
#define CTL1_STASTRE        BIT(7)
#define CTL1_NMINTE         BIT(6)
#define CTL1_GCMEN          BIT(5)
#define CTL1_ACK            BIT(4)
#define CTL1_EOBINTE        BIT(3)
#define CTL1_INTEN          BIT(2)
#define CTL1_STOP           BIT(1)
#define CTL1_START          BIT(0)

/* CTL2 fields */
#define CTL2_SCLFRQ(rv)     extract8((rv), 1, 6)
#define CTL2_ENABLE         BIT(0)

/* CTL3 fields */
#define CTL3_SCL_LVL        BIT(7)
#define CTL3_SDA_LVL        BIT(6)
#define CTL3_BNK_SEL        BIT(5)
#define CTL3_400K_MODE      BIT(4)
#define CTL3_IDL_START      BIT(3)
#define CTL3_ARPMEN         BIT(2)
#define CTL3_SCLFRQ(rv)     extract8((rv), 0, 2)

/* ADDR fields */
#define ADDR_EN             BIT(7)
#define ADDR_A(rv)          extract8((rv), 0, 6)

/* FIF_CTL fields */
#define FIF_CTL_FIFO_EN         BIT(4)

/* FIF_CTS fields */
#define FIF_CTS_CLR_FIFO        BIT(6)
#define FIF_CTS_RFTE_IE         BIT(3)
#define FIF_CTS_RXF_TXE         BIT(1)

/* TXF_CTL fields */
#define TXF_CTL_THR_TXIE        BIT(6)
#define TXF_CTL_TX_THR(rv)      extract8((rv), 0, 5)

/* TXF_STS fields */
#define TXF_STS_TX_THST         BIT(6)
#define TXF_STS_TX_BYTES(rv)    extract8((rv), 0, 5)

/* RXF_CTL fields */
#define RXF_CTL_THR_RXIE        BIT(6)
#define RXF_CTL_LAST            BIT(5)
#define RXF_CTL_RX_THR(rv)      extract8((rv), 0, 5)

/* RXF_STS fields */
#define RXF_STS_RX_THST         BIT(6)
#define RXF_STS_RX_BYTES(rv)    extract8((rv), 0, 5)


static void choose_bank(QTestState *qts, uint64_t base_addr, uint8_t bank)
{
    uint8_t ctl3 = qtest_readb(qts, base_addr + OFFSET_CTL3);

    if (bank) {
        ctl3 |= CTL3_BNK_SEL;
    } else {
        ctl3 &= ~CTL3_BNK_SEL;
    }

    qtest_writeb(qts, base_addr + OFFSET_CTL3, ctl3);
}

static void check_running(QTestState *qts, uint64_t base_addr)
{
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_CST) & CST_BUSY);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_CST) & CST_BB);
}

static void check_stopped(QTestState *qts, uint64_t base_addr)
{
    uint8_t cst3;

    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_ST), ==, 0);
    g_assert_false(qtest_readb(qts, base_addr + OFFSET_CST) & CST_BUSY);
    g_assert_false(qtest_readb(qts, base_addr + OFFSET_CST) & CST_BB);

    cst3 = qtest_readb(qts, base_addr + OFFSET_CST3);
    g_assert_true(cst3 & CST3_EO_BUSY);
    qtest_writeb(qts, base_addr + OFFSET_CST3, cst3);
    cst3 = qtest_readb(qts, base_addr + OFFSET_CST3);
    g_assert_false(cst3 & CST3_EO_BUSY);
}

static void enable_bus(QTestState *qts, uint64_t base_addr)
{
    uint8_t ctl2 = qtest_readb(qts, base_addr + OFFSET_CTL2);

    ctl2 |= CTL2_ENABLE;
    qtest_writeb(qts, base_addr + OFFSET_CTL2, ctl2);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_CTL2) & CTL2_ENABLE);
}

static void disable_bus(QTestState *qts, uint64_t base_addr)
{
    uint8_t ctl2 = qtest_readb(qts, base_addr + OFFSET_CTL2);

    ctl2 &= ~CTL2_ENABLE;
    qtest_writeb(qts, base_addr + OFFSET_CTL2, ctl2);
    g_assert_false(qtest_readb(qts, base_addr + OFFSET_CTL2) & CTL2_ENABLE);
}

static void start_transfer(QTestState *qts, uint64_t base_addr)
{
    uint8_t ctl1;

    ctl1 = CTL1_START | CTL1_INTEN | CTL1_STASTRE;
    qtest_writeb(qts, base_addr + OFFSET_CTL1, ctl1);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_CTL1), ==,
                    CTL1_INTEN | CTL1_STASTRE | CTL1_INTEN);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_ST), ==,
                    ST_MODE | ST_XMIT | ST_SDAST);
    check_running(qts, base_addr);
}

static void stop_transfer(QTestState *qts, uint64_t base_addr)
{
    uint8_t ctl1 = qtest_readb(qts, base_addr + OFFSET_CTL1);

    ctl1 &= ~(CTL1_START | CTL1_ACK);
    ctl1 |= CTL1_STOP | CTL1_INTEN | CTL1_EOBINTE;
    qtest_writeb(qts, base_addr + OFFSET_CTL1, ctl1);
    ctl1 = qtest_readb(qts, base_addr + OFFSET_CTL1);
    g_assert_false(ctl1 & CTL1_STOP);
}

static void send_byte(QTestState *qts, uint64_t base_addr, uint8_t byte)
{
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_ST), ==,
                    ST_MODE | ST_XMIT | ST_SDAST);
    qtest_writeb(qts, base_addr + OFFSET_SDA, byte);
}

static bool check_recv(QTestState *qts, uint64_t base_addr)
{
    uint8_t st, fif_ctl, rxf_ctl, rxf_sts;
    bool fifo;

    st = qtest_readb(qts, base_addr + OFFSET_ST);
    choose_bank(qts, base_addr, 0);
    fif_ctl = qtest_readb(qts, base_addr + OFFSET_FIF_CTL);
    fifo = fif_ctl & FIF_CTL_FIFO_EN;
    if (!fifo) {
        return st == (ST_MODE | ST_SDAST);
    }

    choose_bank(qts, base_addr, 1);
    rxf_ctl = qtest_readb(qts, base_addr + OFFSET_RXF_CTL);
    rxf_sts = qtest_readb(qts, base_addr + OFFSET_RXF_STS);

    if ((rxf_ctl & RXF_CTL_THR_RXIE) && RXF_STS_RX_BYTES(rxf_sts) < 16) {
        return st == ST_MODE;
    } else {
        return st == (ST_MODE | ST_SDAST);
    }
}

static uint8_t recv_byte(QTestState *qts, uint64_t base_addr)
{
    g_assert_true(check_recv(qts, base_addr));
    return qtest_readb(qts, base_addr + OFFSET_SDA);
}

static void send_address(QTestState *qts, uint64_t base_addr, uint8_t addr,
                         bool recv, bool valid)
{
    uint8_t encoded_addr = (addr << 1) | (recv ? 1 : 0);
    uint8_t st;

    qtest_writeb(qts, base_addr + OFFSET_SDA, encoded_addr);
    st = qtest_readb(qts, base_addr + OFFSET_ST);

    if (valid) {
        if (recv) {
            g_assert_cmphex(st, ==, ST_MODE | ST_SDAST | ST_STASTR);
        } else {
            g_assert_cmphex(st, ==, ST_MODE | ST_XMIT | ST_SDAST | ST_STASTR);
        }

        qtest_writeb(qts, base_addr + OFFSET_ST, ST_STASTR);
        st = qtest_readb(qts, base_addr + OFFSET_ST);
        if (recv) {
            g_assert_true(check_recv(qts, base_addr));
        } else {
            g_assert_cmphex(st, ==, ST_MODE | ST_XMIT | ST_SDAST);
        }
    } else {
        if (recv) {
            g_assert_cmphex(st, ==, ST_MODE | ST_NEGACK);
        } else {
            g_assert_cmphex(st, ==, ST_MODE | ST_XMIT | ST_NEGACK);
        }
    }
}

static void send_nack(QTestState *qts, uint64_t base_addr)
{
    uint8_t ctl1 = qtest_readb(qts, base_addr + OFFSET_CTL1);

    ctl1 &= ~(CTL1_START | CTL1_STOP);
    ctl1 |= CTL1_ACK | CTL1_INTEN;
    qtest_writeb(qts, base_addr + OFFSET_CTL1, ctl1);
}

static void start_fifo_mode(QTestState *qts, uint64_t base_addr)
{
    choose_bank(qts, base_addr, 0);
    qtest_writeb(qts, base_addr + OFFSET_FIF_CTL, FIF_CTL_FIFO_EN);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_FIF_CTL) &
                  FIF_CTL_FIFO_EN);
    choose_bank(qts, base_addr, 1);
    qtest_writeb(qts, base_addr + OFFSET_FIF_CTS,
                 FIF_CTS_CLR_FIFO | FIF_CTS_RFTE_IE);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_FIF_CTS), ==,
                    FIF_CTS_RFTE_IE);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_TXF_STS), ==, 0);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_RXF_STS), ==, 0);
}

static void start_recv_fifo(QTestState *qts, uint64_t base_addr, uint8_t bytes)
{
    choose_bank(qts, base_addr, 1);
    qtest_writeb(qts, base_addr + OFFSET_TXF_CTL, 0);
    qtest_writeb(qts, base_addr + OFFSET_RXF_CTL,
                 RXF_CTL_THR_RXIE | RXF_CTL_LAST | bytes);
}

/* Check the SMBus's status is set correctly when disabled. */
static void test_disable_bus(gconstpointer data)
{
    intptr_t index = (intptr_t)data;
    uint64_t base_addr = SMBUS_ADDR(index);
    QTestState *qts = qtest_init("-machine npcm750-evb");

    disable_bus(qts, base_addr);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_CTL1), ==, 0);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_ST), ==, 0);
    g_assert_false(qtest_readb(qts, base_addr + OFFSET_CST3) & CST3_EO_BUSY);
    g_assert_cmphex(qtest_readb(qts, base_addr + OFFSET_CST), ==, 0);
    qtest_quit(qts);
}

/* Check the SMBus returns a NACK for an invalid address. */
static void test_invalid_addr(gconstpointer data)
{
    intptr_t index = (intptr_t)data;
    uint64_t base_addr = SMBUS_ADDR(index);
    int irq = SMBUS_IRQ(index);
    QTestState *qts = qtest_init("-machine npcm750-evb");

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    enable_bus(qts, base_addr);
    g_assert_false(qtest_get_irq(qts, irq));
    start_transfer(qts, base_addr);
    send_address(qts, base_addr, INVALID_DEVICE_ADDR, false, false);
    g_assert_true(qtest_get_irq(qts, irq));
    stop_transfer(qts, base_addr);
    check_running(qts, base_addr);
    qtest_writeb(qts, base_addr + OFFSET_ST, ST_NEGACK);
    g_assert_false(qtest_readb(qts, base_addr + OFFSET_ST) & ST_NEGACK);
    check_stopped(qts, base_addr);
    qtest_quit(qts);
}

/* Check the SMBus can send and receive bytes to a device in single mode. */
static void test_single_mode(gconstpointer data)
{
    intptr_t index = (intptr_t)data;
    uint64_t base_addr = SMBUS_ADDR(index);
    int irq = SMBUS_IRQ(index);
    uint8_t value = 0x60;
    QTestState *qts = qtest_init("-machine npcm750-evb");

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    enable_bus(qts, base_addr);

    /* Sending */
    g_assert_false(qtest_get_irq(qts, irq));
    start_transfer(qts, base_addr);
    g_assert_true(qtest_get_irq(qts, irq));
    send_address(qts, base_addr, EVB_DEVICE_ADDR, false, true);
    send_byte(qts, base_addr, TMP105_REG_CONFIG);
    send_byte(qts, base_addr, value);
    stop_transfer(qts, base_addr);
    check_stopped(qts, base_addr);

    /* Receiving */
    start_transfer(qts, base_addr);
    send_address(qts, base_addr, EVB_DEVICE_ADDR, false, true);
    send_byte(qts, base_addr, TMP105_REG_CONFIG);
    start_transfer(qts, base_addr);
    send_address(qts, base_addr, EVB_DEVICE_ADDR, true, true);
    send_nack(qts, base_addr);
    stop_transfer(qts, base_addr);
    check_running(qts, base_addr);
    g_assert_cmphex(recv_byte(qts, base_addr), ==, value);
    check_stopped(qts, base_addr);
    qtest_quit(qts);
}

/* Check the SMBus can send and receive bytes in FIFO mode. */
static void test_fifo_mode(gconstpointer data)
{
    intptr_t index = (intptr_t)data;
    uint64_t base_addr = SMBUS_ADDR(index);
    int irq = SMBUS_IRQ(index);
    uint8_t value = 0x60;
    QTestState *qts = qtest_init("-machine npcm750-evb");

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    enable_bus(qts, base_addr);
    start_fifo_mode(qts, base_addr);
    g_assert_false(qtest_get_irq(qts, irq));

    /* Sending */
    start_transfer(qts, base_addr);
    send_address(qts, base_addr, EVB_DEVICE_ADDR, false, true);
    choose_bank(qts, base_addr, 1);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_FIF_CTS) &
                  FIF_CTS_RXF_TXE);
    qtest_writeb(qts, base_addr + OFFSET_TXF_CTL, TXF_CTL_THR_TXIE);
    send_byte(qts, base_addr, TMP105_REG_CONFIG);
    send_byte(qts, base_addr, value);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_FIF_CTS) &
                  FIF_CTS_RXF_TXE);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_TXF_STS) &
                  TXF_STS_TX_THST);
    g_assert_cmpuint(TXF_STS_TX_BYTES(
                        qtest_readb(qts, base_addr + OFFSET_TXF_STS)), ==, 0);
    g_assert_true(qtest_get_irq(qts, irq));
    stop_transfer(qts, base_addr);
    check_stopped(qts, base_addr);

    /* Receiving */
    start_fifo_mode(qts, base_addr);
    start_transfer(qts, base_addr);
    send_address(qts, base_addr, EVB_DEVICE_ADDR, false, true);
    send_byte(qts, base_addr, TMP105_REG_CONFIG);
    start_transfer(qts, base_addr);
    qtest_writeb(qts, base_addr + OFFSET_FIF_CTS, FIF_CTS_RXF_TXE);
    start_recv_fifo(qts, base_addr, 1);
    send_address(qts, base_addr, EVB_DEVICE_ADDR, true, true);
    g_assert_false(qtest_readb(qts, base_addr + OFFSET_FIF_CTS) &
                   FIF_CTS_RXF_TXE);
    g_assert_true(qtest_readb(qts, base_addr + OFFSET_RXF_STS) &
                  RXF_STS_RX_THST);
    g_assert_cmpuint(RXF_STS_RX_BYTES(
                        qtest_readb(qts, base_addr + OFFSET_RXF_STS)), ==, 1);
    send_nack(qts, base_addr);
    stop_transfer(qts, base_addr);
    check_running(qts, base_addr);
    g_assert_cmphex(recv_byte(qts, base_addr), ==, value);
    g_assert_cmpuint(RXF_STS_RX_BYTES(
                        qtest_readb(qts, base_addr + OFFSET_RXF_STS)), ==, 0);
    check_stopped(qts, base_addr);
    qtest_quit(qts);
}

static void smbus_add_test(const char *name, int index, GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
            "npcm7xx_smbus[%d]/%s", index, name);
    qtest_add_data_func(full_name, (void *)(intptr_t)index, fn);
}
#define add_test(name, td) smbus_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    for (i = 0; i < NR_SMBUS_DEVICES; ++i) {
        add_test(disable_bus, i);
        add_test(invalid_addr, i);
    }

    for (i = 0; i < ARRAY_SIZE(evb_bus_list); ++i) {
        add_test(single_mode, evb_bus_list[i]);
        add_test(fifo_mode, evb_bus_list[i]);
    }

    return g_test_run();
}
