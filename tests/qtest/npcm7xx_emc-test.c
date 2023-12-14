/*
 * QTests for Nuvoton NPCM7xx EMC Modules.
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
#include "libqos/libqos.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qemu/bitops.h"
#include "qemu/iov.h"

/* Name of the emc device. */
#define TYPE_NPCM7XX_EMC "npcm7xx-emc"

/* Timeout for various operations, in seconds. */
#define TIMEOUT_SECONDS 10

/* Address in memory of the descriptor. */
#define DESC_ADDR (1 << 20) /* 1 MiB */

/* Address in memory of the data packet. */
#define DATA_ADDR (DESC_ADDR + 4096)

#define CRC_LENGTH 4

#define NUM_TX_DESCRIPTORS 3
#define NUM_RX_DESCRIPTORS 2

/* Size of tx,rx test buffers. */
#define TX_DATA_LEN 64
#define RX_DATA_LEN 64

#define TX_STEP_COUNT 10000
#define RX_STEP_COUNT 10000

/* 32-bit register indices. */
typedef enum NPCM7xxPWMRegister {
    /* Control registers. */
    REG_CAMCMR,
    REG_CAMEN,

    /* There are 16 CAMn[ML] registers. */
    REG_CAMM_BASE,
    REG_CAML_BASE,

    REG_TXDLSA = 0x22,
    REG_RXDLSA,
    REG_MCMDR,
    REG_MIID,
    REG_MIIDA,
    REG_FFTCR,
    REG_TSDR,
    REG_RSDR,
    REG_DMARFC,
    REG_MIEN,

    /* Status registers. */
    REG_MISTA,
    REG_MGSTA,
    REG_MPCNT,
    REG_MRPC,
    REG_MRPCC,
    REG_MREPC,
    REG_DMARFS,
    REG_CTXDSA,
    REG_CTXBSA,
    REG_CRXDSA,
    REG_CRXBSA,

    NPCM7XX_NUM_EMC_REGS,
} NPCM7xxPWMRegister;

enum { NUM_CAMML_REGS = 16 };

/* REG_CAMCMR fields */
/* Enable CAM Compare */
#define REG_CAMCMR_ECMP (1 << 4)
/* Accept Unicast Packet */
#define REG_CAMCMR_AUP (1 << 0)

/* REG_MCMDR fields */
/* Software Reset */
#define REG_MCMDR_SWR (1 << 24)
/* Frame Transmission On */
#define REG_MCMDR_TXON (1 << 8)
/* Accept Long Packet */
#define REG_MCMDR_ALP (1 << 1)
/* Frame Reception On */
#define REG_MCMDR_RXON (1 << 0)

/* REG_MIEN fields */
/* Enable Transmit Completion Interrupt */
#define REG_MIEN_ENTXCP (1 << 18)
/* Enable Transmit Interrupt */
#define REG_MIEN_ENTXINTR (1 << 16)
/* Enable Receive Good Interrupt */
#define REG_MIEN_ENRXGD (1 << 4)
/* ENable Receive Interrupt */
#define REG_MIEN_ENRXINTR (1 << 0)

/* REG_MISTA fields */
/* Transmit Bus Error Interrupt */
#define REG_MISTA_TXBERR (1 << 24)
/* Transmit Descriptor Unavailable Interrupt */
#define REG_MISTA_TDU (1 << 23)
/* Transmit Completion Interrupt */
#define REG_MISTA_TXCP (1 << 18)
/* Transmit Interrupt */
#define REG_MISTA_TXINTR (1 << 16)
/* Receive Bus Error Interrupt */
#define REG_MISTA_RXBERR (1 << 11)
/* Receive Descriptor Unavailable Interrupt */
#define REG_MISTA_RDU (1 << 10)
/* DMA Early Notification Interrupt */
#define REG_MISTA_DENI (1 << 9)
/* Maximum Frame Length Interrupt */
#define REG_MISTA_DFOI (1 << 8)
/* Receive Good Interrupt */
#define REG_MISTA_RXGD (1 << 4)
/* Packet Too Long Interrupt */
#define REG_MISTA_PTLE (1 << 3)
/* Receive Interrupt */
#define REG_MISTA_RXINTR (1 << 0)

