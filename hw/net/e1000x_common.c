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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci_device.h"
#include "net/net.h"

#include "e1000x_common.h"

#include "trace.h"

bool e1000x_rx_ready(PCIDevice *d, uint32_t *mac)
{
    bool link_up = mac[STATUS] & E1000_STATUS_LU;
    bool rx_enabled = mac[RCTL] & E1000_RCTL_EN;
    bool pci_master = d->config[PCI_COMMAND] & PCI_COMMAND_MASTER;

    if (!link_up || !rx_enabled || !pci_master) {
        trace_e1000x_rx_can_recv_disabled(link_up, rx_enabled, pci_master);
        return false;
    }

    return true;
}

bool e1000x_is_vlan_packet(const uint8_t *buf, uint16_t vet)
{
    uint16_t eth_proto = lduw_be_p(buf + 12);
    bool res = (eth_proto == vet);

    trace_e1000x_vlan_is_vlan_pkt(res, eth_proto, vet);

    return res;
}

bool e1000x_rx_group_filter(uint32_t *mac, const uint8_t *buf)
{
    static const int mta_shift[] = { 4, 3, 2, 0 };
    uint32_t f, ra[2], *rp, rctl = mac[RCTL];

    for (rp = mac + RA; rp < mac + RA + 32; rp += 2) {
        if (!(rp[1] & E1000_RAH_AV)) {
            continue;
        }
        ra[0] = cpu_to_le32(rp[0]);
        ra[1] = cpu_to_le32(rp[1]);
        if (!memcmp(buf, (uint8_t *)ra, 6)) {
            trace_e1000x_rx_flt_ucast_match((int)(rp - mac - RA) / 2,
                                            MAC_ARG(buf));
            return true;
        }
    }
    trace_e1000x_rx_flt_ucast_mismatch(MAC_ARG(buf));

    f = mta_shift[(rctl >> E1000_RCTL_MO_SHIFT) & 3];
    f = (((buf[5] << 8) | buf[4]) >> f) & 0xfff;
    if (mac[MTA + (f >> 5)] & (1 << (f & 0x1f))) {
        e1000x_inc_reg_if_not_full(mac, MPRC);
        return true;
    }

    trace_e1000x_rx_flt_inexact_mismatch(MAC_ARG(buf),
                                         (rctl >> E1000_RCTL_MO_SHIFT) & 3,
                                         f >> 5,
                                         mac[MTA + (f >> 5)]);

    return false;
}

bool e1000x_hw_rx_enabled(uint32_t *mac)
{
    if (!(mac[STATUS] & E1000_STATUS_LU)) {
        trace_e1000x_rx_link_down(mac[STATUS]);
        return false;
    }

    if (!(mac[RCTL] & E1000_RCTL_EN)) {
        trace_e1000x_rx_disabled(mac[RCTL]);
        return false;
    }

    return true;
}

bool e1000x_is_oversized(uint32_t *mac, size_t size)
{
    /* this is the size past which hardware will
       drop packets when setting LPE=0 */
    static const int maximum_ethernet_vlan_size = 1522;
    /* this is the size past which hardware will
       drop packets when setting LPE=1 */
    static const int maximum_ethernet_lpe_size = 16 * KiB;

    if ((size > maximum_ethernet_lpe_size ||
        (size > maximum_ethernet_vlan_size
            && !(mac[RCTL] & E1000_RCTL_LPE)))
        && !(mac[RCTL] & E1000_RCTL_SBP)) {
        e1000x_inc_reg_if_not_full(mac, ROC);
        trace_e1000x_rx_oversized(size);
        return true;
    }

    return false;
}

void e1000x_restart_autoneg(uint32_t *mac, uint16_t *phy, QEMUTimer *timer)
{
    e1000x_update_regs_on_link_down(mac, phy);
    trace_e1000x_link_negotiation_start();
    timer_mod(timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 500);
}

void e1000x_reset_mac_addr(NICState *nic, uint32_t *mac_regs,
                           uint8_t *mac_addr)
{
    int i;

    mac_regs[RA] = 0;
    mac_regs[RA + 1] = E1000_RAH_AV;
    for (i = 0; i < 4; i++) {
        mac_regs[RA] |= mac_addr[i] << (8 * i);
        mac_regs[RA + 1] |=
            (i < 2) ? mac_addr[i + 4] << (8 * i) : 0;
    }

    qemu_format_nic_info_str(qemu_get_queue(nic), mac_addr);
    trace_e1000x_mac_indicate(MAC_ARG(mac_addr));
}

