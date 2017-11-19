/*
 * CSKY Ethernet interface emulation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "hw/sysbus.h"
#include "net/net.h"
#include <zlib.h>
#include "qemu/log.h"
#include "cpu.h"
#include "hw/csky/cskydev.h"

#define NUM_BD 128

#define TYPE_CSKY_MAC "csky_mac"
#define CSKY_MAC(obj) OBJECT_CHECK(csky_mac_state, (obj), TYPE_CSKY_MAC)

/* buffer descriptor*/
typedef struct {
    uint32_t status;
    uint32_t buffer_addr;
} csky_mac_bd;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    uint32_t moder;
    uint32_t int_source;
    uint32_t int_mask;
    uint32_t ipgt;
    uint32_t ipgr1;
    uint32_t ipgr2;
    uint32_t packetlen;
    uint32_t collconf;
    uint32_t tx_bd_num;
    uint32_t ctrlmoder;
    uint32_t mii_moder;
    uint32_t mii_command;
    uint32_t mii_address;
    uint32_t mii_tx_data;
    uint32_t mii_rx_data;
    uint32_t mii_status;
    uint32_t eth_hash0_adr;
    uint32_t eth_hash1_adr;
    uint32_t eth_tx_ctrl;
    csky_mac_bd bd_buffer[NUM_BD];
    int32_t next_rx;
} csky_mac_state;

static const VMStateDescription vmstate_csky_mac_bd = {
    .name = "csky_mac_bd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(status, csky_mac_bd),
        VMSTATE_UINT32(status, csky_mac_bd),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_csky_mac = {
    .name = "csky_mac",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(moder, csky_mac_state),
        VMSTATE_UINT32(int_source, csky_mac_state),
        VMSTATE_UINT32(int_mask, csky_mac_state),
        VMSTATE_UINT32(ipgt, csky_mac_state),
        VMSTATE_UINT32(ipgr1, csky_mac_state),
        VMSTATE_UINT32(ipgr2, csky_mac_state),
        VMSTATE_UINT32(packetlen, csky_mac_state),
        VMSTATE_UINT32(collconf, csky_mac_state),
        VMSTATE_UINT32(tx_bd_num, csky_mac_state),
        VMSTATE_UINT32(ctrlmoder, csky_mac_state),
        VMSTATE_UINT32(mii_moder, csky_mac_state),
        VMSTATE_UINT32(mii_command, csky_mac_state),
        VMSTATE_UINT32(mii_address, csky_mac_state),
        VMSTATE_UINT32(mii_tx_data, csky_mac_state),
        VMSTATE_UINT32(mii_rx_data, csky_mac_state),
        VMSTATE_UINT32(mii_status, csky_mac_state),
        VMSTATE_UINT32(eth_hash0_adr, csky_mac_state),
        VMSTATE_UINT32(eth_hash1_adr, csky_mac_state),
        VMSTATE_UINT32(eth_tx_ctrl, csky_mac_state),
        VMSTATE_STRUCT_ARRAY(bd_buffer, csky_mac_state, NUM_BD, 1,
                             vmstate_csky_mac_bd, csky_mac_bd),
        VMSTATE_INT32(next_rx, csky_mac_state),
        VMSTATE_END_OF_LIST()
    }
};

/* moder register */
#define MODER_RESMALL   0x10000
#define MODER_PAD       0x8000
#define MODER_HUGEN     0X4000
#define MODER_CRCEN     0X2000
#define MODER_DLYCRCEN  0x1000
#define MODER_LOOPBACK  0x80
#define MODER_PRO       0x10
#define MODER_TXEN      0X2
#define MODER_RXEN      0X1

#define INT_SOURCE_BER  0X80
#define INT_SOURCE_RXC  0X40
#define INT_SOURCE_TXC  0X20
#define INT_SOURCE_BUSY 0X10
#define INT_SOURCE_RXE  0x8
#define INT_SOURCE_RXB  0x4
#define INT_SOURCE_TXE  0X2
#define INT_SOURCE_TXB  0X1

#define CTRL_MODER_TXFLOW 0X4
#define CTRL_MODER_RXFLOW 0X2
#define CTRL_MODER_PASSALL 0X1

/* receive buffer descriptor */
#define RXBD_EMPTY   (1 << 15)
#define RXBD_IRQ     (1 << 14)
#define RXBD_WR      (1 << 13)
#define RXBD_CF      (1 << 8)
#define RXBD_MISS    (1 << 7)
#define RXBD_DN      (1 << 4)
#define RXBD_TL      (1 << 3)
#define RXBD_SF      (1 << 2)
#define RXBD_CRC     (1 << 1)

/* transmit buffer descriptor */
#define TXBD_RD      (1 << 15)
#define TXBD_IRQ     (1 << 14)
#define TXBD_WR      (1 << 13)
#define TXBD_PAD     (1 << 12)
#define TXBD_CRC     (1 << 11)

