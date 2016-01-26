/*
 * QEMU model of the Xilinx Ethernet Lite MAC.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
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
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "net/net.h"

#define D(x)
#define R_TX_BUF0     0
#define R_TX_LEN0     (0x07f4 / 4)
#define R_TX_GIE0     (0x07f8 / 4)
#define R_TX_CTRL0    (0x07fc / 4)
#define R_TX_BUF1     (0x0800 / 4)
#define R_TX_LEN1     (0x0ff4 / 4)
#define R_TX_CTRL1    (0x0ffc / 4)

#define R_RX_BUF0     (0x1000 / 4)
#define R_RX_CTRL0    (0x17fc / 4)
#define R_RX_BUF1     (0x1800 / 4)
#define R_RX_CTRL1    (0x1ffc / 4)
#define R_MAX         (0x2000 / 4)

#define GIE_GIE    0x80000000

#define CTRL_I     0x8
#define CTRL_P     0x2
#define CTRL_S     0x1

#define TYPE_XILINX_ETHLITE "xlnx.xps-ethernetlite"
#define XILINX_ETHLITE(obj) \
    OBJECT_CHECK(struct xlx_ethlite, (obj), TYPE_XILINX_ETHLITE)

struct xlx_ethlite
{
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;

    uint32_t c_tx_pingpong;
    uint32_t c_rx_pingpong;
    unsigned int txbuf;
    unsigned int rxbuf;

    uint32_t regs[R_MAX];
};

static inline void eth_pulse_irq(struct xlx_ethlite *s)
{
    /* Only the first gie reg is active.  */
    if (s->regs[R_TX_GIE0] & GIE_GIE) {
        qemu_irq_pulse(s->irq);
    }
}

static uint64_t
eth_read(void *opaque, hwaddr addr, unsigned int size)
{
    struct xlx_ethlite *s = opaque;
    uint32_t r = 0;

    addr >>= 2;

    switch (addr)
    {
        case R_TX_GIE0:
        case R_TX_LEN0:
        case R_TX_LEN1:
        case R_TX_CTRL1:
        case R_TX_CTRL0:
        case R_RX_CTRL1:
        case R_RX_CTRL0:
            r = s->regs[addr];
            D(qemu_log("%s " TARGET_FMT_plx "=%x\n", __func__, addr * 4, r));
            break;

        default:
            r = tswap32(s->regs[addr]);
            break;
    }
    return r;
}

static void
eth_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    struct xlx_ethlite *s = opaque;
    unsigned int base = 0;
    uint32_t value = val64;

    addr >>= 2;
    switch (addr) 
    {
        case R_TX_CTRL0:
        case R_TX_CTRL1:
            if (addr == R_TX_CTRL1)
                base = 0x800 / 4;

            D(qemu_log("%s addr=" TARGET_FMT_plx " val=%x\n",
                       __func__, addr * 4, value));
            if ((value & (CTRL_P | CTRL_S)) == CTRL_S) {
                qemu_send_packet(qemu_get_queue(s->nic),
                                 (void *) &s->regs[base],
                                 s->regs[base + R_TX_LEN0]);
                D(qemu_log("eth_tx %d\n", s->regs[base + R_TX_LEN0]));
                if (s->regs[base + R_TX_CTRL0] & CTRL_I)
                    eth_pulse_irq(s);
            } else if ((value & (CTRL_P | CTRL_S)) == (CTRL_P | CTRL_S)) {
                memcpy(&s->conf.macaddr.a[0], &s->regs[base], 6);
                if (s->regs[base + R_TX_CTRL0] & CTRL_I)
                    eth_pulse_irq(s);
            }

            /* We are fast and get ready pretty much immediately so
               we actually never flip the S nor P bits to one.  */
            s->regs[addr] = value & ~(CTRL_P | CTRL_S);
            break;

        /* Keep these native.  */
        case R_RX_CTRL0:
        case R_RX_CTRL1:
            if (!(value & CTRL_S)) {
                qemu_flush_queued_packets(qemu_get_queue(s->nic));
            }
            /* fall through */
        case R_TX_LEN0:
        case R_TX_LEN1:
        case R_TX_GIE0:
            D(qemu_log("%s addr=" TARGET_FMT_plx " val=%x\n",
                       __func__, addr * 4, value));
            s->regs[addr] = value;
            break;

        default:
            s->regs[addr] = tswap32(value);
            break;
    }
}

static const MemoryRegionOps eth_ops = {
    .read = eth_read,
    .write = eth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int eth_can_rx(NetClientState *nc)
{
    struct xlx_ethlite *s = qemu_get_nic_opaque(nc);
    unsigned int rxbase = s->rxbuf * (0x800 / 4);

    return !(s->regs[rxbase + R_RX_CTRL0] & CTRL_S);
}

static ssize_t eth_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    struct xlx_ethlite *s = qemu_get_nic_opaque(nc);
    unsigned int rxbase = s->rxbuf * (0x800 / 4);

    /* DA filter.  */
    if (!(buf[0] & 0x80) && memcmp(&s->conf.macaddr.a[0], buf, 6))
        return size;

    if (s->regs[rxbase + R_RX_CTRL0] & CTRL_S) {
        D(qemu_log("ethlite lost packet %x\n", s->regs[R_RX_CTRL0]));
        return -1;
    }

    D(qemu_log("%s %zd rxbase=%x\n", __func__, size, rxbase));
    memcpy(&s->regs[rxbase + R_RX_BUF0], buf, size);

    s->regs[rxbase + R_RX_CTRL0] |= CTRL_S;
    if (s->regs[R_RX_CTRL0] & CTRL_I) {
        eth_pulse_irq(s);
    }

    /* If c_rx_pingpong was set flip buffers.  */
    s->rxbuf ^= s->c_rx_pingpong;
    return size;
}

static void xilinx_ethlite_reset(DeviceState *dev)
{
    struct xlx_ethlite *s = XILINX_ETHLITE(dev);

    s->rxbuf = 0;
}

static NetClientInfo net_xilinx_ethlite_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = eth_can_rx,
    .receive = eth_rx,
};

static void xilinx_ethlite_realize(DeviceState *dev, Error **errp)
{
    struct xlx_ethlite *s = XILINX_ETHLITE(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_xilinx_ethlite_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void xilinx_ethlite_init(Object *obj)
{
    struct xlx_ethlite *s = XILINX_ETHLITE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &eth_ops, s,
                          "xlnx.xps-ethernetlite", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static Property xilinx_ethlite_properties[] = {
    DEFINE_PROP_UINT32("tx-ping-pong", struct xlx_ethlite, c_tx_pingpong, 1),
    DEFINE_PROP_UINT32("rx-ping-pong", struct xlx_ethlite, c_rx_pingpong, 1),
    DEFINE_NIC_PROPERTIES(struct xlx_ethlite, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_ethlite_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_ethlite_realize;
    dc->reset = xilinx_ethlite_reset;
    dc->props = xilinx_ethlite_properties;
}

static const TypeInfo xilinx_ethlite_info = {
    .name          = TYPE_XILINX_ETHLITE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct xlx_ethlite),
    .instance_init = xilinx_ethlite_init,
    .class_init    = xilinx_ethlite_class_init,
};

static void xilinx_ethlite_register_types(void)
{
    type_register_static(&xilinx_ethlite_info);
}

type_init(xilinx_ethlite_register_types)
