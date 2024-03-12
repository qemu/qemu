/*
 * Core code for QEMU e1000e emulation
 *
 * Software developer's manuals:
 * http://www.intel.com/content/dam/doc/datasheet/82574l-gbe-controller-datasheet.pdf
 *
 * Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Leonid Bloch <leonid@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * Based on work done by:
 * Nir Peleg, Tutis Systems Ltd. for Qumranet Inc.
 * Copyright (c) 2008 Qumranet
 * Based on work done by:
 * Copyright (c) 2007 Dan Aloni
 * Copyright (c) 2004 Antony T Curtis
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NET_E1000E_CORE_H
#define HW_NET_E1000E_CORE_H

#define E1000E_PHY_PAGE_SIZE    (0x20)
#define E1000E_PHY_PAGES        (0x07)
#define E1000E_MAC_SIZE         (0x8000)
#define E1000E_EEPROM_SIZE      (64)
#define E1000E_MSIX_VEC_NUM     (5)
#define E1000E_NUM_QUEUES       (2)

typedef struct E1000Core E1000ECore;

enum { PHY_R = BIT(0),
       PHY_W = BIT(1),
       PHY_RW = PHY_R | PHY_W,
       PHY_ANYPAGE = BIT(2) };

typedef struct E1000IntrDelayTimer_st {
    QEMUTimer *timer;
    bool running;
    uint32_t delay_reg;
    uint32_t delay_resolution_ns;
    E1000ECore *core;
} E1000IntrDelayTimer;

struct E1000Core {
    uint32_t mac[E1000E_MAC_SIZE];
    uint16_t phy[E1000E_PHY_PAGES][E1000E_PHY_PAGE_SIZE];
    uint16_t eeprom[E1000E_EEPROM_SIZE];

    uint32_t rxbuf_sizes[E1000_PSRCTL_BUFFS_PER_DESC];
    uint32_t rx_desc_buf_size;
    uint32_t rxbuf_min_shift;
    uint8_t rx_desc_len;

    QEMUTimer *autoneg_timer;

    struct e1000e_tx {
        e1000x_txd_props props;

        bool skip_cp;
        unsigned char sum_needed;
        bool cptse;
        struct NetTxPkt *tx_pkt;
    } tx[E1000E_NUM_QUEUES];

    struct NetRxPkt *rx_pkt;

    bool has_vnet;
    int max_queue_num;

    /* Interrupt moderation management */
    uint32_t delayed_causes;

    E1000IntrDelayTimer radv;
    E1000IntrDelayTimer rdtr;
    E1000IntrDelayTimer raid;

    E1000IntrDelayTimer tadv;
    E1000IntrDelayTimer tidv;

    E1000IntrDelayTimer itr;

    E1000IntrDelayTimer eitr[E1000E_MSIX_VEC_NUM];

    uint32_t itr_guest_value;
    uint32_t eitr_guest_value[E1000E_MSIX_VEC_NUM];

    uint16_t vet;

    uint8_t permanent_mac[ETH_ALEN];

    NICState *owner_nic;
    PCIDevice *owner;
    void (*owner_start_recv)(PCIDevice *d);

    int64_t timadj;
};

void
e1000e_core_write(E1000ECore *core, hwaddr addr, uint64_t val, unsigned size);

uint64_t
e1000e_core_read(E1000ECore *core, hwaddr addr, unsigned size);

void
e1000e_core_pci_realize(E1000ECore      *regs,
                       const uint16_t *eeprom_templ,
                       uint32_t        eeprom_size,
                       const uint8_t  *macaddr);

void
e1000e_core_reset(E1000ECore *core);

void
e1000e_core_pre_save(E1000ECore *core);

int
e1000e_core_post_load(E1000ECore *core);

void
e1000e_core_set_link_status(E1000ECore *core);

void
e1000e_core_pci_uninit(E1000ECore *core);

bool
e1000e_can_receive(E1000ECore *core);

ssize_t
e1000e_receive(E1000ECore *core, const uint8_t *buf, size_t size);

ssize_t
e1000e_receive_iov(E1000ECore *core, const struct iovec *iov, int iovcnt);

void
e1000e_start_recv(E1000ECore *core);

#endif