static ssize_t csky_mac_receive(NetClientState *nc, const uint8_t *buf,
                                size_t size);

static void csky_mac_update(csky_mac_state *s)
{
    int level;

    level = ((s->int_source & s->int_mask) != 0);
    qemu_set_irq(s->irq, level);
}

static uint64_t csky_mac_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_mac_state *s = (csky_mac_state *)opaque;
    uint64_t ret;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mac_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x0:
        ret = s->moder;
        break;
    case 0x4:
        ret = s->int_source;
        break;
    case 0x8:
        ret = s->int_mask;
        break;
    case 0xc:
        ret = s->ipgt;
        break;
    case 0x10:
        ret = s->ipgr1;
        break;
    case 0x14:
        ret = s->ipgr2;
        break;
    case 0x18:
        ret = s->packetlen;
        break;
    case 0x1c:
        ret = s->collconf;
        break;
    case 0x20:
        ret = s->tx_bd_num;
        break;
    case 0x24:
        ret = s->ctrlmoder;
        break;
    case 0x28:
        ret = s->mii_moder;
        break;
    case 0x2c:
        ret = s->mii_command;
        break;
    case 0x30:
        ret = s->mii_address;
        break;
    case 0x34:
        ret = s->mii_tx_data;
        break;
    case 0x38:
        ret = s->mii_tx_data;
        break;
    case 0x3c:
        ret = s->mii_status;
        break;
    case 0x40:
        ret = (s->conf.macaddr.a[2] << 24) | (s->conf.macaddr.a[3] << 16)
            | (s->conf.macaddr.a[4] << 8) | s->conf.macaddr.a[5];
        break;
    case 0x44:
        ret = (s->conf.macaddr.a[0] << 8) | (s->conf.macaddr.a[1]);
        break;
    case 0x48:
        ret = s->eth_hash0_adr;
        break;
    case 0x4c:
        ret = s->eth_hash1_adr;
        break;
    case 0x50:
        ret = s->eth_tx_ctrl;
        break;
    default:
        if ((offset < 0x1400) || (offset > 0x1800)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_mac_read: Bad offset %x\n", (int)offset);
            ret = 0;
        } else {
            /* read bd */
            int index = (offset - 0x1400) / 0x8;
            int addend = (offset - 0x1400) % 0x8;
            if (addend / 0x4 == 0) {
                ret = s->bd_buffer[index].status;
            } else {
                ret = s->bd_buffer[index].buffer_addr;
            }
        }
        break;
    }

    return ret;
}

static inline void csky_mac_release_packet(csky_mac_state *s, int index)
{
    int status;
    int size;
    uint8_t *p;
    uint8_t frame[2032];
    NetClientState *nc = qemu_get_queue(s->nic);

    p = frame;

    status = s->bd_buffer[index].status;
    size = status >> 16;

    if (size < 4) { /* do not send this packet */
        s->bd_buffer[index].status &= ~TXBD_RD;
        return;
    }

    cpu_physical_memory_read(s->bd_buffer[index].buffer_addr, p, size);
    p += size;

    if (status & TXBD_PAD) {
        /* Add pad */
        int MinFL = 60;
        if (size < MinFL) {
            memset(p, 0, MinFL - size);
            size  = MinFL;
        }
    }

    if (s->moder & MODER_LOOPBACK) {
        s->bd_buffer[index].status &= ~TXBD_RD;

        if (status & TXBD_IRQ) {
            s->int_source |= INT_SOURCE_TXB;
            csky_mac_update(s);
        }

        csky_mac_receive(nc, frame, size);
        return;
    }

    qemu_send_packet(nc, frame, size);

    s->bd_buffer[index].status &= ~TXBD_RD;

    if (status & TXBD_IRQ) {
        s->int_source |= INT_SOURCE_TXB;
        csky_mac_update(s);
    }
}