typedef struct NPCM7xxEMCTxDesc NPCM7xxEMCTxDesc;
typedef struct NPCM7xxEMCRxDesc NPCM7xxEMCRxDesc;

struct NPCM7xxEMCTxDesc {
    uint32_t flags;
    uint32_t txbsa;
    uint32_t status_and_length;
    uint32_t ntxdsa;
};

struct NPCM7xxEMCRxDesc {
    uint32_t status_and_length;
    uint32_t rxbsa;
    uint32_t reserved;
    uint32_t nrxdsa;
};

/* NPCM7xxEMCTxDesc.flags values */
/* Owner: 0 = cpu, 1 = emc */
#define TX_DESC_FLAG_OWNER_MASK (1 << 31)
/* Transmit interrupt enable */
#define TX_DESC_FLAG_INTEN (1 << 2)

/* NPCM7xxEMCTxDesc.status_and_length values */
/* Transmission complete */
#define TX_DESC_STATUS_TXCP (1 << 19)
/* Transmit interrupt */
#define TX_DESC_STATUS_TXINTR (1 << 16)

/* NPCM7xxEMCRxDesc.status_and_length values */
/* Owner: 0b00 = cpu, 0b10 = emc */
#define RX_DESC_STATUS_OWNER_SHIFT 30
#define RX_DESC_STATUS_OWNER_MASK 0xc0000000
/* Frame Reception Complete */
#define RX_DESC_STATUS_RXGD (1 << 20)
/* Packet too long */
#define RX_DESC_STATUS_PTLE (1 << 19)
/* Receive Interrupt */
#define RX_DESC_STATUS_RXINTR (1 << 16)

#define RX_DESC_PKT_LEN(word) ((uint32_t) (word) & 0xffff)

typedef struct EMCModule {
    int rx_irq;
    int tx_irq;
    uint64_t base_addr;
} EMCModule;

typedef struct TestData {
    const EMCModule *module;
} TestData;

static const EMCModule emc_module_list[] = {
    {
        .rx_irq     = 15,
        .tx_irq     = 16,
        .base_addr  = 0xf0825000
    },
    {
        .rx_irq     = 114,
        .tx_irq     = 115,
        .base_addr  = 0xf0826000
    }
};

/* Returns the index of the EMC module. */
static int emc_module_index(const EMCModule *mod)
{
    ptrdiff_t diff = mod - emc_module_list;

    g_assert_true(diff >= 0 && diff < ARRAY_SIZE(emc_module_list));

    return diff;
}

#ifndef _WIN32
static void packet_test_clear(void *sockets)
{
    int *test_sockets = sockets;

    close(test_sockets[0]);
    g_free(test_sockets);
}

static int *packet_test_init(int module_num, GString *cmd_line)
{
    int *test_sockets = g_new(int, 2);
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, test_sockets);
    g_assert_cmpint(ret, != , -1);

    /*
     * KISS and use -nic. We specify two nics (both emc{0,1}) because there's
     * currently no way to specify only emc1: The driver implicitly relies on
     * emc[i] == nd_table[i].
     */
    if (module_num == 0) {
        g_string_append_printf(cmd_line,
                               " -nic socket,fd=%d,model=" TYPE_NPCM7XX_EMC " "
                               " -nic user,model=" TYPE_NPCM7XX_EMC " ",
                               test_sockets[1]);
    } else {
        g_string_append_printf(cmd_line,
                               " -nic user,model=" TYPE_NPCM7XX_EMC " "
                               " -nic socket,fd=%d,model=" TYPE_NPCM7XX_EMC " ",
                               test_sockets[1]);
    }

    g_test_queue_destroy(packet_test_clear, test_sockets);
    return test_sockets;
}
#endif /* _WIN32 */

static uint32_t emc_read(QTestState *qts, const EMCModule *mod,
                         NPCM7xxPWMRegister regno)
{
    return qtest_readl(qts, mod->base_addr + regno * sizeof(uint32_t));
}

