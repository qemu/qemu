/*
 * QEMU e1000(e) emulation - shared code
 *
 * Copyright (c) 2008 Qumranet
 *
 * Based on work done by:
 * Nir Peleg, Tutis Systems Ltd. for Qumranet Inc.
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

#ifndef HW_NET_E1000X_COMMON_H
#define HW_NET_E1000X_COMMON_H

static inline void
e1000x_inc_reg_if_not_full(uint32_t *mac, int index)
{
    if (mac[index] != UINT32_MAX) {
        mac[index]++;
    }
}

static inline void
e1000x_grow_8reg_if_not_full(uint32_t *mac, int index, int size)
{
    uint64_t sum = mac[index] | (uint64_t)mac[index + 1] << 32;

    if (sum + size < sum) {
        sum = ~0ULL;
    } else {
        sum += size;
    }
    mac[index] = sum;
    mac[index + 1] = sum >> 32;
}

static inline int
e1000x_vlan_enabled(uint32_t *mac)
{
    return ((mac[CTRL] & E1000_CTRL_VME) != 0);
}

static inline int
e1000x_is_vlan_txd(uint32_t txd_lower)
{
    return ((txd_lower & E1000_TXD_CMD_VLE) != 0);
}

static inline int
e1000x_vlan_rx_filter_enabled(uint32_t *mac)
{
    return ((mac[RCTL] & E1000_RCTL_VFE) != 0);
}

static inline int
e1000x_fcs_len(uint32_t *mac)
{
    /* FCS aka Ethernet CRC-32. We don't get it from backends and can't
    * fill it in, just pad descriptor length by 4 bytes unless guest
    * told us to strip it off the packet. */
    return (mac[RCTL] & E1000_RCTL_SECRC) ? 0 : 4;
}

static inline void
e1000x_update_regs_on_link_down(uint32_t *mac, uint16_t *phy)
{
    mac[STATUS] &= ~E1000_STATUS_LU;
    phy[MII_BMSR] &= ~MII_BMSR_LINK_ST;
    phy[MII_BMSR] &= ~MII_BMSR_AN_COMP;
    phy[MII_ANLPAR] &= ~MII_ANLPAR_ACK;
}

static inline void
e1000x_update_regs_on_link_up(uint32_t *mac, uint16_t *phy)
{
    mac[STATUS] |= E1000_STATUS_LU;
    phy[MII_BMSR] |= MII_BMSR_LINK_ST;
}

void e1000x_update_rx_total_stats(uint32_t *mac,
                                  eth_pkt_types_e pkt_type,
                                  size_t pkt_size,
                                  size_t pkt_fcs_size);

void e1000x_core_prepare_eeprom(uint16_t       *eeprom,
                                const uint16_t *templ,
                                uint32_t        templ_size,
                                uint16_t        dev_id,
                                const uint8_t  *macaddr);

uint32_t e1000x_rxbufsize(uint32_t rctl);

bool e1000x_rx_ready(PCIDevice *d, uint32_t *mac);

bool e1000x_is_vlan_packet(const void *buf, uint16_t vet);

bool e1000x_rx_group_filter(uint32_t *mac, const uint8_t *buf);

bool e1000x_hw_rx_enabled(uint32_t *mac);

bool e1000x_is_oversized(uint32_t *mac, size_t size);

void e1000x_restart_autoneg(uint32_t *mac, uint16_t *phy, QEMUTimer *timer);

void e1000x_reset_mac_addr(NICState *nic, uint32_t *mac_regs,
                           uint8_t *mac_addr);

void e1000x_update_regs_on_autoneg_done(uint32_t *mac, uint16_t *phy);

void e1000x_increase_size_stats(uint32_t *mac, const int *size_regs, int size);

typedef struct e1000x_txd_props {
    uint8_t ipcss;
    uint8_t ipcso;
    uint16_t ipcse;
    uint8_t tucss;
    uint8_t tucso;
    uint16_t tucse;
    uint32_t paylen;
    uint8_t hdr_len;
    uint16_t mss;
    int8_t ip;
    int8_t tcp;
    bool tse;
} e1000x_txd_props;

void e1000x_read_tx_ctx_descr(struct e1000_context_desc *d,
                              e1000x_txd_props *props);

void e1000x_timestamp(uint32_t *mac, int64_t timadj, size_t lo, size_t hi);
void e1000x_set_timinca(uint32_t *mac, int64_t *timadj, uint32_t val);

#endif
