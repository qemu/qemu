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

#include "sysbus.h"
#include "hw.h"
#include "net.h"

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

struct xlx_ethlite
{
    SysBusDevice busdev;
    qemu_irq irq;
    VLANClientState *vc;

    unsigned int c_tx_pingpong;
    unsigned int c_rx_pingpong;
    unsigned int txbuf;
    unsigned int rxbuf;

    uint8_t macaddr[6];
    uint32_t regs[R_MAX];
};

static inline void eth_pulse_irq(struct xlx_ethlite *s)
{
    /* Only the first gie reg is active.  */
    if (s->regs[R_TX_GIE0] & GIE_GIE) {
        qemu_irq_pulse(s->irq);
    }
}

static uint32_t eth_readl (void *opaque, target_phys_addr_t addr)
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
            D(qemu_log("%s %x=%x\n", __func__, addr * 4, r));
            break;

        /* Rx packet data is endian fixed at the way into the rx rams. This
         * speeds things up because the ethlite MAC does not have a len
         * register. That means the CPU will issue MMIO reads for the entire
         * 2k rx buffer even for small packets.
         */
        default:
            r = s->regs[addr];
            break;
    }
    return r;
}

static void
eth_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct xlx_ethlite *s = opaque;
    unsigned int base = 0;

    addr >>= 2;
    switch (addr) 
    {
        case R_TX_CTRL0:
        case R_TX_CTRL1:
            if (addr == R_TX_CTRL1)
                base = 0x800 / 4;

            D(qemu_log("%s addr=%x val=%x\n", __func__, addr * 4, value));
            if ((value & (CTRL_P | CTRL_S)) == CTRL_S) {
                qemu_send_packet(s->vc,
                                 (void *) &s->regs[base],
                                 s->regs[base + R_TX_LEN0]);
                D(qemu_log("eth_tx %d\n", s->regs[base + R_TX_LEN0]));
                if (s->regs[base + R_TX_CTRL0] & CTRL_I)
                    eth_pulse_irq(s);
            } else if ((value & (CTRL_P | CTRL_S)) == (CTRL_P | CTRL_S)) {
                memcpy(&s->macaddr[0], &s->regs[base], 6);
                if (s->regs[base + R_TX_CTRL0] & CTRL_I)
                    eth_pulse_irq(s);
            }

            /* We are fast and get ready pretty much immediately so
               we actually never flip the S nor P bits to one.  */
            s->regs[addr] = value & ~(CTRL_P | CTRL_S);
            break;

        /* Keep these native.  */
        case R_TX_LEN0:
        case R_TX_LEN1:
        case R_TX_GIE0:
        case R_RX_CTRL0:
        case R_RX_CTRL1:
            D(qemu_log("%s addr=%x val=%x\n", __func__, addr * 4, value));
            s->regs[addr] = value;
            break;

        /* Packet data, make sure it stays BE.  */
        default:
            s->regs[addr] = cpu_to_be32(value);
            break;
    }
}

static CPUReadMemoryFunc *eth_read[] = {
    NULL, NULL, &eth_readl,
};

static CPUWriteMemoryFunc *eth_write[] = {
    NULL, NULL, &eth_writel,
};

static int eth_can_rx(void *opaque)
{
    struct xlx_ethlite *s = opaque;
    int r;
    r = !(s->regs[R_RX_CTRL0] & CTRL_S);
    qemu_log("%s %d\n", __func__, r);
    return r;
}

static void eth_rx(void *opaque, const uint8_t *buf, int size)
{
    struct xlx_ethlite *s = opaque;
    unsigned int rxbase = s->rxbuf * (0x800 / 4);
    int i;

    /* DA filter.  */
    if (!(buf[0] & 0x80) && memcmp(&s->macaddr[0], buf, 6))
        return;

    if (s->regs[rxbase + R_RX_CTRL0] & CTRL_S) {
        D(qemu_log("ethlite lost packet %x\n", s->regs[R_RX_CTRL0]));
        return;
    }

    D(qemu_log("%s %d rxbase=%x\n", __func__, size, rxbase));
    memcpy(&s->regs[rxbase + R_RX_BUF0], buf, size);

    /* Bring it into host endianess.  */
    for (i = 0; i < ((size + 3) / 4); i++) {
       uint32_t d = s->regs[rxbase + R_RX_BUF0 + i];
       s->regs[rxbase + R_RX_BUF0 + i] = be32_to_cpu(d);
    }

    s->regs[rxbase + R_RX_CTRL0] |= CTRL_S;
    if (s->regs[rxbase + R_RX_CTRL0] & CTRL_I)
        eth_pulse_irq(s);

    /* If c_rx_pingpong was set flip buffers.  */
    s->rxbuf ^= s->c_rx_pingpong;
    return;
}

static void eth_cleanup(VLANClientState *vc)
{
    struct xlx_ethlite *s = vc->opaque;
    qemu_free(s);
}

static void xilinx_ethlite_init(SysBusDevice *dev)
{
    struct xlx_ethlite *s = FROM_SYSBUS(typeof (*s), dev);
    int regs;

    sysbus_init_irq(dev, &s->irq);
    s->c_tx_pingpong = qdev_get_prop_int(&dev->qdev, "txpingpong", 1);
    s->c_rx_pingpong = qdev_get_prop_int(&dev->qdev, "rxpingpong", 1);
    s->rxbuf = 0;

    regs = cpu_register_io_memory(0, eth_read, eth_write, s);
    sysbus_init_mmio(dev, R_MAX * 4, regs);

    qdev_get_macaddr(&dev->qdev, s->macaddr);
    s->vc = qdev_get_vlan_client(&dev->qdev,
                                 eth_rx, eth_can_rx, eth_cleanup, s);
}

static void xilinx_ethlite_register(void)
{
    sysbus_register_dev("xilinx,ethlite", sizeof (struct xlx_ethlite),
                        xilinx_ethlite_init);
}

device_init(xilinx_ethlite_register)