#ifndef _WIN32
static void emc_write(QTestState *qts, const EMCModule *mod,
                      NPCM7xxPWMRegister regno, uint32_t value)
{
    qtest_writel(qts, mod->base_addr + regno * sizeof(uint32_t), value);
}

static void emc_read_tx_desc(QTestState *qts, uint32_t addr,
                             NPCM7xxEMCTxDesc *desc)
{
    qtest_memread(qts, addr, desc, sizeof(*desc));
    desc->flags = le32_to_cpu(desc->flags);
    desc->txbsa = le32_to_cpu(desc->txbsa);
    desc->status_and_length = le32_to_cpu(desc->status_and_length);
    desc->ntxdsa = le32_to_cpu(desc->ntxdsa);
}

static void emc_write_tx_desc(QTestState *qts, const NPCM7xxEMCTxDesc *desc,
                              uint32_t addr)
{
    NPCM7xxEMCTxDesc le_desc;

    le_desc.flags = cpu_to_le32(desc->flags);
    le_desc.txbsa = cpu_to_le32(desc->txbsa);
    le_desc.status_and_length = cpu_to_le32(desc->status_and_length);
    le_desc.ntxdsa = cpu_to_le32(desc->ntxdsa);
    qtest_memwrite(qts, addr, &le_desc, sizeof(le_desc));
}

static void emc_read_rx_desc(QTestState *qts, uint32_t addr,
                             NPCM7xxEMCRxDesc *desc)
{
    qtest_memread(qts, addr, desc, sizeof(*desc));
    desc->status_and_length = le32_to_cpu(desc->status_and_length);
    desc->rxbsa = le32_to_cpu(desc->rxbsa);
    desc->reserved = le32_to_cpu(desc->reserved);
    desc->nrxdsa = le32_to_cpu(desc->nrxdsa);
}

static void emc_write_rx_desc(QTestState *qts, const NPCM7xxEMCRxDesc *desc,
                              uint32_t addr)
{
    NPCM7xxEMCRxDesc le_desc;

    le_desc.status_and_length = cpu_to_le32(desc->status_and_length);
    le_desc.rxbsa = cpu_to_le32(desc->rxbsa);
    le_desc.reserved = cpu_to_le32(desc->reserved);
    le_desc.nrxdsa = cpu_to_le32(desc->nrxdsa);
    qtest_memwrite(qts, addr, &le_desc, sizeof(le_desc));
}

/*
 * Reset the EMC module.
 * The module must be reset before, e.g., TXDLSA,RXDLSA are changed.
 */
static bool emc_soft_reset(QTestState *qts, const EMCModule *mod)
{
    uint32_t val;
    uint64_t end_time;

    emc_write(qts, mod, REG_MCMDR, REG_MCMDR_SWR);

    /*
     * Wait for device to reset as the linux driver does.
     * During reset the AHB reads 0 for all registers. So first wait for
     * something that resets to non-zero, and then wait for SWR becoming 0.
     */
    end_time = g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;

    do {
        qtest_clock_step(qts, 100);
        val = emc_read(qts, mod, REG_FFTCR);
    } while (val == 0 && g_get_monotonic_time() < end_time);
    if (val != 0) {
        do {
            qtest_clock_step(qts, 100);
            val = emc_read(qts, mod, REG_MCMDR);
            if ((val & REG_MCMDR_SWR) == 0) {
                /*
                 * N.B. The CAMs have been reset here, so macaddr matching of
                 * incoming packets will not work.
                 */
                return true;
            }
        } while (g_get_monotonic_time() < end_time);
    }

    g_message("%s: Timeout expired", __func__);
    return false;
}
#endif /* _WIN32 */

