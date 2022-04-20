/*
 * QEMU model of the Smartfusion2 Ethernet MAC.
 *
 * Copyright (c) 2020 Subbaraya Sundeep <sundeep.lkml@gmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Refer to section Ethernet MAC in the document:
 * UG0331: SmartFusion2 Microcontroller Subsystem User Guide
 * Datasheet URL:
 * https://www.microsemi.com/document-portal/cat_view/56661-internal-documents/
 * 56758-soc?lang=en&limit=20&limitstart=220
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/registerfields.h"
#include "hw/net/msf2-emac.h"
#include "hw/net/mii.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

REG32(CFG1, 0x0)
    FIELD(CFG1, RESET, 31, 1)
    FIELD(CFG1, RX_EN, 2, 1)
    FIELD(CFG1, TX_EN, 0, 1)
    FIELD(CFG1, LB_EN, 8, 1)
REG32(CFG2, 0x4)
REG32(IFG, 0x8)
REG32(HALF_DUPLEX, 0xc)
REG32(MAX_FRAME_LENGTH, 0x10)
REG32(MII_CMD, 0x24)
    FIELD(MII_CMD, READ, 0, 1)
REG32(MII_ADDR, 0x28)
    FIELD(MII_ADDR, REGADDR, 0, 5)
    FIELD(MII_ADDR, PHYADDR, 8, 5)
REG32(MII_CTL, 0x2c)
REG32(MII_STS, 0x30)
REG32(STA1, 0x40)
REG32(STA2, 0x44)
REG32(FIFO_CFG0, 0x48)
REG32(FIFO_CFG4, 0x58)
    FIELD(FIFO_CFG4, BCAST, 9, 1)
    FIELD(FIFO_CFG4, MCAST, 8, 1)
REG32(FIFO_CFG5, 0x5C)
    FIELD(FIFO_CFG5, BCAST, 9, 1)
    FIELD(FIFO_CFG5, MCAST, 8, 1)
REG32(DMA_TX_CTL, 0x180)
    FIELD(DMA_TX_CTL, EN, 0, 1)
REG32(DMA_TX_DESC, 0x184)
REG32(DMA_TX_STATUS, 0x188)
    FIELD(DMA_TX_STATUS, PKTCNT, 16, 8)
    FIELD(DMA_TX_STATUS, UNDERRUN, 1, 1)
    FIELD(DMA_TX_STATUS, PKT_SENT, 0, 1)
REG32(DMA_RX_CTL, 0x18c)
    FIELD(DMA_RX_CTL, EN, 0, 1)
REG32(DMA_RX_DESC, 0x190)
REG32(DMA_RX_STATUS, 0x194)
    FIELD(DMA_RX_STATUS, PKTCNT, 16, 8)
    FIELD(DMA_RX_STATUS, OVERFLOW, 2, 1)
    FIELD(DMA_RX_STATUS, PKT_RCVD, 0, 1)
REG32(DMA_IRQ_MASK, 0x198)
REG32(DMA_IRQ, 0x19c)

#define EMPTY_MASK              (1 << 31)
#define PKT_SIZE                0x7FF
#define PHYADDR                 0x1
#define MAX_PKT_SIZE            2048

typedef struct {
    uint32_t pktaddr;
    uint32_t pktsize;
    uint32_t next;
} EmacDesc;

static uint32_t emac_get_isr(MSF2EmacState *s)
{
    uint32_t ier = s->regs[R_DMA_IRQ_MASK];
    uint32_t tx = s->regs[R_DMA_TX_STATUS] & 0xF;
    uint32_t rx = s->regs[R_DMA_RX_STATUS] & 0xF;
    uint32_t isr = (rx << 4) | tx;

    s->regs[R_DMA_IRQ] = ier & isr;
    return s->regs[R_DMA_IRQ];
}

static void emac_update_irq(MSF2EmacState *s)
{
    bool intr = emac_get_isr(s);

    qemu_set_irq(s->irq, intr);
}

static void emac_load_desc(MSF2EmacState *s, EmacDesc *d, hwaddr desc)
{
    address_space_read(&s->dma_as, desc, MEMTXATTRS_UNSPECIFIED, d, sizeof *d);
    /* Convert from LE into host endianness. */
    d->pktaddr = le32_to_cpu(d->pktaddr);
    d->pktsize = le32_to_cpu(d->pktsize);
    d->next = le32_to_cpu(d->next);
}

