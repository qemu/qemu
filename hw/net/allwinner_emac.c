/*
 * Emulation of Allwinner EMAC Fast Ethernet controller and
 * Realtek RTL8201CP PHY
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This model is based on reverse-engineering of Linux kernel driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "qemu/fifo8.h"
#include "hw/irq.h"
#include "hw/net/allwinner_emac.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include <zlib.h>

static uint8_t padding[60];

static void mii_set_link(RTL8201CPState *mii, bool link_ok)
{
    if (link_ok) {
        mii->bmsr |= MII_BMSR_LINK_ST | MII_BMSR_AN_COMP;
        mii->anlpar |= MII_ANAR_TXFD | MII_ANAR_10FD | MII_ANAR_10 |
                       MII_ANAR_CSMACD;
    } else {
        mii->bmsr &= ~(MII_BMSR_LINK_ST | MII_BMSR_AN_COMP);
        mii->anlpar = MII_ANAR_TX;
    }
}

static void mii_reset(RTL8201CPState *mii, bool link_ok)
{
    mii->bmcr = MII_BMCR_FD | MII_BMCR_AUTOEN | MII_BMCR_SPEED;
    mii->bmsr = MII_BMSR_100TX_FD | MII_BMSR_100TX_HD | MII_BMSR_10T_FD |
                MII_BMSR_10T_HD | MII_BMSR_MFPS | MII_BMSR_AUTONEG;
    mii->anar = MII_ANAR_TXFD | MII_ANAR_TX | MII_ANAR_10FD | MII_ANAR_10 |
                MII_ANAR_CSMACD;
    mii->anlpar = MII_ANAR_TX;

    mii_set_link(mii, link_ok);
}

static uint16_t RTL8201CP_mdio_read(AwEmacState *s, uint8_t addr, uint8_t reg)
{
    RTL8201CPState *mii = &s->mii;
    uint16_t ret = 0xffff;

    if (addr == s->phy_addr) {
        switch (reg) {
        case MII_BMCR:
            return mii->bmcr;
        case MII_BMSR:
            return mii->bmsr;
        case MII_PHYID1:
            return RTL8201CP_PHYID1;
        case MII_PHYID2:
            return RTL8201CP_PHYID2;
        case MII_ANAR:
            return mii->anar;
        case MII_ANLPAR:
            return mii->anlpar;
        case MII_ANER:
        case MII_NSR:
        case MII_LBREMR:
        case MII_REC:
        case MII_SNRDR:
        case MII_TEST:
            qemu_log_mask(LOG_UNIMP,
                          "allwinner_emac: read from unimpl. mii reg 0x%x\n",
                          reg);
            return 0;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "allwinner_emac: read from invalid mii reg 0x%x\n",
                          reg);
            return 0;
        }
    }
    return ret;
}

static void RTL8201CP_mdio_write(AwEmacState *s, uint8_t addr, uint8_t reg,
                                 uint16_t value)
{
    RTL8201CPState *mii = &s->mii;
    NetClientState *nc;

    if (addr == s->phy_addr) {
        switch (reg) {
        case MII_BMCR:
            if (value & MII_BMCR_RESET) {
                nc = qemu_get_queue(s->nic);
                mii_reset(mii, !nc->link_down);
            } else {
                mii->bmcr = value;
            }
            break;
        case MII_ANAR:
            mii->anar = value;
            break;
        case MII_BMSR:
        case MII_PHYID1:
        case MII_PHYID2:
        case MII_ANLPAR:
        case MII_ANER:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "allwinner_emac: write to read-only mii reg 0x%x\n",
                          reg);
            break;
        case MII_NSR:
        case MII_LBREMR:
        case MII_REC:
        case MII_SNRDR:
        case MII_TEST:
            qemu_log_mask(LOG_UNIMP,
                          "allwinner_emac: write to unimpl. mii reg 0x%x\n",
                          reg);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "allwinner_emac: write to invalid mii reg 0x%x\n",
                          reg);
        }
    }
}

static void aw_emac_update_irq(AwEmacState *s)
{
    qemu_set_irq(s->irq, (s->int_sta & s->int_ctl) != 0);
}

static void aw_emac_tx_reset(AwEmacState *s, int chan)
{
    fifo8_reset(&s->tx_fifo[chan]);
    s->tx_length[chan] = 0;
}

static void aw_emac_rx_reset(AwEmacState *s)
{
    fifo8_reset(&s->rx_fifo);
    s->rx_num_packets = 0;
    s->rx_packet_size = 0;
    s->rx_packet_pos = 0;
}

static void fifo8_push_word(Fifo8 *fifo, uint32_t val)
{
    fifo8_push(fifo, val);
    fifo8_push(fifo, val >> 8);
    fifo8_push(fifo, val >> 16);
    fifo8_push(fifo, val >> 24);
}

static uint32_t fifo8_pop_word(Fifo8 *fifo)
{
    uint32_t ret;

    ret = fifo8_pop(fifo);
    ret |= fifo8_pop(fifo) << 8;
    ret |= fifo8_pop(fifo) << 16;
    ret |= fifo8_pop(fifo) << 24;

    return ret;
}

static bool aw_emac_can_receive(NetClientState *nc)
{
    AwEmacState *s = qemu_get_nic_opaque(nc);

    /*
     * To avoid packet drops, allow reception only when there is space
     * for a full frame: 1522 + 8 (rx headers) + 2 (padding).
     */
    return (s->ctl & EMAC_CTL_RX_EN) && (fifo8_num_free(&s->rx_fifo) >= 1532);
}