/* Check emc registers are reset to default value. */
static void test_init(gconstpointer test_data)
{
    const TestData *td = test_data;
    const EMCModule *mod = td->module;
    QTestState *qts = qtest_init("-machine quanta-gsj");
    int i;

#define CHECK_REG(regno, value) \
  do { \
    g_assert_cmphex(emc_read(qts, mod, (regno)), ==, (value)); \
  } while (0)

    CHECK_REG(REG_CAMCMR, 0);
    CHECK_REG(REG_CAMEN, 0);
    CHECK_REG(REG_TXDLSA, 0xfffffffc);
    CHECK_REG(REG_RXDLSA, 0xfffffffc);
    CHECK_REG(REG_MCMDR, 0);
    CHECK_REG(REG_MIID, 0);
    CHECK_REG(REG_MIIDA, 0x00900000);
    CHECK_REG(REG_FFTCR, 0x0101);
    CHECK_REG(REG_DMARFC, 0x0800);
    CHECK_REG(REG_MIEN, 0);
    CHECK_REG(REG_MISTA, 0);
    CHECK_REG(REG_MGSTA, 0);
    CHECK_REG(REG_MPCNT, 0x7fff);
    CHECK_REG(REG_MRPC, 0);
    CHECK_REG(REG_MRPCC, 0);
    CHECK_REG(REG_MREPC, 0);
    CHECK_REG(REG_DMARFS, 0);
    CHECK_REG(REG_CTXDSA, 0);
    CHECK_REG(REG_CTXBSA, 0);
    CHECK_REG(REG_CRXDSA, 0);
    CHECK_REG(REG_CRXBSA, 0);

#undef CHECK_REG

    /* Skip over the MAC address registers, which is BASE+0 */
    for (i = 1; i < NUM_CAMML_REGS; ++i) {
        g_assert_cmpuint(emc_read(qts, mod, REG_CAMM_BASE + i * 2), ==,
                         0);
        g_assert_cmpuint(emc_read(qts, mod, REG_CAML_BASE + i * 2), ==,
                         0);
    }

    qtest_quit(qts);
}

#ifndef _WIN32
static bool emc_wait_irq(QTestState *qts, const EMCModule *mod, int step,
                         bool is_tx)
{
    uint64_t end_time =
        g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;

    do {
        if (qtest_get_irq(qts, is_tx ? mod->tx_irq : mod->rx_irq)) {
            return true;
        }
        qtest_clock_step(qts, step);
    } while (g_get_monotonic_time() < end_time);

    g_message("%s: Timeout expired", __func__);
    return false;
}

static bool emc_wait_mista(QTestState *qts, const EMCModule *mod, int step,
                           uint32_t flag)
{
    uint64_t end_time =
        g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;

    do {
        uint32_t mista = emc_read(qts, mod, REG_MISTA);
        if (mista & flag) {
            return true;
        }
        qtest_clock_step(qts, step);
    } while (g_get_monotonic_time() < end_time);

    g_message("%s: Timeout expired", __func__);
    return false;
}

static bool wait_socket_readable(int fd)
{
    fd_set read_fds;
    struct timeval tv;
    int rv;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    tv.tv_sec = TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    rv = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (rv == -1) {
        perror("select");
    } else if (rv == 0) {
        g_message("%s: Timeout expired", __func__);
    }
    return rv == 1;
}

/* Initialize *desc (in host endian format). */
static void init_tx_desc(NPCM7xxEMCTxDesc *desc, size_t count,
                         uint32_t desc_addr)
{
    g_assert(count >= 2);
    memset(&desc[0], 0, sizeof(*desc) * count);
    /* Leave the last one alone, owned by the cpu -> stops transmission. */
    for (size_t i = 0; i < count - 1; ++i) {
        desc[i].flags =
            (TX_DESC_FLAG_OWNER_MASK | /* owner = 1: emc */
             TX_DESC_FLAG_INTEN |
             0 | /* crc append = 0 */
             0 /* padding enable = 0 */);
        desc[i].status_and_length =
            (0 | /* collision count = 0 */
             0 | /* SQE = 0 */
             0 | /* PAU = 0 */
             0 | /* TXHA = 0 */
             0 | /* LC = 0 */
             0 | /* TXABT = 0 */
             0 | /* NCS = 0 */
             0 | /* EXDEF = 0 */
             0 | /* TXCP = 0 */
             0 | /* DEF = 0 */
             0 | /* TXINTR = 0 */
             0 /* length filled in later */);
        desc[i].ntxdsa = desc_addr + (i + 1) * sizeof(*desc);
    }
}