static void emac_store_desc(MSF2EmacState *s, EmacDesc *d, hwaddr desc)
{
    /* Convert from host endianness into LE. */
    d->pktaddr = cpu_to_le32(d->pktaddr);
    d->pktsize = cpu_to_le32(d->pktsize);
    d->next = cpu_to_le32(d->next);

    address_space_write(&s->dma_as, desc, MEMTXATTRS_UNSPECIFIED, d, sizeof *d);
}

static void msf2_dma_tx(MSF2EmacState *s)
{
    NetClientState *nc = qemu_get_queue(s->nic);
    hwaddr desc = s->regs[R_DMA_TX_DESC];
    uint8_t buf[MAX_PKT_SIZE];
    EmacDesc d;
    int size;
    uint8_t pktcnt;
    uint32_t status;

    if (!(s->regs[R_CFG1] & R_CFG1_TX_EN_MASK)) {
        return;
    }

    while (1) {
        emac_load_desc(s, &d, desc);
        if (d.pktsize & EMPTY_MASK) {
            break;
        }
        size = d.pktsize & PKT_SIZE;
        address_space_read(&s->dma_as, d.pktaddr, MEMTXATTRS_UNSPECIFIED,
                           buf, size);
        /*
         * This is very basic way to send packets. Ideally there should be
         * a FIFO and packets should be sent out from FIFO only when
         * R_CFG1 bit 0 is set.
         */
        if (s->regs[R_CFG1] & R_CFG1_LB_EN_MASK) {
            qemu_receive_packet(nc, buf, size);
        } else {
            qemu_send_packet(nc, buf, size);
        }
        d.pktsize |= EMPTY_MASK;
        emac_store_desc(s, &d, desc);
        /* update sent packets count */
        status = s->regs[R_DMA_TX_STATUS];
        pktcnt = FIELD_EX32(status, DMA_TX_STATUS, PKTCNT);
        pktcnt++;
        s->regs[R_DMA_TX_STATUS] = FIELD_DP32(status, DMA_TX_STATUS,
                                              PKTCNT, pktcnt);
        s->regs[R_DMA_TX_STATUS] |= R_DMA_TX_STATUS_PKT_SENT_MASK;
        desc = d.next;
    }
    s->regs[R_DMA_TX_STATUS] |= R_DMA_TX_STATUS_UNDERRUN_MASK;
    s->regs[R_DMA_TX_CTL] &= ~R_DMA_TX_CTL_EN_MASK;
}

static void msf2_phy_update_link(MSF2EmacState *s)
{
    /* Autonegotiation status mirrors link status. */
    if (qemu_get_queue(s->nic)->link_down) {
        s->phy_regs[MII_BMSR] &= ~(MII_BMSR_AN_COMP |
                                   MII_BMSR_LINK_ST);
    } else {
        s->phy_regs[MII_BMSR] |= (MII_BMSR_AN_COMP |
                                  MII_BMSR_LINK_ST);
    }
}

static void msf2_phy_reset(MSF2EmacState *s)
{
    memset(&s->phy_regs[0], 0, sizeof(s->phy_regs));
    s->phy_regs[MII_BMCR] = 0x1140;
    s->phy_regs[MII_BMSR] = 0x7968;
    s->phy_regs[MII_PHYID1] = 0x0022;
    s->phy_regs[MII_PHYID2] = 0x1550;
    s->phy_regs[MII_ANAR] = 0x01E1;
    s->phy_regs[MII_ANLPAR] = 0xCDE1;

    msf2_phy_update_link(s);
}

