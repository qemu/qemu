/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell MV88W8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/net/mv88w8618_eth.h"
#include "migration/vmstate.h"
#include "system/dma.h"
#include "net/net.h"

#define MP_ETH_SIZE             0x00001000

/* Ethernet register offsets */
#define MP_ETH_SMIR             0x010
#define MP_ETH_PCXR             0x408
#define MP_ETH_SDCMR            0x448
#define MP_ETH_ICR              0x450
#define MP_ETH_IMR              0x458
#define MP_ETH_FRDP0            0x480
#define MP_ETH_FRDP1            0x484
#define MP_ETH_FRDP2            0x488
#define MP_ETH_FRDP3            0x48C
#define MP_ETH_CRDP0            0x4A0
#define MP_ETH_CRDP1            0x4A4
#define MP_ETH_CRDP2            0x4A8
#define MP_ETH_CRDP3            0x4AC
#define MP_ETH_CTDP0            0x4E0
#define MP_ETH_CTDP1            0x4E4

/* MII PHY access */
#define MP_ETH_SMIR_DATA        0x0000FFFF
#define MP_ETH_SMIR_ADDR        0x03FF0000
#define MP_ETH_SMIR_OPCODE      (1 << 26) /* Read value */
#define MP_ETH_SMIR_RDVALID     (1 << 27)

/* PHY registers */
#define MP_ETH_PHY1_BMSR        0x00210000
#define MP_ETH_PHY1_PHYSID1     0x00410000
#define MP_ETH_PHY1_PHYSID2     0x00610000

#define MP_PHY_BMSR_LINK        0x0004
#define MP_PHY_BMSR_AUTONEG     0x0008

#define MP_PHY_88E3015          0x01410E20

/* TX descriptor status */
#define MP_ETH_TX_OWN           (1U << 31)

/* RX descriptor status */
#define MP_ETH_RX_OWN           (1U << 31)

/* Interrupt cause/mask bits */
#define MP_ETH_IRQ_RX_BIT       0
#define MP_ETH_IRQ_RX           (1 << MP_ETH_IRQ_RX_BIT)
#define MP_ETH_IRQ_TXHI_BIT     2
#define MP_ETH_IRQ_TXLO_BIT     3

/* Port config bits */
#define MP_ETH_PCXR_2BSM_BIT    28 /* 2-byte incoming suffix */

/* SDMA command bits */
#define MP_ETH_CMD_TXHI         (1 << 23)
#define MP_ETH_CMD_TXLO         (1 << 22)

typedef struct mv88w8618_tx_desc {
    uint32_t cmdstat;
    uint16_t res;
    uint16_t bytes;
    uint32_t buffer;
    uint32_t next;
} mv88w8618_tx_desc;

typedef struct mv88w8618_rx_desc {
    uint32_t cmdstat;
    uint16_t bytes;
    uint16_t buffer_size;
    uint32_t buffer;
    uint32_t next;
} mv88w8618_rx_desc;

OBJECT_DECLARE_SIMPLE_TYPE(mv88w8618_eth_state, MV88W8618_ETH)

struct mv88w8618_eth_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq irq;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    uint32_t smir;
    uint32_t icr;
    uint32_t imr;
    int mmio_index;
    uint32_t vlan_header;
    uint32_t tx_queue[2];
    uint32_t rx_queue[4];
    uint32_t frx_queue[4];
    uint32_t cur_rx[4];
    NICState *nic;
    NICConf conf;
};

static void eth_rx_desc_put(AddressSpace *dma_as, uint32_t addr,
                            mv88w8618_rx_desc *desc)
{
    cpu_to_le32s(&desc->cmdstat);
    cpu_to_le16s(&desc->bytes);
    cpu_to_le16s(&desc->buffer_size);
    cpu_to_le32s(&desc->buffer);
    cpu_to_le32s(&desc->next);
    dma_memory_write(dma_as, addr, desc, sizeof(*desc), MEMTXATTRS_UNSPECIFIED);
}

static void eth_rx_desc_get(AddressSpace *dma_as, uint32_t addr,
                            mv88w8618_rx_desc *desc)
{
    dma_memory_read(dma_as, addr, desc, sizeof(*desc), MEMTXATTRS_UNSPECIFIED);
    le32_to_cpus(&desc->cmdstat);
    le16_to_cpus(&desc->bytes);
    le16_to_cpus(&desc->buffer_size);
    le32_to_cpus(&desc->buffer);
    le32_to_cpus(&desc->next);
}