static void enable_tx(QTestState *qts, const EMCModule *mod,
                      const NPCM7xxEMCTxDesc *desc, size_t count,
                      uint32_t desc_addr, uint32_t mien_flags)
{
    /* Write the descriptors to guest memory. */
    for (size_t i = 0; i < count; ++i) {
        emc_write_tx_desc(qts, desc + i, desc_addr + i * sizeof(*desc));
    }

    /* Trigger sending the packet. */
    /* The module must be reset before changing TXDLSA. */
    g_assert(emc_soft_reset(qts, mod));
    emc_write(qts, mod, REG_TXDLSA, desc_addr);
    emc_write(qts, mod, REG_CTXDSA, ~0);
    emc_write(qts, mod, REG_MIEN, REG_MIEN_ENTXCP | mien_flags);
    {
        uint32_t mcmdr = emc_read(qts, mod, REG_MCMDR);
        mcmdr |= REG_MCMDR_TXON;
        emc_write(qts, mod, REG_MCMDR, mcmdr);
    }
}

static void emc_send_verify1(QTestState *qts, const EMCModule *mod, int fd,
                             bool with_irq, uint32_t desc_addr,
                             uint32_t next_desc_addr,
                             const char *test_data, int test_size)
{
    NPCM7xxEMCTxDesc result_desc;
    uint32_t expected_mask, expected_value, recv_len;
    int ret;
    char buffer[TX_DATA_LEN];

    g_assert(wait_socket_readable(fd));

    /* Read the descriptor back. */
    emc_read_tx_desc(qts, desc_addr, &result_desc);
    /* Descriptor should be owned by cpu now. */
    g_assert((result_desc.flags & TX_DESC_FLAG_OWNER_MASK) == 0);
    /* Test the status bits, ignoring the length field. */
    expected_mask = 0xffff << 16;
    expected_value = TX_DESC_STATUS_TXCP;
    if (with_irq) {
        expected_value |= TX_DESC_STATUS_TXINTR;
    }
    g_assert_cmphex((result_desc.status_and_length & expected_mask), ==,
                    expected_value);

    /* Check data sent to the backend. */
    recv_len = ~0;
    ret = recv(fd, &recv_len, sizeof(recv_len), MSG_DONTWAIT);
    g_assert_cmpint(ret, == , sizeof(recv_len));

    g_assert(wait_socket_readable(fd));
    memset(buffer, 0xff, sizeof(buffer));
    ret = recv(fd, buffer, test_size, MSG_DONTWAIT);
    g_assert_cmpmem(buffer, ret, test_data, test_size);
}

static void emc_send_verify(QTestState *qts, const EMCModule *mod, int fd,
                            bool with_irq)
{
    NPCM7xxEMCTxDesc desc[NUM_TX_DESCRIPTORS];
    uint32_t desc_addr = DESC_ADDR;
    static const char test1_data[] = "TEST1";
    static const char test2_data[] = "Testing 1 2 3 ...";
    uint32_t data1_addr = DATA_ADDR;
    uint32_t data2_addr = data1_addr + sizeof(test1_data);
    bool got_tdu;
    uint32_t end_desc_addr;

    /* Prepare test data buffer. */
    qtest_memwrite(qts, data1_addr, test1_data, sizeof(test1_data));
    qtest_memwrite(qts, data2_addr, test2_data, sizeof(test2_data));

    init_tx_desc(&desc[0], NUM_TX_DESCRIPTORS, desc_addr);
    desc[0].txbsa = data1_addr;
    desc[0].status_and_length |= sizeof(test1_data);
    desc[1].txbsa = data2_addr;
    desc[1].status_and_length |= sizeof(test2_data);

    enable_tx(qts, mod, &desc[0], NUM_TX_DESCRIPTORS, desc_addr,
              with_irq ? REG_MIEN_ENTXINTR : 0);

    /* Prod the device to send the packet. */
    emc_write(qts, mod, REG_TSDR, 1);

    /*
     * It's problematic to observe the interrupt for each packet.
     * Instead just wait until all the packets go out.
     */
    got_tdu = false;
    while (!got_tdu) {
        if (with_irq) {
            g_assert_true(emc_wait_irq(qts, mod, TX_STEP_COUNT,
                                       /*is_tx=*/true));
        } else {
            g_assert_true(emc_wait_mista(qts, mod, TX_STEP_COUNT,
                                         REG_MISTA_TXINTR));
        }
        got_tdu = !!(emc_read(qts, mod, REG_MISTA) & REG_MISTA_TDU);
        /* If we don't have TDU yet, reset the interrupt. */
        if (!got_tdu) {
            emc_write(qts, mod, REG_MISTA,
                      emc_read(qts, mod, REG_MISTA) & 0xffff0000);
        }
    }

    end_desc_addr = desc_addr + 2 * sizeof(desc[0]);
    g_assert_cmphex(emc_read(qts, mod, REG_CTXDSA), ==, end_desc_addr);
    g_assert_cmphex(emc_read(qts, mod, REG_MISTA), ==,
                    REG_MISTA_TXCP | REG_MISTA_TXINTR | REG_MISTA_TDU);

    emc_send_verify1(qts, mod, fd, with_irq,
                     desc_addr, end_desc_addr,
                     test1_data, sizeof(test1_data));
    emc_send_verify1(qts, mod, fd, with_irq,
                     desc_addr + sizeof(desc[0]), end_desc_addr,
                     test2_data, sizeof(test2_data));
}

