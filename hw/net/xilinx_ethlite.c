/*
 * QEMU model of the Xilinx Ethernet Lite MAC.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
 * Copyright (c) 2024 Linaro, Ltd
 *
 * DS580: https://docs.amd.com/v/u/en-US/xps_ethernetlite
 * LogiCORE IP XPS Ethernet Lite Media Access Controller
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
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/misc/unimp.h"
#include "net/net.h"
#include "trace.h"

#define BUFSZ_MAX      0x07e4
#define A_MDIO_BASE    0x07e4
#define A_TX_BASE0     0x07f4
#define A_TX_BASE1     0x0ff4
#define A_RX_BASE0     0x17fc
#define A_RX_BASE1     0x1ffc

enum {
    TX_LEN =  0,
    TX_GIE =  1,
    TX_CTRL = 2,
    TX_MAX
};

enum {
    RX_CTRL = 0,
    RX_MAX
};

#define GIE_GIE    0x80000000

#define CTRL_I     0x8
#define CTRL_P     0x2
#define CTRL_S     0x1

typedef struct XlnxXpsEthLitePort {
    MemoryRegion txio;
    MemoryRegion rxio;
    MemoryRegion txbuf;
    MemoryRegion rxbuf;

    struct {
        uint32_t tx_len;
        uint32_t tx_gie;
        uint32_t tx_ctrl;

        uint32_t rx_ctrl;
    } reg;
} XlnxXpsEthLitePort;

#define TYPE_XILINX_ETHLITE "xlnx.xps-ethernetlite"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxXpsEthLite, XILINX_ETHLITE)

struct XlnxXpsEthLite
{
    SysBusDevice parent_obj;

    EndianMode model_endianness;
    MemoryRegion container;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;

    uint32_t c_tx_pingpong;
    uint32_t c_rx_pingpong;
    unsigned int port_index; /* dual port RAM index */

    UnimplementedDeviceState rsvd;
    UnimplementedDeviceState mdio;
    XlnxXpsEthLitePort port[2];
};

static inline void eth_pulse_irq(XlnxXpsEthLite *s)
{
    /* Only the first gie reg is active.  */
    if (s->port[0].reg.tx_gie & GIE_GIE) {
        qemu_irq_pulse(s->irq);
    }
}

static unsigned addr_to_port_index(hwaddr addr)
{
    return extract64(addr, 11, 1);
}

static void *txbuf_ptr(XlnxXpsEthLite *s, unsigned port_index)
{
    return memory_region_get_ram_ptr(&s->port[port_index].txbuf);
}

static void *rxbuf_ptr(XlnxXpsEthLite *s, unsigned port_index)
{
    return memory_region_get_ram_ptr(&s->port[port_index].rxbuf);
}

static uint64_t port_tx_read(void *opaque, hwaddr addr, unsigned int size)
{
    XlnxXpsEthLite *s = opaque;
    unsigned port_index = addr_to_port_index(addr);
    uint32_t r = 0;

    switch (addr >> 2) {
    case TX_LEN:
        r = s->port[port_index].reg.tx_len;
        break;
    case TX_GIE:
        r = s->port[port_index].reg.tx_gie;
        break;
    case TX_CTRL:
        r = s->port[port_index].reg.tx_ctrl;
        break;
    default:
        g_assert_not_reached();
    }

    return r;
}