static void csky_mac_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    csky_mac_state *s = (csky_mac_state *)opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mac_write: 0x%x must word align write\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x0:
        if (!(s->moder & MODER_RXEN) && (value & MODER_RXEN)) {
            s->next_rx = s->tx_bd_num;
        }
        s->moder = value;
        return;
    case 0x4:
        s->int_source &= ~value;
        csky_mac_update(s);
        return;
    case 0x8:
        s->int_mask = value;
        return;
    case 0xc:
        s->ipgt = value;
        return;
    case 0x10:
        s->ipgr1 = value;
        return;
    case 0x14:
        s->ipgr2 = value;
        return;
    case 0x18:
        s->packetlen = value;
        return;
    case 0x1c:
        s->collconf = value;
        return;
    case 0x20:
        s->tx_bd_num = value;
        return;
    case 0x24:
        s->ctrlmoder = value;
        return;
    case 0x28:
        s->mii_moder = value;
        return;
    case 0x2c:
        s->mii_command = value;
        return;
    case 0x30:
        s->mii_address = value;
        return;
    case 0x34:
        s->mii_tx_data = value;
        return;
    case 0x38: /* mii_rx_data register read only */
        return;
    case 0x3c: /* mii_status register read only */
        return;
    case 0x40:
        s->conf.macaddr.a[2] = value >> 24;
        s->conf.macaddr.a[3] = value >> 16;
        s->conf.macaddr.a[4] = value >> 8;
        s->conf.macaddr.a[5] = value;
        return;
    case 0x44:
        s->conf.macaddr.a[0] = value >> 8;
        s->conf.macaddr.a[1] = value;
        return;
    case 0x48:
        s->eth_hash0_adr = value;
        return;
    case 0x4c:
        s->eth_hash1_adr = value;
        return;
    case 0x50:
        s->eth_tx_ctrl = value;
        return;
    default:
        if ((offset < 0x1400) || (offset > 0x1800)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_mac_write: Bad offset %x\n", (int)offset);
            return;
        } else {
            /* write bd */
            int index, addend;

            index = (offset - 0x1400) / 0x8;
            addend = (offset - 0x1400) % 0x8;

            if (addend / 0x4) {
                s->bd_buffer[index].buffer_addr = value;
            } else {
                s->bd_buffer[index].status = value;
                if ((index < s->tx_bd_num) && (value & TXBD_RD)) {
                    /* send a packet */
                    csky_mac_release_packet(s, index);
                }
            }
            return;
        }
    }
}

static const MemoryRegionOps csky_mac_ops = {
    .read = csky_mac_read,
    .write = csky_mac_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
static int csky_mac_can_receive(NetClientState *nc)
{
    csky_mac_state *s = qemu_get_nic_opaque(nc);

    if ((s->moder & MODER_RXEN) &&
       (s->tx_bd_num != 128) &&
       (s->bd_buffer[s->next_rx].status & RXBD_EMPTY)) {
        return 1;
    }
    return 0;
}
*/

static ssize_t csky_mac_receive(NetClientState *nc, const uint8_t *buf,
                                size_t size)
{
    csky_mac_state *s = qemu_get_nic_opaque(nc);

    if (!((s->moder & MODER_RXEN) &&
       (s->tx_bd_num != 128) &&
       (s->bd_buffer[s->next_rx].status & RXBD_EMPTY))) {
        return -1;
    }

    s->bd_buffer[s->next_rx].status &= ~RXBD_EMPTY;

    cpu_physical_memory_write(s->bd_buffer[s->next_rx].buffer_addr, buf, size);

    s->bd_buffer[s->next_rx].status |= ((size + 4) << 16);

    if (s->bd_buffer[s->next_rx].status & RXBD_IRQ) {
        s->int_source |= INT_SOURCE_RXB;
        csky_mac_update(s);
    }

    if (s->bd_buffer[s->next_rx].status & RXBD_WR) {
        s->next_rx = s->tx_bd_num;
    } else {
        s->next_rx++;
    }

    return size;
}

static void csky_mac_cleanup(NetClientState *nc)
{
    csky_mac_state *s = qemu_get_nic_opaque(nc);

    s->nic = NULL;
}

static NetClientInfo net_csky_mac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
/*    .can_receive = csky_mac_can_receive,*/
    .receive = csky_mac_receive,
    .cleanup = csky_mac_cleanup,
};

static inline void csky_mac_reset(csky_mac_state *s)
{
    s->moder = 0xa000;
    s->ipgt = 0x12;
    s->ipgr1 = 0xc;
    s->ipgr2 = 0x12;
    s->packetlen = 0x400600;
    s->collconf = 0xf003f;
    s->tx_bd_num = 0x40;
    s->mii_moder = 0x64;
    s->next_rx = s->tx_bd_num;
}

static int csky_mac_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    csky_mac_state *s = CSKY_MAC(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &csky_mac_ops, s,
                          TYPE_CSKY_MAC, 0x2000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_csky_mac_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    csky_mac_reset(s);
    return 0;
}

static Property csky_mac_properties[] = {
    DEFINE_NIC_PROPERTIES(csky_mac_state, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void csky_mac_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = csky_mac_init;
    dc->props = csky_mac_properties;
    dc->vmsd = &vmstate_csky_mac;
}

static const TypeInfo csky_mac_info = {
    .name          = TYPE_CSKY_MAC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_mac_state),
    .class_init    = csky_mac_class_init,
};

static void csky_mac_register_types(void)
{
    type_register_static(&csky_mac_info);
}


void csky_mac_create(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(nd, "csky_mac");
    dev = qdev_create(NULL, "csky_mac");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

type_init(csky_mac_register_types)