/* Initialize *desc (in host endian format). */
static void init_rx_desc(NPCM7xxEMCRxDesc *desc, size_t count,
                         uint32_t desc_addr, uint32_t data_addr)
{
    g_assert_true(count >= 2);
    memset(desc, 0, sizeof(*desc) * count);
    desc[0].rxbsa = data_addr;
    desc[0].status_and_length =
        (0b10 << RX_DESC_STATUS_OWNER_SHIFT | /* owner = 10: emc */
         0 | /* RP = 0 */
         0 | /* ALIE = 0 */
         0 | /* RXGD = 0 */
         0 | /* PTLE = 0 */
         0 | /* CRCE = 0 */
         0 | /* RXINTR = 0 */
         0   /* length (filled in later) */);
    /* Leave the last one alone, owned by the cpu -> stops transmission. */
    desc[0].nrxdsa = desc_addr + sizeof(*desc);
}

static void enable_rx(QTestState *qts, const EMCModule *mod,
                      const NPCM7xxEMCRxDesc *desc, size_t count,
                      uint32_t desc_addr, uint32_t mien_flags,
                      uint32_t mcmdr_flags)
{
    /*
     * Write the descriptor to guest memory.
     * FWIW, IWBN if the docs said the buffer needs to be at least DMARFC
     * bytes.
     */
    for (size_t i = 0; i < count; ++i) {
        emc_write_rx_desc(qts, desc + i, desc_addr + i * sizeof(*desc));
    }

    /* Trigger receiving the packet. */
    /* The module must be reset before changing RXDLSA. */
    g_assert(emc_soft_reset(qts, mod));
    emc_write(qts, mod, REG_RXDLSA, desc_addr);
    emc_write(qts, mod, REG_MIEN, REG_MIEN_ENRXGD | mien_flags);

    /*
     * We don't know what the device's macaddr is, so just accept all
     * unicast packets (AUP).
     */
    emc_write(qts, mod, REG_CAMCMR, REG_CAMCMR_AUP);
    emc_write(qts, mod, REG_CAMEN, 1 << 0);
    {
        uint32_t mcmdr = emc_read(qts, mod, REG_MCMDR);
        mcmdr |= REG_MCMDR_RXON | mcmdr_flags;
        emc_write(qts, mod, REG_MCMDR, mcmdr);
    }
}

