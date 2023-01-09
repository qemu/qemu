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
 */

#ifndef NPCM7XX_EMC_H
#define NPCM7XX_EMC_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "net/net.h"

/* 32-bit register indices. */
enum NPCM7xxPWMRegister {
    /* Control registers. */
    REG_CAMCMR,
    REG_CAMEN,

    /* There are 16 CAMn[ML] registers. */
    REG_CAMM_BASE,
    REG_CAML_BASE,
    REG_CAMML_LAST = 0x21,

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
};

/* REG_CAMCMR fields */
/* Enable CAM Compare */
#define REG_CAMCMR_ECMP (1 << 4)
/* Complement CAM Compare */
#define REG_CAMCMR_CCAM (1 << 3)
/* Accept Broadcast Packet */
#define REG_CAMCMR_ABP (1 << 2)
/* Accept Multicast Packet */
#define REG_CAMCMR_AMP (1 << 1)
/* Accept Unicast Packet */
#define REG_CAMCMR_AUP (1 << 0)

/* REG_MCMDR fields */
/* Software Reset */
#define REG_MCMDR_SWR (1 << 24)
/* Internal Loopback Select */
#define REG_MCMDR_LBK (1 << 21)
/* Operation Mode Select */
#define REG_MCMDR_OPMOD (1 << 20)
/* Enable MDC Clock Generation */
#define REG_MCMDR_ENMDC (1 << 19)
/* Full-Duplex Mode Select */
#define REG_MCMDR_FDUP (1 << 18)
/* Enable SQE Checking */
#define REG_MCMDR_ENSEQ (1 << 17)
/* Send PAUSE Frame */
#define REG_MCMDR_SDPZ (1 << 16)
/* No Defer */
#define REG_MCMDR_NDEF (1 << 9)
/* Frame Transmission On */
#define REG_MCMDR_TXON (1 << 8)
/* Strip CRC Checksum */
#define REG_MCMDR_SPCRC (1 << 5)
/* Accept CRC Error Packet */
#define REG_MCMDR_AEP (1 << 4)
/* Accept Control Packet */
#define REG_MCMDR_ACP (1 << 3)
/* Accept Runt Packet */
#define REG_MCMDR_ARP (1 << 2)
/* Accept Long Packet */
#define REG_MCMDR_ALP (1 << 1)
/* Frame Reception On */
#define REG_MCMDR_RXON (1 << 0)

/* REG_MIEN fields */
/* Enable Transmit Descriptor Unavailable Interrupt */
#define REG_MIEN_ENTDU (1 << 23)
/* Enable Transmit Completion Interrupt */
#define REG_MIEN_ENTXCP (1 << 18)
/* Enable Transmit Interrupt */
#define REG_MIEN_ENTXINTR (1 << 16)
/* Enable Receive Descriptor Unavailable Interrupt */
#define REG_MIEN_ENRDU (1 << 10)
/* Enable Receive Good Interrupt */
#define REG_MIEN_ENRXGD (1 << 4)
/* Enable Receive Interrupt */
#define REG_MIEN_ENRXINTR (1 << 0)

/* REG_MISTA fields */
/* TODO: Add error fields and support simulated errors? */
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

/* REG_MGSTA fields */
/* Transmission Halted */
#define REG_MGSTA_TXHA (1 << 11)
/* Receive Halted */
#define REG_MGSTA_RXHA (1 << 11)

/* REG_DMARFC fields */
/* Maximum Receive Frame Length */
#define REG_DMARFC_RXMS(word) extract32((word), 0, 16)

/* REG MIIDA fields */
/* Busy Bit */
#define REG_MIIDA_BUSY (1 << 17)

/* Transmit and receive descriptors */
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
/* CRC append */
#define TX_DESC_FLAG_CRCAPP (1 << 1)
/* Padding enable */
#define TX_DESC_FLAG_PADEN (1 << 0)

/* NPCM7xxEMCTxDesc.status_and_length values */
/* Collision count */
#define TX_DESC_STATUS_CCNT_SHIFT 28
#define TX_DESC_STATUS_CCNT_BITSIZE 4
/* SQE error */
#define TX_DESC_STATUS_SQE (1 << 26)
/* Transmission paused */
#define TX_DESC_STATUS_PAU (1 << 25)
/* P transmission halted */
#define TX_DESC_STATUS_TXHA (1 << 24)
/* Late collision */
#define TX_DESC_STATUS_LC (1 << 23)
/* Transmission abort */
#define TX_DESC_STATUS_TXABT (1 << 22)
/* No carrier sense */
#define TX_DESC_STATUS_NCS (1 << 21)
/* Defer exceed */
#define TX_DESC_STATUS_EXDEF (1 << 20)
/* Transmission complete */
#define TX_DESC_STATUS_TXCP (1 << 19)
/* Transmission deferred */
#define TX_DESC_STATUS_DEF (1 << 17)
/* Transmit interrupt */
#define TX_DESC_STATUS_TXINTR (1 << 16)

#define TX_DESC_PKT_LEN(word) extract32((word), 0, 16)

/* Transmit buffer start address */
#define TX_DESC_TXBSA(word) ((uint32_t) (word) & ~3u)

/* Next transmit descriptor start address */
#define TX_DESC_NTXDSA(word) ((uint32_t) (word) & ~3u)

/* NPCM7xxEMCRxDesc.status_and_length values */
/* Owner: 0b00 = cpu, 0b01 = undefined, 0b10 = emc, 0b11 = undefined */
#define RX_DESC_STATUS_OWNER_SHIFT 30
#define RX_DESC_STATUS_OWNER_BITSIZE 2
#define RX_DESC_STATUS_OWNER_MASK (3 << RX_DESC_STATUS_OWNER_SHIFT)
/* Runt packet */
#define RX_DESC_STATUS_RP (1 << 22)
/* Alignment error */
#define RX_DESC_STATUS_ALIE (1 << 21)
/* Frame reception complete */
#define RX_DESC_STATUS_RXGD (1 << 20)
/* Packet too long */
#define RX_DESC_STATUS_PTLE (1 << 19)
/* CRC error */
#define RX_DESC_STATUS_CRCE (1 << 17)
/* Receive interrupt */
#define RX_DESC_STATUS_RXINTR (1 << 16)

#define RX_DESC_PKT_LEN(word) extract32((word), 0, 16)

/* Receive buffer start address */
#define RX_DESC_RXBSA(word) ((uint32_t) (word) & ~3u)

/* Next receive descriptor start address */
#define RX_DESC_NRXDSA(word) ((uint32_t) (word) & ~3u)

/* Minimum packet length, when TX_DESC_FLAG_PADEN is set. */
#define MIN_PACKET_LENGTH 64

struct NPCM7xxEMCState {
    /*< private >*/
    SysBusDevice parent;
    /*< public >*/

    MemoryRegion iomem;

    qemu_irq tx_irq;
    qemu_irq rx_irq;

    NICState *nic;
    NICConf conf;

    /* 0 or 1, for log messages */
    uint8_t emc_num;

    uint32_t regs[NPCM7XX_NUM_EMC_REGS];

    /*
     * tx is active. Set to true by TSDR and then switches off when out of
     * descriptors. If the TXON bit in REG_MCMDR is off then this is off.
     */
    bool tx_active;

    /*
     * rx is active. Set to true by RSDR and then switches off when out of
     * descriptors. If the RXON bit in REG_MCMDR is off then this is off.
     */
    bool rx_active;
};

#define TYPE_NPCM7XX_EMC "npcm7xx-emc"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxEMCState, NPCM7XX_EMC)

#endif /* NPCM7XX_EMC_H */