static void write_to_phy(MSF2EmacState *s)
{
    uint8_t reg_addr = s->regs[R_MII_ADDR] & R_MII_ADDR_REGADDR_MASK;
    uint8_t phy_addr = (s->regs[R_MII_ADDR] >> R_MII_ADDR_PHYADDR_SHIFT) &
                       R_MII_ADDR_REGADDR_MASK;
    uint16_t data = s->regs[R_MII_CTL] & 0xFFFF;

    if (phy_addr != PHYADDR) {
        return;
    }

    switch (reg_addr) {
    case MII_BMCR:
        if (data & MII_BMCR_RESET) {
            /* Phy reset */
            msf2_phy_reset(s);
            data &= ~MII_BMCR_RESET;
        }
        if (data & MII_BMCR_AUTOEN) {
            /* Complete autonegotiation immediately */
            data &= ~MII_BMCR_AUTOEN;
            s->phy_regs[MII_BMSR] |= MII_BMSR_AN_COMP;
        }
        break;
    }

    s->phy_regs[reg_addr] = data;
}

static uint16_t read_from_phy(MSF2EmacState *s)
{
    uint8_t reg_addr = s->regs[R_MII_ADDR] & R_MII_ADDR_REGADDR_MASK;
    uint8_t phy_addr = (s->regs[R_MII_ADDR] >> R_MII_ADDR_PHYADDR_SHIFT) &
                       R_MII_ADDR_REGADDR_MASK;

    if (phy_addr == PHYADDR) {
        return s->phy_regs[reg_addr];
    } else {
        return 0xFFFF;
    }
}

static void msf2_emac_do_reset(MSF2EmacState *s)
{
    memset(&s->regs[0], 0, sizeof(s->regs));
    s->regs[R_CFG1] = 0x80000000;
    s->regs[R_CFG2] = 0x00007000;
    s->regs[R_IFG] = 0x40605060;
    s->regs[R_HALF_DUPLEX] = 0x00A1F037;
    s->regs[R_MAX_FRAME_LENGTH] = 0x00000600;
    s->regs[R_FIFO_CFG5] = 0X3FFFF;

    msf2_phy_reset(s);
}

static uint64_t emac_read(void *opaque, hwaddr addr, unsigned int size)
{
    MSF2EmacState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;

    switch (addr) {
    case R_DMA_IRQ:
        r = emac_get_isr(s);
        break;
    default:
        if (addr >= ARRAY_SIZE(s->regs)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__,
                          addr * 4);
            return r;
        }
        r = s->regs[addr];
        break;
    }
    return r;
}