static ssize_t aw_emac_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    AwEmacState *s = qemu_get_nic_opaque(nc);
    Fifo8 *fifo = &s->rx_fifo;
    size_t padded_size, total_size;
    uint32_t crc;

    padded_size = size > 60 ? size : 60;
    total_size = QEMU_ALIGN_UP(RX_HDR_SIZE + padded_size + CRC_SIZE, 4);

    if (!(s->ctl & EMAC_CTL_RX_EN) || (fifo8_num_free(fifo) < total_size)) {
        return -1;
    }

    fifo8_push_word(fifo, EMAC_UNDOCUMENTED_MAGIC);
    fifo8_push_word(fifo, EMAC_RX_HEADER(padded_size + CRC_SIZE,
                                         EMAC_RX_IO_DATA_STATUS_OK));
    fifo8_push_all(fifo, buf, size);
    crc = crc32(~0, buf, size);

    if (padded_size != size) {
        fifo8_push_all(fifo, padding, padded_size - size);
        crc = crc32(crc, padding, padded_size - size);
    }

    fifo8_push_word(fifo, crc);
    fifo8_push_all(fifo, padding, QEMU_ALIGN_UP(padded_size, 4) - padded_size);
    s->rx_num_packets++;

    s->int_sta |= EMAC_INT_RX;
    aw_emac_update_irq(s);

    return size;
}

static void aw_emac_reset(DeviceState *dev)
{
    AwEmacState *s = AW_EMAC(dev);
    NetClientState *nc = qemu_get_queue(s->nic);

    s->ctl = 0;
    s->tx_mode = 0;
    s->int_ctl = 0;
    s->int_sta = 0;
    s->tx_channel = 0;
    s->phy_target = 0;

    aw_emac_tx_reset(s, 0);
    aw_emac_tx_reset(s, 1);
    aw_emac_rx_reset(s);

    mii_reset(&s->mii, !nc->link_down);
}