static ssize_t eth_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    mv88w8618_eth_state *s = qemu_get_nic_opaque(nc);
    uint32_t desc_addr;
    mv88w8618_rx_desc desc;
    int i;

    for (i = 0; i < 4; i++) {
        desc_addr = s->cur_rx[i];
        if (!desc_addr) {
            continue;
        }
        do {
            eth_rx_desc_get(&s->dma_as, desc_addr, &desc);
            if ((desc.cmdstat & MP_ETH_RX_OWN) && desc.buffer_size >= size) {
                dma_memory_write(&s->dma_as, desc.buffer + s->vlan_header,
                                 buf, size, MEMTXATTRS_UNSPECIFIED);
                desc.bytes = size + s->vlan_header;
                desc.cmdstat &= ~MP_ETH_RX_OWN;
                s->cur_rx[i] = desc.next;

                s->icr |= MP_ETH_IRQ_RX;
                if (s->icr & s->imr) {
                    qemu_irq_raise(s->irq);
                }
                eth_rx_desc_put(&s->dma_as, desc_addr, &desc);
                return size;
            }
            desc_addr = desc.next;
        } while (desc_addr != s->rx_queue[i]);
    }
    return size;
}

static void eth_tx_desc_put(AddressSpace *dma_as, uint32_t addr,
                            mv88w8618_tx_desc *desc)
{
    cpu_to_le32s(&desc->cmdstat);
    cpu_to_le16s(&desc->res);
    cpu_to_le16s(&desc->bytes);
    cpu_to_le32s(&desc->buffer);
    cpu_to_le32s(&desc->next);
    dma_memory_write(dma_as, addr, desc, sizeof(*desc), MEMTXATTRS_UNSPECIFIED);
}

static void eth_tx_desc_get(AddressSpace *dma_as, uint32_t addr,
                            mv88w8618_tx_desc *desc)
{
    dma_memory_read(dma_as, addr, desc, sizeof(*desc), MEMTXATTRS_UNSPECIFIED);
    le32_to_cpus(&desc->cmdstat);
    le16_to_cpus(&desc->res);
    le16_to_cpus(&desc->bytes);
    le32_to_cpus(&desc->buffer);
    le32_to_cpus(&desc->next);
}

static void eth_send(mv88w8618_eth_state *s, int queue_index)
{
    uint32_t desc_addr = s->tx_queue[queue_index];
    mv88w8618_tx_desc desc;
    uint32_t next_desc;
    uint8_t buf[2048];
    int len;

    do {
        eth_tx_desc_get(&s->dma_as, desc_addr, &desc);
        next_desc = desc.next;
        if (desc.cmdstat & MP_ETH_TX_OWN) {
            len = desc.bytes;
            if (len < 2048) {
                dma_memory_read(&s->dma_as, desc.buffer, buf, len,
                                MEMTXATTRS_UNSPECIFIED);
                qemu_send_packet(qemu_get_queue(s->nic), buf, len);
            }
            desc.cmdstat &= ~MP_ETH_TX_OWN;
            s->icr |= 1 << (MP_ETH_IRQ_TXLO_BIT - queue_index);
            eth_tx_desc_put(&s->dma_as, desc_addr, &desc);
        }
        desc_addr = next_desc;
    } while (desc_addr != s->tx_queue[queue_index]);
}

static uint64_t mv88w8618_eth_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    mv88w8618_eth_state *s = opaque;

    switch (offset) {
    case MP_ETH_SMIR:
        if (s->smir & MP_ETH_SMIR_OPCODE) {
            switch (s->smir & MP_ETH_SMIR_ADDR) {
            case MP_ETH_PHY1_BMSR:
                return MP_PHY_BMSR_LINK | MP_PHY_BMSR_AUTONEG |
                       MP_ETH_SMIR_RDVALID;
            case MP_ETH_PHY1_PHYSID1:
                return (MP_PHY_88E3015 >> 16) | MP_ETH_SMIR_RDVALID;
            case MP_ETH_PHY1_PHYSID2:
                return (MP_PHY_88E3015 & 0xFFFF) | MP_ETH_SMIR_RDVALID;
            default:
                return MP_ETH_SMIR_RDVALID;
            }
        }
        return 0;

    case MP_ETH_ICR:
        return s->icr;

    case MP_ETH_IMR:
        return s->imr;

    case MP_ETH_FRDP0 ... MP_ETH_FRDP3:
        return s->frx_queue[(offset - MP_ETH_FRDP0) / 4];

    case MP_ETH_CRDP0 ... MP_ETH_CRDP3:
        return s->rx_queue[(offset - MP_ETH_CRDP0) / 4];

    case MP_ETH_CTDP0 ... MP_ETH_CTDP1:
        return s->tx_queue[(offset - MP_ETH_CTDP0) / 4];

    default:
        return 0;
    }
}