static void emac_write(void *opaque, hwaddr addr, uint64_t val64,
        unsigned int size)
{
    MSF2EmacState *s = opaque;
    uint32_t value = val64;
    uint32_t enreqbits;
    uint8_t pktcnt;

    addr >>= 2;
    switch (addr) {
    case R_DMA_TX_CTL:
        s->regs[addr] = value;
        if (value & R_DMA_TX_CTL_EN_MASK) {
            msf2_dma_tx(s);
        }
        break;
    case R_DMA_RX_CTL:
        s->regs[addr] = value;
        if (value & R_DMA_RX_CTL_EN_MASK) {
            s->rx_desc = s->regs[R_DMA_RX_DESC];
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case R_CFG1:
        s->regs[addr] = value;
        if (value & R_CFG1_RESET_MASK) {
            msf2_emac_do_reset(s);
        }
        break;
    case R_FIFO_CFG0:
       /*
        * For our implementation, turning on modules is instantaneous,
        * so the states requested via the *ENREQ bits appear in the
        * *ENRPLY bits immediately. Also the reset bits to reset PE-MCXMAC
        * module are not emulated here since it deals with start of frames,
        * inter-packet gap and control frames.
        */
        enreqbits = extract32(value, 8, 5);
        s->regs[addr] = deposit32(value, 16, 5, enreqbits);
        break;
    case R_DMA_TX_DESC:
        if (value & 0x3) {
            qemu_log_mask(LOG_GUEST_ERROR, "Tx Descriptor address should be"
                          " 32 bit aligned\n");
        }
        /* Ignore [1:0] bits */
        s->regs[addr] = value & ~3;
        break;
    case R_DMA_RX_DESC:
        if (value & 0x3) {
            qemu_log_mask(LOG_GUEST_ERROR, "Rx Descriptor address should be"
                          " 32 bit aligned\n");
        }
        /* Ignore [1:0] bits */
        s->regs[addr] = value & ~3;
        break;
    case R_DMA_TX_STATUS:
        if (value & R_DMA_TX_STATUS_UNDERRUN_MASK) {
            s->regs[addr] &= ~R_DMA_TX_STATUS_UNDERRUN_MASK;
        }
        if (value & R_DMA_TX_STATUS_PKT_SENT_MASK) {
            pktcnt = FIELD_EX32(s->regs[addr], DMA_TX_STATUS, PKTCNT);
            pktcnt--;
            s->regs[addr] = FIELD_DP32(s->regs[addr], DMA_TX_STATUS,
                                       PKTCNT, pktcnt);
            if (pktcnt == 0) {
                s->regs[addr] &= ~R_DMA_TX_STATUS_PKT_SENT_MASK;
            }
        }
        break;
    case R_DMA_RX_STATUS:
        if (value & R_DMA_RX_STATUS_OVERFLOW_MASK) {
            s->regs[addr] &= ~R_DMA_RX_STATUS_OVERFLOW_MASK;
        }
        if (value & R_DMA_RX_STATUS_PKT_RCVD_MASK) {
            pktcnt = FIELD_EX32(s->regs[addr], DMA_RX_STATUS, PKTCNT);
            pktcnt--;
            s->regs[addr] = FIELD_DP32(s->regs[addr], DMA_RX_STATUS,
                                       PKTCNT, pktcnt);
            if (pktcnt == 0) {
                s->regs[addr] &= ~R_DMA_RX_STATUS_PKT_RCVD_MASK;
            }
        }
        break;
    case R_DMA_IRQ:
        break;
    case R_MII_CMD:
        if (value & R_MII_CMD_READ_MASK) {
            s->regs[R_MII_STS] = read_from_phy(s);
        }
        break;
    case R_MII_CTL:
        s->regs[addr] = value;
        write_to_phy(s);
        break;
    case R_STA1:
        s->regs[addr] = value;
       /*
        * R_STA1 [31:24] : octet 1 of mac address
        * R_STA1 [23:16] : octet 2 of mac address
        * R_STA1 [15:8] : octet 3 of mac address
        * R_STA1 [7:0] : octet 4 of mac address
        */
        stl_be_p(s->mac_addr, value);
        break;
    case R_STA2:
        s->regs[addr] = value;
       /*
        * R_STA2 [31:24] : octet 5 of mac address
        * R_STA2 [23:16] : octet 6 of mac address
        */
        stw_be_p(s->mac_addr + 4, value >> 16);
        break;
    default:
        if (addr >= ARRAY_SIZE(s->regs)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__,
                          addr * 4);
            return;
        }
        s->regs[addr] = value;
        break;
    }
    emac_update_irq(s);
}

static const MemoryRegionOps emac_ops = {
    .read = emac_read,
    .write = emac_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static bool emac_can_rx(NetClientState *nc)
{
    MSF2EmacState *s = qemu_get_nic_opaque(nc);

    return (s->regs[R_CFG1] & R_CFG1_RX_EN_MASK) &&
           (s->regs[R_DMA_RX_CTL] & R_DMA_RX_CTL_EN_MASK);
}

static bool addr_filter_ok(MSF2EmacState *s, const uint8_t *buf)
{
    /* The broadcast MAC address: FF:FF:FF:FF:FF:FF */
    const uint8_t broadcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF };
    bool bcast_en = true;
    bool mcast_en = true;

    if (s->regs[R_FIFO_CFG5] & R_FIFO_CFG5_BCAST_MASK) {
        bcast_en = true; /* Broadcast dont care for drop circuitry */
    } else if (s->regs[R_FIFO_CFG4] & R_FIFO_CFG4_BCAST_MASK) {
        bcast_en = false;
    }

    if (s->regs[R_FIFO_CFG5] & R_FIFO_CFG5_MCAST_MASK) {
        mcast_en = true; /* Multicast dont care for drop circuitry */
    } else if (s->regs[R_FIFO_CFG4] & R_FIFO_CFG4_MCAST_MASK) {
        mcast_en = false;
    }

    if (!memcmp(buf, broadcast_addr, sizeof(broadcast_addr))) {
        return bcast_en;
    }

    if (buf[0] & 1) {
        return mcast_en;
    }

    return !memcmp(buf, s->mac_addr, sizeof(s->mac_addr));
}