static uint64_t aw_emac_read(void *opaque, hwaddr offset, unsigned size)
{
    AwEmacState *s = opaque;
    Fifo8 *fifo = &s->rx_fifo;
    NetClientState *nc;
    uint64_t ret;

    switch (offset) {
    case EMAC_CTL_REG:
        return s->ctl;
    case EMAC_TX_MODE_REG:
        return s->tx_mode;
    case EMAC_TX_INS_REG:
        return s->tx_channel;
    case EMAC_RX_CTL_REG:
        return s->rx_ctl;
    case EMAC_RX_IO_DATA_REG:
        if (!s->rx_num_packets) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Read IO data register when no packet available");
            return 0;
        }

        ret = fifo8_pop_word(fifo);

        switch (s->rx_packet_pos) {
        case 0:     /* Word is magic header */
            s->rx_packet_pos += 4;
            break;
        case 4:     /* Word is rx info header */
            s->rx_packet_pos += 4;
            s->rx_packet_size = QEMU_ALIGN_UP(extract32(ret, 0, 16), 4);
            break;
        default:    /* Word is packet data */
            s->rx_packet_pos += 4;
            s->rx_packet_size -= 4;

            if (!s->rx_packet_size) {
                s->rx_packet_pos = 0;
                s->rx_num_packets--;
                nc = qemu_get_queue(s->nic);
                if (aw_emac_can_receive(nc)) {
                    qemu_flush_queued_packets(nc);
                }
            }
        }
        return ret;
    case EMAC_RX_FBC_REG:
        return s->rx_num_packets;
    case EMAC_INT_CTL_REG:
        return s->int_ctl;
    case EMAC_INT_STA_REG:
        return s->int_sta;
    case EMAC_MAC_MRDD_REG:
        return RTL8201CP_mdio_read(s,
                                   extract32(s->phy_target, PHY_ADDR_SHIFT, 8),
                                   extract32(s->phy_target, PHY_REG_SHIFT, 8));
    default:
        qemu_log_mask(LOG_UNIMP,
                      "allwinner_emac: read access to unknown register 0x"
                      HWADDR_FMT_plx "\n", offset);
        ret = 0;
    }

    return ret;
}

static void aw_emac_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    AwEmacState *s = opaque;
    Fifo8 *fifo;
    NetClientState *nc = qemu_get_queue(s->nic);
    int chan;

    switch (offset) {
    case EMAC_CTL_REG:
        if (value & EMAC_CTL_RESET) {
            aw_emac_reset(DEVICE(s));
            value &= ~EMAC_CTL_RESET;
        }
        s->ctl = value;
        if (aw_emac_can_receive(nc)) {
            qemu_flush_queued_packets(nc);
        }
        break;
    case EMAC_TX_MODE_REG:
        s->tx_mode = value;
        break;
    case EMAC_TX_CTL0_REG:
    case EMAC_TX_CTL1_REG:
        chan = (offset == EMAC_TX_CTL0_REG ? 0 : 1);
        if ((value & 1) && (s->ctl & EMAC_CTL_TX_EN)) {
            uint32_t len, ret;
            const uint8_t *data;

            fifo = &s->tx_fifo[chan];
            len = s->tx_length[chan];

            if (len > fifo8_num_used(fifo)) {
                len = fifo8_num_used(fifo);
                qemu_log_mask(LOG_GUEST_ERROR,
                              "allwinner_emac: TX length > fifo data length\n");
            }
            if (len > 0) {
                data = fifo8_pop_bufptr(fifo, len, &ret);
                qemu_send_packet(nc, data, ret);
                aw_emac_tx_reset(s, chan);
                /* Raise TX interrupt */
                s->int_sta |= EMAC_INT_TX_CHAN(chan);
                aw_emac_update_irq(s);
            }
        }
        break;
    case EMAC_TX_INS_REG:
        s->tx_channel = value < NUM_TX_FIFOS ? value : 0;
        break;
    case EMAC_TX_PL0_REG:
    case EMAC_TX_PL1_REG:
        chan = (offset == EMAC_TX_PL0_REG ? 0 : 1);
        if (value > TX_FIFO_SIZE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "allwinner_emac: invalid TX frame length %d\n",
                          (int)value);
            value = TX_FIFO_SIZE;
        }
        s->tx_length[chan] = value;
        break;
    case EMAC_TX_IO_DATA_REG:
        fifo = &s->tx_fifo[s->tx_channel];
        if (fifo8_num_free(fifo) < 4) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "allwinner_emac: TX data overruns fifo\n");
            break;
        }
        fifo8_push_word(fifo, value);
        break;
    case EMAC_RX_CTL_REG:
        s->rx_ctl = value;
        break;
    case EMAC_RX_FBC_REG:
        if (value == 0) {
            aw_emac_rx_reset(s);
        }
        break;
    case EMAC_INT_CTL_REG:
        s->int_ctl = value;
        aw_emac_update_irq(s);
        break;
    case EMAC_INT_STA_REG:
        s->int_sta &= ~value;
        aw_emac_update_irq(s);
        break;
    case EMAC_MAC_MADR_REG:
        s->phy_target = value;
        break;
    case EMAC_MAC_MWTD_REG:
        RTL8201CP_mdio_write(s, extract32(s->phy_target, PHY_ADDR_SHIFT, 8),
                             extract32(s->phy_target, PHY_REG_SHIFT, 8), value);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "allwinner_emac: write access to unknown register 0x"
                      HWADDR_FMT_plx "\n", offset);
    }
}

