/*
 * Nuvoton NPCM7xx EMC Module
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
 *
 * Unsupported/unimplemented features:
 * - MCMDR.FDUP (full duplex) is ignored, half duplex is not supported
 * - Only CAM0 is supported, CAM[1-15] are not
 *   - writes to CAMEN.[1-15] are ignored, these bits always read as zeroes
 * - MII is not implemented, MIIDA.BUSY and MIID always return zero
 * - MCMDR.LBK is not implemented
 * - MCMDR.{OPMOD,ENSQE,AEP,ARP} are not supported
 * - H/W FIFOs are not supported, MCMDR.FFTCR is ignored
 * - MGSTA.SQE is not supported
 * - pause and control frames are not implemented
 * - MGSTA.CCNT is not supported
 * - MPCNT, DMARFS are not implemented
 */

#include "qemu/osdep.h"

/* For crc32 */
#include <zlib.h>

#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/net/npcm7xx_emc.h"
#include "net/eth.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "sysemu/dma.h"
#include "trace.h"

#define CRC_LENGTH 4

/*
 * The maximum size of a (layer 2) ethernet frame as defined by 802.3.
 * 1518 = 6(dest macaddr) + 6(src macaddr) + 2(proto) + 4(crc) + 1500(payload)
 * This does not include an additional 4 for the vlan field (802.1q).
 */
#define MAX_ETH_FRAME_SIZE 1518

static const char *emc_reg_name(int regno)
{
#define REG(name) case REG_ ## name: return #name;
    switch (regno) {
    REG(CAMCMR)
    REG(CAMEN)
    REG(TXDLSA)
    REG(RXDLSA)
    REG(MCMDR)
    REG(MIID)
    REG(MIIDA)
    REG(FFTCR)
    REG(TSDR)
    REG(RSDR)
    REG(DMARFC)
    REG(MIEN)
    REG(MISTA)
    REG(MGSTA)
    REG(MPCNT)
    REG(MRPC)
    REG(MRPCC)
    REG(MREPC)
    REG(DMARFS)
    REG(CTXDSA)
    REG(CTXBSA)
    REG(CRXDSA)
    REG(CRXBSA)
    case REG_CAMM_BASE + 0: return "CAM0M";
    case REG_CAML_BASE + 0: return "CAM0L";
    case REG_CAMM_BASE + 2 ... REG_CAMML_LAST:
        /* Only CAM0 is supported, fold the others into something simple. */
        if (regno & 1) {
            return "CAM<n>L";
        } else {
            return "CAM<n>M";
        }
    default: return "UNKNOWN";
    }
#undef REG
}

static void emc_reset(NPCM7xxEMCState *emc)
{
    uint32_t value;

    trace_npcm7xx_emc_reset(emc->emc_num);

    memset(&emc->regs[0], 0, sizeof(emc->regs));

    /* These regs have non-zero reset values. */
    emc->regs[REG_TXDLSA] = 0xfffffffc;
    emc->regs[REG_RXDLSA] = 0xfffffffc;
    emc->regs[REG_MIIDA] = 0x00900000;
    emc->regs[REG_FFTCR] = 0x0101;
    emc->regs[REG_DMARFC] = 0x0800;
    emc->regs[REG_MPCNT] = 0x7fff;

    emc->tx_active = false;
    emc->rx_active = false;

    /* Set the MAC address in the register space. */
    value = (emc->conf.macaddr.a[0] << 24) |
        (emc->conf.macaddr.a[1] << 16) |
        (emc->conf.macaddr.a[2] << 8) |
        emc->conf.macaddr.a[3];
    emc->regs[REG_CAMM_BASE] = value;

    value = (emc->conf.macaddr.a[4] << 24) | (emc->conf.macaddr.a[5] << 16);
    emc->regs[REG_CAML_BASE] = value;
}