static ssize_t emac_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    MSF2EmacState *s = qemu_get_nic_opaque(nc);
    EmacDesc d;
    uint8_t pktcnt;
    uint32_t status;

    if (size > (s->regs[R_MAX_FRAME_LENGTH] & 0xFFFF)) {
        return size;
    }
    if (!addr_filter_ok(s, buf)) {
        return size;
    }

    emac_load_desc(s, &d, s->rx_desc);

    if (d.pktsize & EMPTY_MASK) {
        address_space_write(&s->dma_as, d.pktaddr, MEMTXATTRS_UNSPECIFIED,
                            buf, size & PKT_SIZE);
        d.pktsize = size & PKT_SIZE;
        emac_store_desc(s, &d, s->rx_desc);
        /* update received packets count */
        status = s->regs[R_DMA_RX_STATUS];
        pktcnt = FIELD_EX32(status, DMA_RX_STATUS, PKTCNT);
        pktcnt++;
        s->regs[R_DMA_RX_STATUS] = FIELD_DP32(status, DMA_RX_STATUS,
                                              PKTCNT, pktcnt);
        s->regs[R_DMA_RX_STATUS] |= R_DMA_RX_STATUS_PKT_RCVD_MASK;
        s->rx_desc = d.next;
    } else {
        s->regs[R_DMA_RX_CTL] &= ~R_DMA_RX_CTL_EN_MASK;
        s->regs[R_DMA_RX_STATUS] |= R_DMA_RX_STATUS_OVERFLOW_MASK;
    }
    emac_update_irq(s);
    return size;
}

static void msf2_emac_reset(DeviceState *dev)
{
    MSF2EmacState *s = MSS_EMAC(dev);

    msf2_emac_do_reset(s);
}

static void emac_set_link(NetClientState *nc)
{
    MSF2EmacState *s = qemu_get_nic_opaque(nc);

    msf2_phy_update_link(s);
}

static NetClientInfo net_msf2_emac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = emac_can_rx,
    .receive = emac_rx,
    .link_status_changed = emac_set_link,
};

static void msf2_emac_realize(DeviceState *dev, Error **errp)
{
    MSF2EmacState *s = MSS_EMAC(dev);

    if (!s->dma_mr) {
        error_setg(errp, "MSS_EMAC 'ahb-bus' link not set");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, "emac-ahb");

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_msf2_emac_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void msf2_emac_init(Object *obj)
{
    MSF2EmacState *s = MSS_EMAC(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &emac_ops, s,
                          "msf2-emac", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static Property msf2_emac_properties[] = {
    DEFINE_PROP_LINK("ahb-bus", MSF2EmacState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_NIC_PROPERTIES(MSF2EmacState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_msf2_emac = {
    .name = TYPE_MSS_EMAC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(mac_addr, MSF2EmacState, ETH_ALEN),
        VMSTATE_UINT32(rx_desc, MSF2EmacState),
        VMSTATE_UINT16_ARRAY(phy_regs, MSF2EmacState, PHY_MAX_REGS),
        VMSTATE_UINT32_ARRAY(regs, MSF2EmacState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void msf2_emac_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = msf2_emac_realize;
    dc->reset = msf2_emac_reset;
    dc->vmsd = &vmstate_msf2_emac;
    device_class_set_props(dc, msf2_emac_properties);
}

static const TypeInfo msf2_emac_info = {
    .name          = TYPE_MSS_EMAC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MSF2EmacState),
    .instance_init = msf2_emac_init,
    .class_init    = msf2_emac_class_init,
};

static void msf2_emac_register_types(void)
{
    type_register_static(&msf2_emac_info);
}

type_init(msf2_emac_register_types)