static void emc_recv_verify(QTestState *qts, const EMCModule *mod, int fd,
                            bool with_irq, bool pump_rsdr)
{
    NPCM7xxEMCRxDesc desc[NUM_RX_DESCRIPTORS];
    uint32_t desc_addr = DESC_ADDR;
    uint32_t data_addr = DATA_ADDR;
    int ret;
    uint32_t expected_mask, expected_value;
    NPCM7xxEMCRxDesc result_desc;

    /* Prepare test data buffer. */
    const char test[RX_DATA_LEN] = "TEST";
    int len = htonl(sizeof(test));
    const struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        },{
            .iov_base = (char *) test,
            .iov_len = sizeof(test),
        },
    };

    /*
     * Reset the device BEFORE sending a test packet, otherwise the packet
     * may get swallowed by an active device of an earlier test.
     */
    init_rx_desc(&desc[0], NUM_RX_DESCRIPTORS, desc_addr, data_addr);
    enable_rx(qts, mod, &desc[0], NUM_RX_DESCRIPTORS, desc_addr,
              with_irq ? REG_MIEN_ENRXINTR : 0, 0);

    /*
     * If requested, prod the device to accept a packet.
     * This isn't necessary, the linux driver doesn't do this.
     * Test doing/not-doing this for robustness.
     */
    if (pump_rsdr) {
        emc_write(qts, mod, REG_RSDR, 1);
    }

    /* Send test packet to device's socket. */
    ret = iov_send(fd, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, == , sizeof(test) + sizeof(len));

    /* Wait for RX interrupt. */
    if (with_irq) {
        g_assert_true(emc_wait_irq(qts, mod, RX_STEP_COUNT, /*is_tx=*/false));
    } else {
        g_assert_true(emc_wait_mista(qts, mod, RX_STEP_COUNT, REG_MISTA_RXGD));
    }

    g_assert_cmphex(emc_read(qts, mod, REG_CRXDSA), ==,
                    desc_addr + sizeof(desc[0]));

    expected_mask = 0xffff;
    expected_value = (REG_MISTA_DENI |
                      REG_MISTA_RXGD |
                      REG_MISTA_RXINTR);
    g_assert_cmphex((emc_read(qts, mod, REG_MISTA) & expected_mask),
                    ==, expected_value);

    /* Read the descriptor back. */
    emc_read_rx_desc(qts, desc_addr, &result_desc);
    /* Descriptor should be owned by cpu now. */
    g_assert((result_desc.status_and_length & RX_DESC_STATUS_OWNER_MASK) == 0);
    /* Test the status bits, ignoring the length field. */
    expected_mask = 0xffff << 16;
    expected_value = RX_DESC_STATUS_RXGD;
    if (with_irq) {
        expected_value |= RX_DESC_STATUS_RXINTR;
    }
    g_assert_cmphex((result_desc.status_and_length & expected_mask), ==,
                    expected_value);
    g_assert_cmpint(RX_DESC_PKT_LEN(result_desc.status_and_length), ==,
                    RX_DATA_LEN + CRC_LENGTH);

    {
        char buffer[RX_DATA_LEN];
        qtest_memread(qts, data_addr, buffer, sizeof(buffer));
        g_assert_cmpstr(buffer, == , "TEST");
    }
}