void e1000x_update_regs_on_autoneg_done(uint32_t *mac, uint16_t *phy)
{
    e1000x_update_regs_on_link_up(mac, phy);
    phy[PHY_LP_ABILITY] |= MII_LPAR_LPACK;
    phy[PHY_STATUS] |= MII_SR_AUTONEG_COMPLETE;
    trace_e1000x_link_negotiation_done();
}

void
e1000x_core_prepare_eeprom(uint16_t       *eeprom,
                           const uint16_t *templ,
                           uint32_t        templ_size,
                           uint16_t        dev_id,
                           const uint8_t  *macaddr)
{
    uint16_t checksum = 0;
    int i;

    memmove(eeprom, templ, templ_size);

    for (i = 0; i < 3; i++) {
        eeprom[i] = (macaddr[2 * i + 1] << 8) | macaddr[2 * i];
    }

    eeprom[11] = eeprom[13] = dev_id;

    for (i = 0; i < EEPROM_CHECKSUM_REG; i++) {
        checksum += eeprom[i];
    }

    checksum = (uint16_t) EEPROM_SUM - checksum;

    eeprom[EEPROM_CHECKSUM_REG] = checksum;
}

uint32_t
e1000x_rxbufsize(uint32_t rctl)
{
    rctl &= E1000_RCTL_BSEX | E1000_RCTL_SZ_16384 | E1000_RCTL_SZ_8192 |
        E1000_RCTL_SZ_4096 | E1000_RCTL_SZ_2048 | E1000_RCTL_SZ_1024 |
        E1000_RCTL_SZ_512 | E1000_RCTL_SZ_256;
    switch (rctl) {
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_16384:
        return 16384;
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_8192:
        return 8192;
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_4096:
        return 4096;
    case E1000_RCTL_SZ_1024:
        return 1024;
    case E1000_RCTL_SZ_512:
        return 512;
    case E1000_RCTL_SZ_256:
        return 256;
    }
    return 2048;
}

void
e1000x_update_rx_total_stats(uint32_t *mac,
                             size_t data_size,
                             size_t data_fcs_size)
{
    static const int PRCregs[6] = { PRC64, PRC127, PRC255, PRC511,
                                    PRC1023, PRC1522 };

    e1000x_increase_size_stats(mac, PRCregs, data_fcs_size);
    e1000x_inc_reg_if_not_full(mac, TPR);
    mac[GPRC] = mac[TPR];
    /* TOR - Total Octets Received:
    * This register includes bytes received in a packet from the <Destination
    * Address> field through the <CRC> field, inclusively.
    * Always include FCS length (4) in size.
    */
    e1000x_grow_8reg_if_not_full(mac, TORL, data_size + 4);
    mac[GORCL] = mac[TORL];
    mac[GORCH] = mac[TORH];
}

void
e1000x_increase_size_stats(uint32_t *mac, const int *size_regs, int size)
{
    if (size > 1023) {
        e1000x_inc_reg_if_not_full(mac, size_regs[5]);
    } else if (size > 511) {
        e1000x_inc_reg_if_not_full(mac, size_regs[4]);
    } else if (size > 255) {
        e1000x_inc_reg_if_not_full(mac, size_regs[3]);
    } else if (size > 127) {
        e1000x_inc_reg_if_not_full(mac, size_regs[2]);
    } else if (size > 64) {
        e1000x_inc_reg_if_not_full(mac, size_regs[1]);
    } else if (size == 64) {
        e1000x_inc_reg_if_not_full(mac, size_regs[0]);
    }
}

void
e1000x_read_tx_ctx_descr(struct e1000_context_desc *d,
                         e1000x_txd_props *props)
{
    uint32_t op = le32_to_cpu(d->cmd_and_length);

    props->ipcss = d->lower_setup.ip_fields.ipcss;
    props->ipcso = d->lower_setup.ip_fields.ipcso;
    props->ipcse = le16_to_cpu(d->lower_setup.ip_fields.ipcse);
    props->tucss = d->upper_setup.tcp_fields.tucss;
    props->tucso = d->upper_setup.tcp_fields.tucso;
    props->tucse = le16_to_cpu(d->upper_setup.tcp_fields.tucse);
    props->paylen = op & 0xfffff;
    props->hdr_len = d->tcp_seg_setup.fields.hdr_len;
    props->mss = le16_to_cpu(d->tcp_seg_setup.fields.mss);
    props->ip = (op & E1000_TXD_CMD_IP) ? 1 : 0;
    props->tcp = (op & E1000_TXD_CMD_TCP) ? 1 : 0;
    props->tse = (op & E1000_TXD_CMD_TSE) ? 1 : 0;
}