static void port_tx_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned int size)
{
    XlnxXpsEthLite *s = opaque;
    unsigned port_index = addr_to_port_index(addr);

    switch (addr >> 2) {
    case TX_LEN:
        s->port[port_index].reg.tx_len = value;
        break;
    case TX_GIE:
        s->port[port_index].reg.tx_gie = value;
        break;
    case TX_CTRL:
        if ((value & (CTRL_P | CTRL_S)) == CTRL_S) {
            qemu_send_packet(qemu_get_queue(s->nic),
                             txbuf_ptr(s, port_index),
                             s->port[port_index].reg.tx_len);
            if (s->port[port_index].reg.tx_ctrl & CTRL_I) {
                eth_pulse_irq(s);
            }
        } else if ((value & (CTRL_P | CTRL_S)) == (CTRL_P | CTRL_S)) {
            memcpy(&s->conf.macaddr.a[0], txbuf_ptr(s, port_index), 6);
            if (s->port[port_index].reg.tx_ctrl & CTRL_I) {
                eth_pulse_irq(s);
            }
        }
        /*
         * We are fast and get ready pretty much immediately
         * so we actually never flip the S nor P bits to one.
         */
        s->port[port_index].reg.tx_ctrl = value & ~(CTRL_P | CTRL_S);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps eth_porttx_ops[2] = {
    [0 ... 1] = {
        .read = port_tx_read,
        .write = port_tx_write,
        .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
        .valid = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
    [0].endianness = DEVICE_LITTLE_ENDIAN,
    [1].endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t port_rx_read(void *opaque, hwaddr addr, unsigned int size)
{
    XlnxXpsEthLite *s = opaque;
    unsigned port_index = addr_to_port_index(addr);
    uint32_t r = 0;

    switch (addr >> 2) {
    case RX_CTRL:
        r = s->port[port_index].reg.rx_ctrl;
        break;
    default:
        g_assert_not_reached();
    }

    return r;
}

static void port_rx_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned int size)
{
    XlnxXpsEthLite *s = opaque;
    unsigned port_index = addr_to_port_index(addr);

    switch (addr >> 2) {
    case RX_CTRL:
        if (!(value & CTRL_S)) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        s->port[port_index].reg.rx_ctrl = value;
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps eth_portrx_ops[2] = {
    [0 ... 1] = {
        .read = port_rx_read,
        .write = port_rx_write,
        .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
        .valid = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
    [0].endianness = DEVICE_LITTLE_ENDIAN,
    [1].endianness = DEVICE_BIG_ENDIAN,
};

static bool eth_can_rx(NetClientState *nc)
{
    XlnxXpsEthLite *s = qemu_get_nic_opaque(nc);

    return !(s->port[s->port_index].reg.rx_ctrl & CTRL_S);
}

static ssize_t eth_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    XlnxXpsEthLite *s = qemu_get_nic_opaque(nc);
    unsigned int port_index = s->port_index;

    /* DA filter.  */
    if (!(buf[0] & 0x80) && memcmp(&s->conf.macaddr.a[0], buf, 6))
        return size;

    if (s->port[port_index].reg.rx_ctrl & CTRL_S) {
        trace_ethlite_pkt_lost(s->port[port_index].reg.rx_ctrl);
        return -1;
    }

    if (size >= BUFSZ_MAX) {
        trace_ethlite_pkt_size_too_big(size);
        return -1;
    }
    memcpy(rxbuf_ptr(s, port_index), buf, size);

    s->port[port_index].reg.rx_ctrl |= CTRL_S;
    if (s->port[port_index].reg.rx_ctrl & CTRL_I) {
        eth_pulse_irq(s);
    }

    /* If c_rx_pingpong was set flip buffers.  */
    s->port_index ^= s->c_rx_pingpong;
    return size;
}

static void xilinx_ethlite_reset(DeviceState *dev)
{
    XlnxXpsEthLite *s = XILINX_ETHLITE(dev);

    s->port_index = 0;
}

static NetClientInfo net_xilinx_ethlite_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = eth_can_rx,
    .receive = eth_rx,
};

static void xilinx_ethlite_realize(DeviceState *dev, Error **errp)
{
    XlnxXpsEthLite *s = XILINX_ETHLITE(dev);
    unsigned ops_index;

    if (s->model_endianness == ENDIAN_MODE_UNSPECIFIED) {
        error_setg(errp, TYPE_XILINX_ETHLITE " property 'endianness'"
                         " must be set to 'big' or 'little'");
        return;
    }
    ops_index = s->model_endianness == ENDIAN_MODE_BIG ? 1 : 0;

    memory_region_init(&s->container, OBJECT(dev),
                       "xlnx.xps-ethernetlite", 0x2000);

    object_initialize_child(OBJECT(dev), "ethlite.reserved", &s->rsvd,
                            TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(&s->rsvd), "name", "ethlite.reserved");
    qdev_prop_set_uint64(DEVICE(&s->rsvd), "size",
                         memory_region_size(&s->container));
    sysbus_realize(SYS_BUS_DEVICE(&s->rsvd), &error_fatal);
    memory_region_add_subregion_overlap(&s->container, 0,
                           sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rsvd), 0),
                           -1);

    object_initialize_child(OBJECT(dev), "ethlite.mdio", &s->mdio,
                            TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(&s->mdio), "name", "ethlite.mdio");
    qdev_prop_set_uint64(DEVICE(&s->mdio), "size", 4 * 4);
    sysbus_realize(SYS_BUS_DEVICE(&s->mdio), &error_fatal);
    memory_region_add_subregion(&s->container, A_MDIO_BASE,
                           sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mdio), 0));

    for (unsigned i = 0; i < 2; i++) {
        memory_region_init_ram(&s->port[i].txbuf, OBJECT(dev),
                               i ? "ethlite.tx[1]buf" : "ethlite.tx[0]buf",
                               BUFSZ_MAX, &error_abort);
        memory_region_add_subregion(&s->container, 0x0800 * i, &s->port[i].txbuf);
        memory_region_init_io(&s->port[i].txio, OBJECT(dev),
                              &eth_porttx_ops[ops_index], s,
                              i ? "ethlite.tx[1]io" : "ethlite.tx[0]io",
                              4 * TX_MAX);
        memory_region_add_subregion(&s->container, i ? A_TX_BASE1 : A_TX_BASE0,
                                    &s->port[i].txio);

        memory_region_init_ram(&s->port[i].rxbuf, OBJECT(dev),
                               i ? "ethlite.rx[1]buf" : "ethlite.rx[0]buf",
                               BUFSZ_MAX, &error_abort);
        memory_region_add_subregion(&s->container, 0x1000 + 0x0800 * i,
                                    &s->port[i].rxbuf);
        memory_region_init_io(&s->port[i].rxio, OBJECT(dev),
                              &eth_portrx_ops[ops_index], s,
                              i ? "ethlite.rx[1]io" : "ethlite.rx[0]io",
                              4 * RX_MAX);
        memory_region_add_subregion(&s->container, i ? A_RX_BASE1 : A_RX_BASE0,
                                    &s->port[i].rxio);
    }

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_xilinx_ethlite_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void xilinx_ethlite_init(Object *obj)
{
    XlnxXpsEthLite *s = XILINX_ETHLITE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->container);
}

static const Property xilinx_ethlite_properties[] = {
    DEFINE_PROP_ENDIAN_NODEFAULT("endianness", XlnxXpsEthLite, model_endianness),
    DEFINE_PROP_UINT32("tx-ping-pong", XlnxXpsEthLite, c_tx_pingpong, 1),
    DEFINE_PROP_UINT32("rx-ping-pong", XlnxXpsEthLite, c_rx_pingpong, 1),
    DEFINE_NIC_PROPERTIES(XlnxXpsEthLite, conf),
};

static void xilinx_ethlite_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_ethlite_realize;
    device_class_set_legacy_reset(dc, xilinx_ethlite_reset);
    device_class_set_props(dc, xilinx_ethlite_properties);
}

static const TypeInfo xilinx_ethlite_types[] = {
    {
        .name          = TYPE_XILINX_ETHLITE,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(XlnxXpsEthLite),
        .instance_init = xilinx_ethlite_init,
        .class_init    = xilinx_ethlite_class_init,
    },
};

DEFINE_TYPES(xilinx_ethlite_types)