static void emc_test_ptle(QTestState *qts, const EMCModule *mod, int fd)
{
    NPCM7xxEMCRxDesc desc[NUM_RX_DESCRIPTORS];
    uint32_t desc_addr = DESC_ADDR;
    uint32_t data_addr = DATA_ADDR;
    int ret;
    NPCM7xxEMCRxDesc result_desc;
    uint32_t expected_mask, expected_value;

    /* Prepare test data buffer. */
#define PTLE_DATA_LEN 1600
    char test_data[PTLE_DATA_LEN];
    int len = htonl(sizeof(test_data));
    const struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        },{
            .iov_base = (char *) test_data,
            .iov_len = sizeof(test_data),
        },
    };
    memset(test_data, 42, sizeof(test_data));

    /*
     * Reset the device BEFORE sending a test packet, otherwise the packet
     * may get swallowed by an active device of an earlier test.
     */
    init_rx_desc(&desc[0], NUM_RX_DESCRIPTORS, desc_addr, data_addr);
    enable_rx(qts, mod, &desc[0], NUM_RX_DESCRIPTORS, desc_addr,
              REG_MIEN_ENRXINTR, REG_MCMDR_ALP);

    /* Send test packet to device's socket. */
    ret = iov_send(fd, iov, 2, 0, sizeof(len) + sizeof(test_data));
    g_assert_cmpint(ret, == , sizeof(test_data) + sizeof(len));

    /* Wait for RX interrupt. */
    g_assert_true(emc_wait_irq(qts, mod, RX_STEP_COUNT, /*is_tx=*/false));

    /* Read the descriptor back. */
    emc_read_rx_desc(qts, desc_addr, &result_desc);
    /* Descriptor should be owned by cpu now. */
    g_assert((result_desc.status_and_length & RX_DESC_STATUS_OWNER_MASK) == 0);
    /* Test the status bits, ignoring the length field. */
    expected_mask = 0xffff << 16;
    expected_value = (RX_DESC_STATUS_RXGD |
                      RX_DESC_STATUS_PTLE |
                      RX_DESC_STATUS_RXINTR);
    g_assert_cmphex((result_desc.status_and_length & expected_mask), ==,
                    expected_value);
    g_assert_cmpint(RX_DESC_PKT_LEN(result_desc.status_and_length), ==,
                    PTLE_DATA_LEN + CRC_LENGTH);

    {
        char buffer[PTLE_DATA_LEN];
        qtest_memread(qts, data_addr, buffer, sizeof(buffer));
        g_assert(memcmp(buffer, test_data, PTLE_DATA_LEN) == 0);
    }
}

static void test_tx(gconstpointer test_data)
{
    const TestData *td = test_data;
    GString *cmd_line = g_string_new("-machine quanta-gsj");
    int *test_sockets = packet_test_init(emc_module_index(td->module),
                                         cmd_line);
    QTestState *qts = qtest_init(cmd_line->str);

    /*
     * TODO: For pedantic correctness test_sockets[0] should be closed after
     * the fork and before the exec, but that will require some harness
     * improvements.
     */
    close(test_sockets[1]);
    /* Defensive programming */
    test_sockets[1] = -1;

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");

    emc_send_verify(qts, td->module, test_sockets[0], /*with_irq=*/false);
    emc_send_verify(qts, td->module, test_sockets[0], /*with_irq=*/true);

    qtest_quit(qts);
}

static void test_rx(gconstpointer test_data)
{
    const TestData *td = test_data;
    GString *cmd_line = g_string_new("-machine quanta-gsj");
    int *test_sockets = packet_test_init(emc_module_index(td->module),
                                         cmd_line);
    QTestState *qts = qtest_init(cmd_line->str);

    /*
     * TODO: For pedantic correctness test_sockets[0] should be closed after
     * the fork and before the exec, but that will require some harness
     * improvements.
     */
    close(test_sockets[1]);
    /* Defensive programming */
    test_sockets[1] = -1;

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");

    emc_recv_verify(qts, td->module, test_sockets[0], /*with_irq=*/false,
                    /*pump_rsdr=*/false);
    emc_recv_verify(qts, td->module, test_sockets[0], /*with_irq=*/false,
                    /*pump_rsdr=*/true);
    emc_recv_verify(qts, td->module, test_sockets[0], /*with_irq=*/true,
                    /*pump_rsdr=*/false);
    emc_recv_verify(qts, td->module, test_sockets[0], /*with_irq=*/true,
                    /*pump_rsdr=*/true);
    emc_test_ptle(qts, td->module, test_sockets[0]);

    qtest_quit(qts);
}
#endif /* _WIN32 */

static void emc_add_test(const char *name, const TestData* td,
                         GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
            "npcm7xx_emc/emc[%d]/%s", emc_module_index(td->module), name);
    qtest_add_data_func(full_name, td, fn);
}
#define add_test(name, td) emc_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    TestData test_data_list[ARRAY_SIZE(emc_module_list)];

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(emc_module_list); ++i) {
        TestData *td = &test_data_list[i];

        td->module = &emc_module_list[i];

        add_test(init, td);
#ifndef _WIN32
        add_test(tx, td);
        add_test(rx, td);
#endif
    }

    return g_test_run();
}