static void mv88w8618_eth_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    mv88w8618_eth_state *s = opaque;

    switch (offset) {
    case MP_ETH_SMIR:
        s->smir = value;
        break;

    case MP_ETH_PCXR:
        s->vlan_header = ((value >> MP_ETH_PCXR_2BSM_BIT) & 1) * 2;
        break;

    case MP_ETH_SDCMR:
        if (value & MP_ETH_CMD_TXHI) {
            eth_send(s, 1);
        }
        if (value & MP_ETH_CMD_TXLO) {
            eth_send(s, 0);
        }
        if (value & (MP_ETH_CMD_TXHI | MP_ETH_CMD_TXLO) && s->icr & s->imr) {
            qemu_irq_raise(s->irq);
        }
        break;

    case MP_ETH_ICR:
        s->icr &= value;
        break;

    case MP_ETH_IMR:
        s->imr = value;
        if (s->icr & s->imr) {
            qemu_irq_raise(s->irq);
        }
        break;

    case MP_ETH_FRDP0 ... MP_ETH_FRDP3:
        s->frx_queue[(offset - MP_ETH_FRDP0) / 4] = value;
        break;

    case MP_ETH_CRDP0 ... MP_ETH_CRDP3:
        s->rx_queue[(offset - MP_ETH_CRDP0) / 4] =
            s->cur_rx[(offset - MP_ETH_CRDP0) / 4] = value;
        break;

    case MP_ETH_CTDP0 ... MP_ETH_CTDP1:
        s->tx_queue[(offset - MP_ETH_CTDP0) / 4] = value;
        break;
    }
}

static const MemoryRegionOps mv88w8618_eth_ops = {
    .read = mv88w8618_eth_read,
    .write = mv88w8618_eth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void eth_cleanup(NetClientState *nc)
{
    mv88w8618_eth_state *s = qemu_get_nic_opaque(nc);

    s->nic = NULL;
}

static NetClientInfo net_mv88w8618_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = eth_receive,
    .cleanup = eth_cleanup,
};

static void mv88w8618_eth_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    mv88w8618_eth_state *s = MV88W8618_ETH(dev);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, obj, &mv88w8618_eth_ops, s,
                          "mv88w8618-eth", MP_ETH_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void mv88w8618_eth_realize(DeviceState *dev, Error **errp)
{
    mv88w8618_eth_state *s = MV88W8618_ETH(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_MV88W8618_ETH " 'dma-memory' link not set");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, "emac-dma");
    s->nic = qemu_new_nic(&net_mv88w8618_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
}

static const VMStateDescription mv88w8618_eth_vmsd = {
    .name = "mv88w8618_eth",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(smir, mv88w8618_eth_state),
        VMSTATE_UINT32(icr, mv88w8618_eth_state),
        VMSTATE_UINT32(imr, mv88w8618_eth_state),
        VMSTATE_UINT32(vlan_header, mv88w8618_eth_state),
        VMSTATE_UINT32_ARRAY(tx_queue, mv88w8618_eth_state, 2),
        VMSTATE_UINT32_ARRAY(rx_queue, mv88w8618_eth_state, 4),
        VMSTATE_UINT32_ARRAY(frx_queue, mv88w8618_eth_state, 4),
        VMSTATE_UINT32_ARRAY(cur_rx, mv88w8618_eth_state, 4),
        VMSTATE_END_OF_LIST()
    }
};

static const Property mv88w8618_eth_properties[] = {
    DEFINE_NIC_PROPERTIES(mv88w8618_eth_state, conf),
    DEFINE_PROP_LINK("dma-memory", mv88w8618_eth_state, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void mv88w8618_eth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &mv88w8618_eth_vmsd;
    device_class_set_props(dc, mv88w8618_eth_properties);
    dc->realize = mv88w8618_eth_realize;
}

static const TypeInfo mv88w8618_eth_info = {
    .name          = TYPE_MV88W8618_ETH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mv88w8618_eth_state),
    .instance_init = mv88w8618_eth_init,
    .class_init    = mv88w8618_eth_class_init,
};

static void musicpal_register_types(void)
{
    type_register_static(&mv88w8618_eth_info);
}

type_init(musicpal_register_types)