static void aw_emac_set_link(NetClientState *nc)
{
    AwEmacState *s = qemu_get_nic_opaque(nc);

    mii_set_link(&s->mii, !nc->link_down);
}

static const MemoryRegionOps aw_emac_mem_ops = {
    .read = aw_emac_read,
    .write = aw_emac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static NetClientInfo net_aw_emac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = aw_emac_can_receive,
    .receive = aw_emac_receive,
    .link_status_changed = aw_emac_set_link,
};

static void aw_emac_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwEmacState *s = AW_EMAC(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &aw_emac_mem_ops, s,
                          "aw_emac", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void aw_emac_realize(DeviceState *dev, Error **errp)
{
    AwEmacState *s = AW_EMAC(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_aw_emac_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    fifo8_create(&s->rx_fifo, RX_FIFO_SIZE);
    fifo8_create(&s->tx_fifo[0], TX_FIFO_SIZE);
    fifo8_create(&s->tx_fifo[1], TX_FIFO_SIZE);
}

static const Property aw_emac_properties[] = {
    DEFINE_NIC_PROPERTIES(AwEmacState, conf),
    DEFINE_PROP_UINT8("phy-addr", AwEmacState, phy_addr, 0),
};

static const VMStateDescription vmstate_mii = {
    .name = "rtl8201cp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(bmcr, RTL8201CPState),
        VMSTATE_UINT16(bmsr, RTL8201CPState),
        VMSTATE_UINT16(anar, RTL8201CPState),
        VMSTATE_UINT16(anlpar, RTL8201CPState),
        VMSTATE_END_OF_LIST()
    }
};

static int aw_emac_post_load(void *opaque, int version_id)
{
    AwEmacState *s = opaque;

    aw_emac_set_link(qemu_get_queue(s->nic));

    return 0;
}

static const VMStateDescription vmstate_aw_emac = {
    .name = "allwinner_emac",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = aw_emac_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(mii, AwEmacState, 1, vmstate_mii, RTL8201CPState),
        VMSTATE_UINT32(ctl, AwEmacState),
        VMSTATE_UINT32(tx_mode, AwEmacState),
        VMSTATE_UINT32(rx_ctl, AwEmacState),
        VMSTATE_UINT32(int_ctl, AwEmacState),
        VMSTATE_UINT32(int_sta, AwEmacState),
        VMSTATE_UINT32(phy_target, AwEmacState),
        VMSTATE_FIFO8(rx_fifo, AwEmacState),
        VMSTATE_UINT32(rx_num_packets, AwEmacState),
        VMSTATE_UINT32(rx_packet_size, AwEmacState),
        VMSTATE_UINT32(rx_packet_pos, AwEmacState),
        VMSTATE_STRUCT_ARRAY(tx_fifo, AwEmacState, NUM_TX_FIFOS, 1,
                             vmstate_fifo8, Fifo8),
        VMSTATE_UINT32_ARRAY(tx_length, AwEmacState, NUM_TX_FIFOS),
        VMSTATE_UINT32(tx_channel, AwEmacState),
        VMSTATE_END_OF_LIST()
    }
};

static void aw_emac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aw_emac_realize;
    device_class_set_props(dc, aw_emac_properties);
    device_class_set_legacy_reset(dc, aw_emac_reset);
    dc->vmsd = &vmstate_aw_emac;
}

static const TypeInfo aw_emac_info = {
    .name           = TYPE_AW_EMAC,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AwEmacState),
    .instance_init   = aw_emac_init,
    .class_init     = aw_emac_class_init,
};

static void aw_emac_register_types(void)
{
    type_register_static(&aw_emac_info);
}

type_init(aw_emac_register_types)