static void npcm7xx_emc_reset(DeviceState *dev)
{
    NPCM7xxEMCState *emc = NPCM7XX_EMC(dev);
    emc_reset(emc);
}

static void emc_soft_reset(NPCM7xxEMCState *emc)
{
    /*
     * The docs say at least MCMDR.{LBK,OPMOD} bits are not changed during a
     * soft reset, but does not go into further detail. For now, KISS.
     */
    uint32_t mcmdr = emc->regs[REG_MCMDR];
    emc_reset(emc);
    emc->regs[REG_MCMDR] = mcmdr & (REG_MCMDR_LBK | REG_MCMDR_OPMOD);

    qemu_set_irq(emc->tx_irq, 0);
    qemu_set_irq(emc->rx_irq, 0);
}

static void emc_set_link(NetClientState *nc)
{
    /* Nothing to do yet. */
}

/* MISTA.TXINTR is the union of the individual bits with their enables. */
static void emc_update_mista_txintr(NPCM7xxEMCState *emc)
{
    /* Only look at the bits we support. */
    uint32_t mask = (REG_MISTA_TXBERR |
                     REG_MISTA_TDU |
                     REG_MISTA_TXCP);
    if (emc->regs[REG_MISTA] & emc->regs[REG_MIEN] & mask) {
        emc->regs[REG_MISTA] |= REG_MISTA_TXINTR;
    } else {
        emc->regs[REG_MISTA] &= ~REG_MISTA_TXINTR;
    }
}

/* MISTA.RXINTR is the union of the individual bits with their enables. */
static void emc_update_mista_rxintr(NPCM7xxEMCState *emc)
{
    /* Only look at the bits we support. */
    uint32_t mask = (REG_MISTA_RXBERR |
                     REG_MISTA_RDU |
                     REG_MISTA_RXGD);
    if (emc->regs[REG_MISTA] & emc->regs[REG_MIEN] & mask) {
        emc->regs[REG_MISTA] |= REG_MISTA_RXINTR;
    } else {
        emc->regs[REG_MISTA] &= ~REG_MISTA_RXINTR;
    }
}

/* N.B. emc_update_mista_txintr must have already been called. */
static void emc_update_tx_irq(NPCM7xxEMCState *emc)
{
    int level = !!(emc->regs[REG_MISTA] &
                   emc->regs[REG_MIEN] &
                   REG_MISTA_TXINTR);
    trace_npcm7xx_emc_update_tx_irq(level);
    qemu_set_irq(emc->tx_irq, level);
}

/* N.B. emc_update_mista_rxintr must have already been called. */
static void emc_update_rx_irq(NPCM7xxEMCState *emc)
{
    int level = !!(emc->regs[REG_MISTA] &
                   emc->regs[REG_MIEN] &
                   REG_MISTA_RXINTR);
    trace_npcm7xx_emc_update_rx_irq(level);
    qemu_set_irq(emc->rx_irq, level);
}

/* Update IRQ states due to changes in MIEN,MISTA. */
static void emc_update_irq_from_reg_change(NPCM7xxEMCState *emc)
{
    emc_update_mista_txintr(emc);
    emc_update_tx_irq(emc);

    emc_update_mista_rxintr(emc);
    emc_update_rx_irq(emc);
}

