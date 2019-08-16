/*
 * Emulation of Allwinner EMAC Fast Ethernet controller and
 * Realtek RTL8201CP PHY
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * Allwinner EMAC register definitions from Linux kernel are:
 *   Copyright 2012 Stefan Roese <sr@denx.de>
 *   Copyright 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *   Copyright 1997 Sten Wang
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef ALLWINNER_EMAC_H
#define ALLWINNER_EMAC_H

#include "qemu/units.h"
#include "net/net.h"
#include "qemu/fifo8.h"
#include "hw/net/mii.h"
#include "hw/sysbus.h"

#define TYPE_AW_EMAC "allwinner-emac"
#define AW_EMAC(obj) OBJECT_CHECK(AwEmacState, (obj), TYPE_AW_EMAC)

/*
 * Allwinner EMAC register list
 */
#define EMAC_CTL_REG            0x00

#define EMAC_TX_MODE_REG        0x04
#define EMAC_TX_FLOW_REG        0x08
#define EMAC_TX_CTL0_REG        0x0C
#define EMAC_TX_CTL1_REG        0x10
#define EMAC_TX_INS_REG         0x14
#define EMAC_TX_PL0_REG         0x18
#define EMAC_TX_PL1_REG         0x1C
#define EMAC_TX_STA_REG         0x20
#define EMAC_TX_IO_DATA_REG     0x24
#define EMAC_TX_IO_DATA1_REG    0x28
#define EMAC_TX_TSVL0_REG       0x2C
#define EMAC_TX_TSVH0_REG       0x30
#define EMAC_TX_TSVL1_REG       0x34
#define EMAC_TX_TSVH1_REG       0x38

#define EMAC_RX_CTL_REG         0x3C
#define EMAC_RX_HASH0_REG       0x40
#define EMAC_RX_HASH1_REG       0x44
#define EMAC_RX_STA_REG         0x48
#define EMAC_RX_IO_DATA_REG     0x4C
#define EMAC_RX_FBC_REG         0x50

#define EMAC_INT_CTL_REG        0x54
#define EMAC_INT_STA_REG        0x58

#define EMAC_MAC_CTL0_REG       0x5C
#define EMAC_MAC_CTL1_REG       0x60
#define EMAC_MAC_IPGT_REG       0x64
#define EMAC_MAC_IPGR_REG       0x68
#define EMAC_MAC_CLRT_REG       0x6C
#define EMAC_MAC_MAXF_REG       0x70
#define EMAC_MAC_SUPP_REG       0x74
#define EMAC_MAC_TEST_REG       0x78
#define EMAC_MAC_MCFG_REG       0x7C
#define EMAC_MAC_MCMD_REG       0x80
#define EMAC_MAC_MADR_REG       0x84
#define EMAC_MAC_MWTD_REG       0x88
#define EMAC_MAC_MRDD_REG       0x8C
#define EMAC_MAC_MIND_REG       0x90
#define EMAC_MAC_SSRR_REG       0x94
#define EMAC_MAC_A0_REG         0x98
#define EMAC_MAC_A1_REG         0x9C
#define EMAC_MAC_A2_REG         0xA0

#define EMAC_SAFX_L_REG0        0xA4
#define EMAC_SAFX_H_REG0        0xA8
#define EMAC_SAFX_L_REG1        0xAC
#define EMAC_SAFX_H_REG1        0xB0
#define EMAC_SAFX_L_REG2        0xB4
#define EMAC_SAFX_H_REG2        0xB8
#define EMAC_SAFX_L_REG3        0xBC
#define EMAC_SAFX_H_REG3        0xC0

/* CTL register fields */
#define EMAC_CTL_RESET                  (1 << 0)
#define EMAC_CTL_TX_EN                  (1 << 1)
#define EMAC_CTL_RX_EN                  (1 << 2)

/* TX MODE register fields */
#define EMAC_TX_MODE_ABORTED_FRAME_EN   (1 << 0)
#define EMAC_TX_MODE_DMA_EN             (1 << 1)

/* RX CTL register fields */
#define EMAC_RX_CTL_AUTO_DRQ_EN         (1 << 1)
#define EMAC_RX_CTL_DMA_EN              (1 << 2)
#define EMAC_RX_CTL_PASS_ALL_EN         (1 << 4)
#define EMAC_RX_CTL_PASS_CTL_EN         (1 << 5)
#define EMAC_RX_CTL_PASS_CRC_ERR_EN     (1 << 6)
#define EMAC_RX_CTL_PASS_LEN_ERR_EN     (1 << 7)
#define EMAC_RX_CTL_PASS_LEN_OOR_EN     (1 << 8)
#define EMAC_RX_CTL_ACCEPT_UNICAST_EN   (1 << 16)
#define EMAC_RX_CTL_DA_FILTER_EN        (1 << 17)
#define EMAC_RX_CTL_ACCEPT_MULTICAST_EN (1 << 20)
#define EMAC_RX_CTL_HASH_FILTER_EN      (1 << 21)
#define EMAC_RX_CTL_ACCEPT_BROADCAST_EN (1 << 22)
#define EMAC_RX_CTL_SA_FILTER_EN        (1 << 24)
#define EMAC_RX_CTL_SA_FILTER_INVERT_EN (1 << 25)

/* RX IO DATA register fields */
#define EMAC_RX_HEADER(len, status)     (((len) & 0xffff) | ((status) << 16))
#define EMAC_RX_IO_DATA_STATUS_CRC_ERR  (1 << 4)
#define EMAC_RX_IO_DATA_STATUS_LEN_ERR  (3 << 5)
#define EMAC_RX_IO_DATA_STATUS_OK       (1 << 7)
#define EMAC_UNDOCUMENTED_MAGIC         0x0143414d  /* header for RX frames */

/* INT CTL and INT STA registers fields */
#define EMAC_INT_TX_CHAN(x) (1 << (x))
#define EMAC_INT_RX         (1 << 8)

/* Due to lack of specifications, size of fifos is chosen arbitrarily */
#define TX_FIFO_SIZE        (4 * KiB)
#define RX_FIFO_SIZE        (32 * KiB)

#define NUM_TX_FIFOS        2
#define RX_HDR_SIZE         8
#define CRC_SIZE            4

#define PHY_REG_SHIFT       0
#define PHY_ADDR_SHIFT      8

typedef struct RTL8201CPState {
    uint16_t bmcr;
    uint16_t bmsr;
    uint16_t anar;
    uint16_t anlpar;
} RTL8201CPState;

typedef struct AwEmacState {
    /*< private >*/
    SysBusDevice  parent_obj;
    /*< public >*/

    MemoryRegion   iomem;
    qemu_irq       irq;
    NICState       *nic;
    NICConf        conf;
    RTL8201CPState mii;
    uint8_t        phy_addr;

    uint32_t       ctl;
    uint32_t       tx_mode;
    uint32_t       rx_ctl;
    uint32_t       int_ctl;
    uint32_t       int_sta;
    uint32_t       phy_target;

    Fifo8          rx_fifo;
    uint32_t       rx_num_packets;
    uint32_t       rx_packet_size;
    uint32_t       rx_packet_pos;

    Fifo8          tx_fifo[NUM_TX_FIFOS];
    uint32_t       tx_length[NUM_TX_FIFOS];
    uint32_t       tx_channel;
} AwEmacState;

#endif