static int emc_read_tx_desc(dma_addr_t addr, NPCM7xxEMCTxDesc *desc)
{
    if (dma_memory_read(&address_space_memory, addr, desc,
                        sizeof(*desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    desc->flags = le32_to_cpu(desc->flags);
    desc->txbsa = le32_to_cpu(desc->txbsa);
    desc->status_and_length = le32_to_cpu(desc->status_and_length);
    desc->ntxdsa = le32_to_cpu(desc->ntxdsa);
    return 0;
}

static int emc_write_tx_desc(const NPCM7xxEMCTxDesc *desc, dma_addr_t addr)
{
    NPCM7xxEMCTxDesc le_desc;

    le_desc.flags = cpu_to_le32(desc->flags);
    le_desc.txbsa = cpu_to_le32(desc->txbsa);
    le_desc.status_and_length = cpu_to_le32(desc->status_and_length);
    le_desc.ntxdsa = cpu_to_le32(desc->ntxdsa);
    if (dma_memory_write(&address_space_memory, addr, &le_desc,
                         sizeof(le_desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to write descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    return 0;
}

static int emc_read_rx_desc(dma_addr_t addr, NPCM7xxEMCRxDesc *desc)
{
    if (dma_memory_read(&address_space_memory, addr, desc,
                        sizeof(*desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    desc->status_and_length = le32_to_cpu(desc->status_and_length);
    desc->rxbsa = le32_to_cpu(desc->rxbsa);
    desc->reserved = le32_to_cpu(desc->reserved);
    desc->nrxdsa = le32_to_cpu(desc->nrxdsa);
    return 0;
}

static int emc_write_rx_desc(const NPCM7xxEMCRxDesc *desc, dma_addr_t addr)
{
    NPCM7xxEMCRxDesc le_desc;

    le_desc.status_and_length = cpu_to_le32(desc->status_and_length);
    le_desc.rxbsa = cpu_to_le32(desc->rxbsa);
    le_desc.reserved = cpu_to_le32(desc->reserved);
    le_desc.nrxdsa = cpu_to_le32(desc->nrxdsa);
    if (dma_memory_write(&address_space_memory, addr, &le_desc,
                         sizeof(le_desc), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to write descriptor @ 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return -1;
    }
    return 0;
}

static void emc_set_mista(NPCM7xxEMCState *emc, uint32_t flags)
{
    trace_npcm7xx_emc_set_mista(flags);
    emc->regs[REG_MISTA] |= flags;
    if (extract32(flags, 16, 16)) {
        emc_update_mista_txintr(emc);
    }
    if (extract32(flags, 0, 16)) {
        emc_update_mista_rxintr(emc);
    }
}

static void emc_halt_tx(NPCM7xxEMCState *emc, uint32_t mista_flag)
{
    emc->tx_active = false;
    emc_set_mista(emc, mista_flag);
}

static void emc_halt_rx(NPCM7xxEMCState *emc, uint32_t mista_flag)
{
    emc->rx_active = false;
    emc_set_mista(emc, mista_flag);
}

static void emc_enable_rx_and_flush(NPCM7xxEMCState *emc)
{
    emc->rx_active = true;
    qemu_flush_queued_packets(qemu_get_queue(emc->nic));
}

static void emc_set_next_tx_descriptor(NPCM7xxEMCState *emc,
                                       const NPCM7xxEMCTxDesc *tx_desc,
                                       uint32_t desc_addr)
{
    /* Update the current descriptor, if only to reset the owner flag. */
    if (emc_write_tx_desc(tx_desc, desc_addr)) {
        /*
         * We just read it so this shouldn't generally happen.
         * Error already reported.
         */
        emc_set_mista(emc, REG_MISTA_TXBERR);
    }
    emc->regs[REG_CTXDSA] = TX_DESC_NTXDSA(tx_desc->ntxdsa);
}

static void emc_set_next_rx_descriptor(NPCM7xxEMCState *emc,
                                       const NPCM7xxEMCRxDesc *rx_desc,
                                       uint32_t desc_addr)
{
    /* Update the current descriptor, if only to reset the owner flag. */
    if (emc_write_rx_desc(rx_desc, desc_addr)) {
        /*
         * We just read it so this shouldn't generally happen.
         * Error already reported.
         */
        emc_set_mista(emc, REG_MISTA_RXBERR);
    }
    emc->regs[REG_CRXDSA] = RX_DESC_NRXDSA(rx_desc->nrxdsa);
}

static void emc_try_send_next_packet(NPCM7xxEMCState *emc)
{
    /* Working buffer for sending out packets. Most packets fit in this. */
#define TX_BUFFER_SIZE 2048
    uint8_t tx_send_buffer[TX_BUFFER_SIZE];
    uint32_t desc_addr = TX_DESC_NTXDSA(emc->regs[REG_CTXDSA]);
    NPCM7xxEMCTxDesc tx_desc;
    uint32_t next_buf_addr, length;
    uint8_t *buf;
    g_autofree uint8_t *malloced_buf = NULL;

    if (emc_read_tx_desc(desc_addr, &tx_desc)) {
        /* Error reading descriptor, already reported. */
        emc_halt_tx(emc, REG_MISTA_TXBERR);
        emc_update_tx_irq(emc);
        return;
    }

    /* Nothing we can do if we don't own the descriptor. */
    if (!(tx_desc.flags & TX_DESC_FLAG_OWNER_MASK)) {
        trace_npcm7xx_emc_cpu_owned_desc(desc_addr);
        emc_halt_tx(emc, REG_MISTA_TDU);
        emc_update_tx_irq(emc);
        return;
     }

    /* Give the descriptor back regardless of what happens. */
    tx_desc.flags &= ~TX_DESC_FLAG_OWNER_MASK;
    tx_desc.status_and_length &= 0xffff;

    /*
     * Despite the h/w documentation saying the tx buffer is word aligned,
     * the linux driver does not word align the buffer. There is value in not
     * aligning the buffer: See the description of NET_IP_ALIGN in linux
     * kernel sources.
     */
    next_buf_addr = tx_desc.txbsa;
    emc->regs[REG_CTXBSA] = next_buf_addr;
    length = TX_DESC_PKT_LEN(tx_desc.status_and_length);
    buf = &tx_send_buffer[0];

    if (length > sizeof(tx_send_buffer)) {
        malloced_buf = g_malloc(length);
        buf = malloced_buf;
    }

    if (dma_memory_read(&address_space_memory, next_buf_addr, buf,
                        length, MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read packet @ 0x%x\n",
                      __func__, next_buf_addr);
        emc_set_mista(emc, REG_MISTA_TXBERR);
        emc_set_next_tx_descriptor(emc, &tx_desc, desc_addr);
        emc_update_tx_irq(emc);
        trace_npcm7xx_emc_tx_done(emc->regs[REG_CTXDSA]);
        return;
    }

    if ((tx_desc.flags & TX_DESC_FLAG_PADEN) && (length < MIN_PACKET_LENGTH)) {
        memset(buf + length, 0, MIN_PACKET_LENGTH - length);
        length = MIN_PACKET_LENGTH;
    }

    /* N.B. emc_receive can get called here. */
    qemu_send_packet(qemu_get_queue(emc->nic), buf, length);
    trace_npcm7xx_emc_sent_packet(length);

    tx_desc.status_and_length |= TX_DESC_STATUS_TXCP;
    if (tx_desc.flags & TX_DESC_FLAG_INTEN) {
        emc_set_mista(emc, REG_MISTA_TXCP);
    }
    if (emc->regs[REG_MISTA] & emc->regs[REG_MIEN] & REG_MISTA_TXINTR) {
        tx_desc.status_and_length |= TX_DESC_STATUS_TXINTR;
    }

    emc_set_next_tx_descriptor(emc, &tx_desc, desc_addr);
    emc_update_tx_irq(emc);
    trace_npcm7xx_emc_tx_done(emc->regs[REG_CTXDSA]);
}

static bool emc_can_receive(NetClientState *nc)
{
    NPCM7xxEMCState *emc = NPCM7XX_EMC(qemu_get_nic_opaque(nc));

    bool can_receive = emc->rx_active;
    trace_npcm7xx_emc_can_receive(can_receive);
    return can_receive;
}

/* If result is false then *fail_reason contains the reason. */
static bool emc_receive_filter1(NPCM7xxEMCState *emc, const uint8_t *buf,
                                size_t len, const char **fail_reason)
{
    eth_pkt_types_e pkt_type = get_eth_packet_type(PKT_GET_ETH_HDR(buf));

    switch (pkt_type) {
    case ETH_PKT_BCAST:
        if (emc->regs[REG_CAMCMR] & REG_CAMCMR_CCAM) {
            return true;
        } else {
            *fail_reason = "Broadcast packet disabled";
            return !!(emc->regs[REG_CAMCMR] & REG_CAMCMR_ABP);
        }
    case ETH_PKT_MCAST:
        if (emc->regs[REG_CAMCMR] & REG_CAMCMR_CCAM) {
            return true;
        } else {
            *fail_reason = "Multicast packet disabled";
            return !!(emc->regs[REG_CAMCMR] & REG_CAMCMR_AMP);
        }
    case ETH_PKT_UCAST: {
        bool matches;
        uint32_t value;
        struct MACAddr mac;
        if (emc->regs[REG_CAMCMR] & REG_CAMCMR_AUP) {
            return true;
        }

        value = emc->regs[REG_CAMM_BASE];
        mac.a[0] = value >> 24;
        mac.a[1] = value >> 16;
        mac.a[2] = value >> 8;
        mac.a[3] = value >> 0;
        value = emc->regs[REG_CAML_BASE];
        mac.a[4] = value >> 24;
        mac.a[5] = value >> 16;

        matches = ((emc->regs[REG_CAMCMR] & REG_CAMCMR_ECMP) &&
                   /* We only support one CAM register, CAM0. */
                   (emc->regs[REG_CAMEN] & (1 << 0)) &&
                   memcmp(buf, mac.a, ETH_ALEN) == 0);
        if (emc->regs[REG_CAMCMR] & REG_CAMCMR_CCAM) {
            *fail_reason = "MACADDR matched, comparison complemented";
            return !matches;
        } else {
            *fail_reason = "MACADDR didn't match";
            return matches;
        }
    }
    default:
        g_assert_not_reached();
    }
}

static bool emc_receive_filter(NPCM7xxEMCState *emc, const uint8_t *buf,
                               size_t len)
{
    const char *fail_reason = NULL;
    bool ok = emc_receive_filter1(emc, buf, len, &fail_reason);
    if (!ok) {
        trace_npcm7xx_emc_packet_filtered_out(fail_reason);
    }
    return ok;
}

static ssize_t emc_receive(NetClientState *nc, const uint8_t *buf, size_t len1)
{
    NPCM7xxEMCState *emc = NPCM7XX_EMC(qemu_get_nic_opaque(nc));
    const uint32_t len = len1;
    size_t max_frame_len;
    bool long_frame;
    uint32_t desc_addr;
    NPCM7xxEMCRxDesc rx_desc;
    uint32_t crc;
    uint8_t *crc_ptr;
    uint32_t buf_addr;

    trace_npcm7xx_emc_receiving_packet(len);

    if (!emc_can_receive(nc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unexpected packet\n", __func__);
        return -1;
    }

    if (len < ETH_HLEN ||
        /* Defensive programming: drop unsupportable large packets. */
        len > 0xffff - CRC_LENGTH) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Dropped frame of %u bytes\n",
                      __func__, len);
        return len;
    }

    /*
     * DENI is set if EMC received the Length/Type field of the incoming
     * packet, so it will be set regardless of what happens next.
     */
    emc_set_mista(emc, REG_MISTA_DENI);

    if (!emc_receive_filter(emc, buf, len)) {
        emc_update_rx_irq(emc);
        return len;
    }

    /* Huge frames (> DMARFC) are dropped. */
    max_frame_len = REG_DMARFC_RXMS(emc->regs[REG_DMARFC]);
    if (len + CRC_LENGTH > max_frame_len) {
        trace_npcm7xx_emc_packet_dropped(len);
        emc_set_mista(emc, REG_MISTA_DFOI);
        emc_update_rx_irq(emc);
        return len;
    }

    /*
     * Long Frames (> MAX_ETH_FRAME_SIZE) are also dropped, unless MCMDR.ALP
     * is set.
     */
    long_frame = false;
    if (len + CRC_LENGTH > MAX_ETH_FRAME_SIZE) {
        if (emc->regs[REG_MCMDR] & REG_MCMDR_ALP) {
            long_frame = true;
        } else {
            trace_npcm7xx_emc_packet_dropped(len);
            emc_set_mista(emc, REG_MISTA_PTLE);
            emc_update_rx_irq(emc);
            return len;
        }
    }

    desc_addr = RX_DESC_NRXDSA(emc->regs[REG_CRXDSA]);
    if (emc_read_rx_desc(desc_addr, &rx_desc)) {
        /* Error reading descriptor, already reported. */
        emc_halt_rx(emc, REG_MISTA_RXBERR);
        emc_update_rx_irq(emc);
        return len;
    }

    /* Nothing we can do if we don't own the descriptor. */
    if (!(rx_desc.status_and_length & RX_DESC_STATUS_OWNER_MASK)) {
        trace_npcm7xx_emc_cpu_owned_desc(desc_addr);
        emc_halt_rx(emc, REG_MISTA_RDU);
        emc_update_rx_irq(emc);
        return len;
    }

    crc = 0;
    crc_ptr = (uint8_t *) &crc;
    if (!(emc->regs[REG_MCMDR] & REG_MCMDR_SPCRC)) {
        crc = cpu_to_be32(crc32(~0, buf, len));
    }

    /* Give the descriptor back regardless of what happens. */
    rx_desc.status_and_length &= ~RX_DESC_STATUS_OWNER_MASK;

    buf_addr = rx_desc.rxbsa;
    emc->regs[REG_CRXBSA] = buf_addr;
    if (dma_memory_write(&address_space_memory, buf_addr, buf,
                         len, MEMTXATTRS_UNSPECIFIED) ||
        (!(emc->regs[REG_MCMDR] & REG_MCMDR_SPCRC) &&
         dma_memory_write(&address_space_memory, buf_addr + len,
                          crc_ptr, 4, MEMTXATTRS_UNSPECIFIED))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bus error writing packet\n",
                      __func__);
        emc_set_mista(emc, REG_MISTA_RXBERR);
        emc_set_next_rx_descriptor(emc, &rx_desc, desc_addr);
        emc_update_rx_irq(emc);
        trace_npcm7xx_emc_rx_done(emc->regs[REG_CRXDSA]);
        return len;
    }

    trace_npcm7xx_emc_received_packet(len);

    /* Note: We've already verified len+4 <= 0xffff. */
    rx_desc.status_and_length = len;
    if (!(emc->regs[REG_MCMDR] & REG_MCMDR_SPCRC)) {
        rx_desc.status_and_length += 4;
    }
    rx_desc.status_and_length |= RX_DESC_STATUS_RXGD;
    emc_set_mista(emc, REG_MISTA_RXGD);

    if (emc->regs[REG_MISTA] & emc->regs[REG_MIEN] & REG_MISTA_RXINTR) {
        rx_desc.status_and_length |= RX_DESC_STATUS_RXINTR;
    }
    if (long_frame) {
        rx_desc.status_and_length |= RX_DESC_STATUS_PTLE;
    }

    emc_set_next_rx_descriptor(emc, &rx_desc, desc_addr);
    emc_update_rx_irq(emc);
    trace_npcm7xx_emc_rx_done(emc->regs[REG_CRXDSA]);
    return len;
}

static uint64_t npcm7xx_emc_read(void *opaque, hwaddr offset, unsigned size)
{
    NPCM7xxEMCState *emc = opaque;
    uint32_t reg = offset / sizeof(uint32_t);
    uint32_t result;

    if (reg >= NPCM7XX_NUM_EMC_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid offset 0x%04" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case REG_MIID:
        /*
         * We don't implement MII. For determinism, always return zero as
         * writes record the last value written for debugging purposes.
         */
        qemu_log_mask(LOG_UNIMP, "%s: Read of MIID, returning 0\n", __func__);
        result = 0;
        break;
    case REG_TSDR:
    case REG_RSDR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Read of write-only reg, %s/%d\n",
                      __func__, emc_reg_name(reg), reg);
        return 0;
    default:
        result = emc->regs[reg];
        break;
    }

    trace_npcm7xx_emc_reg_read(emc->emc_num, result, emc_reg_name(reg), reg);
    return result;
}

static void npcm7xx_emc_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    NPCM7xxEMCState *emc = opaque;
    uint32_t reg = offset / sizeof(uint32_t);
    uint32_t value = v;

    g_assert(size == sizeof(uint32_t));

    if (reg >= NPCM7XX_NUM_EMC_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid offset 0x%04" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    trace_npcm7xx_emc_reg_write(emc->emc_num, emc_reg_name(reg), reg, value);

    switch (reg) {
    case REG_CAMCMR:
        emc->regs[reg] = value;
        break;
    case REG_CAMEN:
        /* Only CAM0 is supported, don't pretend otherwise. */
        if (value & ~1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Only CAM0 is supported, cannot enable others"
                          ": 0x%x\n",
                          __func__, value);
        }
        emc->regs[reg] = value & 1;
        break;
    case REG_CAMM_BASE + 0:
        emc->regs[reg] = value;
        break;
    case REG_CAML_BASE + 0:
        emc->regs[reg] = value;
        break;
    case REG_MCMDR: {
        uint32_t prev;
        if (value & REG_MCMDR_SWR) {
            emc_soft_reset(emc);
            /* On h/w the reset happens over multiple cycles. For now KISS. */
            break;
        }
        prev = emc->regs[reg];
        emc->regs[reg] = value;
        /* Update tx state. */
        if (!(prev & REG_MCMDR_TXON) &&
            (value & REG_MCMDR_TXON)) {
            emc->regs[REG_CTXDSA] = emc->regs[REG_TXDLSA];
            /*
             * Linux kernel turns TX on with CPU still holding descriptor,
             * which suggests we should wait for a write to TSDR before trying
             * to send a packet: so we don't send one here.
             */
        } else if ((prev & REG_MCMDR_TXON) &&
                   !(value & REG_MCMDR_TXON)) {
            emc->regs[REG_MGSTA] |= REG_MGSTA_TXHA;
        }
        if (!(value & REG_MCMDR_TXON)) {
            emc_halt_tx(emc, 0);
        }
        /* Update rx state. */
        if (!(prev & REG_MCMDR_RXON) &&
            (value & REG_MCMDR_RXON)) {
            emc->regs[REG_CRXDSA] = emc->regs[REG_RXDLSA];
        } else if ((prev & REG_MCMDR_RXON) &&
                   !(value & REG_MCMDR_RXON)) {
            emc->regs[REG_MGSTA] |= REG_MGSTA_RXHA;
        }
        if (value & REG_MCMDR_RXON) {
            emc_enable_rx_and_flush(emc);
        } else {
            emc_halt_rx(emc, 0);
        }
        break;
    }
    case REG_TXDLSA:
    case REG_RXDLSA:
    case REG_DMARFC:
    case REG_MIID:
        emc->regs[reg] = value;
        break;
    case REG_MIEN:
        emc->regs[reg] = value;
        emc_update_irq_from_reg_change(emc);
        break;
    case REG_MISTA:
        /* Clear the bits that have 1 in "value". */
        emc->regs[reg] &= ~value;
        emc_update_irq_from_reg_change(emc);
        break;
    case REG_MGSTA:
        /* Clear the bits that have 1 in "value". */
        emc->regs[reg] &= ~value;
        break;
    case REG_TSDR:
        if (emc->regs[REG_MCMDR] & REG_MCMDR_TXON) {
            emc->tx_active = true;
            /* Keep trying to send packets until we run out. */
            while (emc->tx_active) {
                emc_try_send_next_packet(emc);
            }
        }
        break;
    case REG_RSDR:
        if (emc->regs[REG_MCMDR] & REG_MCMDR_RXON) {
            emc_enable_rx_and_flush(emc);
        }
        break;
    case REG_MIIDA:
        emc->regs[reg] = value & ~REG_MIIDA_BUSY;
        break;
    case REG_MRPC:
    case REG_MRPCC:
    case REG_MREPC:
    case REG_CTXDSA:
    case REG_CTXBSA:
    case REG_CRXDSA:
    case REG_CRXBSA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Write to read-only reg %s/%d\n",
                      __func__, emc_reg_name(reg), reg);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Write to unimplemented reg %s/%d\n",
                      __func__, emc_reg_name(reg), reg);
        break;
    }
}

static const struct MemoryRegionOps npcm7xx_emc_ops = {
    .read = npcm7xx_emc_read,
    .write = npcm7xx_emc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void emc_cleanup(NetClientState *nc)
{
    /* Nothing to do yet. */
}

static NetClientInfo net_npcm7xx_emc_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = emc_can_receive,
    .receive = emc_receive,
    .cleanup = emc_cleanup,
    .link_status_changed = emc_set_link,
};

static void npcm7xx_emc_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxEMCState *emc = NPCM7XX_EMC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(emc);

    memory_region_init_io(&emc->iomem, OBJECT(emc), &npcm7xx_emc_ops, emc,
                          TYPE_NPCM7XX_EMC, 4 * KiB);
    sysbus_init_mmio(sbd, &emc->iomem);
    sysbus_init_irq(sbd, &emc->tx_irq);
    sysbus_init_irq(sbd, &emc->rx_irq);

    qemu_macaddr_default_if_unset(&emc->conf.macaddr);
    emc->nic = qemu_new_nic(&net_npcm7xx_emc_info, &emc->conf,
                            object_get_typename(OBJECT(dev)), dev->id,
                            &dev->mem_reentrancy_guard, emc);
    qemu_format_nic_info_str(qemu_get_queue(emc->nic), emc->conf.macaddr.a);
}

static void npcm7xx_emc_unrealize(DeviceState *dev)
{
    NPCM7xxEMCState *emc = NPCM7XX_EMC(dev);

    qemu_del_nic(emc->nic);
}

static const VMStateDescription vmstate_npcm7xx_emc = {
    .name = TYPE_NPCM7XX_EMC,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(emc_num, NPCM7xxEMCState),
        VMSTATE_UINT32_ARRAY(regs, NPCM7xxEMCState, NPCM7XX_NUM_EMC_REGS),
        VMSTATE_BOOL(tx_active, NPCM7xxEMCState),
        VMSTATE_BOOL(rx_active, NPCM7xxEMCState),
        VMSTATE_END_OF_LIST(),
    },
};

static Property npcm7xx_emc_properties[] = {
    DEFINE_NIC_PROPERTIES(NPCM7xxEMCState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void npcm7xx_emc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "NPCM7xx EMC Controller";
    dc->realize = npcm7xx_emc_realize;
    dc->unrealize = npcm7xx_emc_unrealize;
    dc->reset = npcm7xx_emc_reset;
    dc->vmsd = &vmstate_npcm7xx_emc;
    device_class_set_props(dc, npcm7xx_emc_properties);
}

static const TypeInfo npcm7xx_emc_info = {
    .name = TYPE_NPCM7XX_EMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NPCM7xxEMCState),
    .class_init = npcm7xx_emc_class_init,
};

static void npcm7xx_emc_register_type(void)
{
    type_register_static(&npcm7xx_emc_info);
}

type_init(npcm7xx_emc_register_type)
