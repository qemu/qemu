/*
 * QEMU model of XGMAC Ethernet.
 *
 * derived from the Xilinx AXI-Ethernet by Edgar E. Iglesias.
 *
 * Copyright (c) 2011 Calxeda, Inc.
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

#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"
#include "net/net.h"
#include "net/checksum.h"

#ifdef DEBUG_XGMAC
#define DEBUGF_BRK(message, args...) do { \
                                         fprintf(stderr, (message), ## args); \
                                     } while (0)
#else
#define DEBUGF_BRK(message, args...) do { } while (0)
#endif

#define XGMAC_CONTROL           0x00000000   /* MAC Configuration */
#define XGMAC_FRAME_FILTER      0x00000001   /* MAC Frame Filter */
#define XGMAC_FLOW_CTRL         0x00000006   /* MAC Flow Control */
#define XGMAC_VLAN_TAG          0x00000007   /* VLAN Tags */
#define XGMAC_VERSION           0x00000008   /* Version */
/* VLAN tag for insertion or replacement into tx frames */
#define XGMAC_VLAN_INCL         0x00000009
#define XGMAC_LPI_CTRL          0x0000000a   /* LPI Control and Status */
#define XGMAC_LPI_TIMER         0x0000000b   /* LPI Timers Control */
#define XGMAC_TX_PACE           0x0000000c   /* Transmit Pace and Stretch */
#define XGMAC_VLAN_HASH         0x0000000d   /* VLAN Hash Table */
#define XGMAC_DEBUG             0x0000000e   /* Debug */
#define XGMAC_INT_STATUS        0x0000000f   /* Interrupt and Control */
/* HASH table registers */
#define XGMAC_HASH(n)           ((0x00000300/4) + (n))
#define XGMAC_NUM_HASH          16
/* Operation Mode */
#define XGMAC_OPMODE            (0x00000400/4)
/* Remote Wake-Up Frame Filter */
#define XGMAC_REMOTE_WAKE       (0x00000700/4)
/* PMT Control and Status */
#define XGMAC_PMT               (0x00000704/4)

#define XGMAC_ADDR_HIGH(reg)    (0x00000010+((reg) * 2))
#define XGMAC_ADDR_LOW(reg)     (0x00000011+((reg) * 2))

#define DMA_BUS_MODE            0x000003c0   /* Bus Mode */
#define DMA_XMT_POLL_DEMAND     0x000003c1   /* Transmit Poll Demand */
#define DMA_RCV_POLL_DEMAND     0x000003c2   /* Received Poll Demand */
#define DMA_RCV_BASE_ADDR       0x000003c3   /* Receive List Base */
#define DMA_TX_BASE_ADDR        0x000003c4   /* Transmit List Base */
#define DMA_STATUS              0x000003c5   /* Status Register */
#define DMA_CONTROL             0x000003c6   /* Ctrl (Operational Mode) */
#define DMA_INTR_ENA            0x000003c7   /* Interrupt Enable */
#define DMA_MISSED_FRAME_CTR    0x000003c8   /* Missed Frame Counter */
/* Receive Interrupt Watchdog Timer */
#define DMA_RI_WATCHDOG_TIMER   0x000003c9
#define DMA_AXI_BUS             0x000003ca   /* AXI Bus Mode */
#define DMA_AXI_STATUS          0x000003cb   /* AXI Status */
#define DMA_CUR_TX_DESC_ADDR    0x000003d2   /* Current Host Tx Descriptor */
#define DMA_CUR_RX_DESC_ADDR    0x000003d3   /* Current Host Rx Descriptor */
#define DMA_CUR_TX_BUF_ADDR     0x000003d4   /* Current Host Tx Buffer */
#define DMA_CUR_RX_BUF_ADDR     0x000003d5   /* Current Host Rx Buffer */
#define DMA_HW_FEATURE          0x000003d6   /* Enabled Hardware Features */

/* DMA Status register defines */
#define DMA_STATUS_GMI          0x08000000   /* MMC interrupt */
#define DMA_STATUS_GLI          0x04000000   /* GMAC Line interface int */
#define DMA_STATUS_EB_MASK      0x00380000   /* Error Bits Mask */
#define DMA_STATUS_EB_TX_ABORT  0x00080000   /* Error Bits - TX Abort */
#define DMA_STATUS_EB_RX_ABORT  0x00100000   /* Error Bits - RX Abort */
#define DMA_STATUS_TS_MASK      0x00700000   /* Transmit Process State */
#define DMA_STATUS_TS_SHIFT     20
#define DMA_STATUS_RS_MASK      0x000e0000   /* Receive Process State */
#define DMA_STATUS_RS_SHIFT     17
#define DMA_STATUS_NIS          0x00010000   /* Normal Interrupt Summary */
#define DMA_STATUS_AIS          0x00008000   /* Abnormal Interrupt Summary */
#define DMA_STATUS_ERI          0x00004000   /* Early Receive Interrupt */
#define DMA_STATUS_FBI          0x00002000   /* Fatal Bus Error Interrupt */
#define DMA_STATUS_ETI          0x00000400   /* Early Transmit Interrupt */
#define DMA_STATUS_RWT          0x00000200   /* Receive Watchdog Timeout */
#define DMA_STATUS_RPS          0x00000100   /* Receive Process Stopped */
#define DMA_STATUS_RU           0x00000080   /* Receive Buffer Unavailable */
#define DMA_STATUS_RI           0x00000040   /* Receive Interrupt */
#define DMA_STATUS_UNF          0x00000020   /* Transmit Underflow */
#define DMA_STATUS_OVF          0x00000010   /* Receive Overflow */
#define DMA_STATUS_TJT          0x00000008   /* Transmit Jabber Timeout */
#define DMA_STATUS_TU           0x00000004   /* Transmit Buffer Unavailable */
#define DMA_STATUS_TPS          0x00000002   /* Transmit Process Stopped */
#define DMA_STATUS_TI           0x00000001   /* Transmit Interrupt */

/* DMA Control register defines */
#define DMA_CONTROL_ST          0x00002000   /* Start/Stop Transmission */
#define DMA_CONTROL_SR          0x00000002   /* Start/Stop Receive */
#define DMA_CONTROL_DFF         0x01000000   /* Disable flush of rx frames */

struct desc {
    uint32_t ctl_stat;
    uint16_t buffer1_size;
    uint16_t buffer2_size;
    uint32_t buffer1_addr;
    uint32_t buffer2_addr;
    uint32_t ext_stat;
    uint32_t res[3];
};

#define R_MAX 0x400

typedef struct RxTxStats {
    uint64_t rx_bytes;
    uint64_t tx_bytes;

    uint64_t rx;
    uint64_t rx_bcast;
    uint64_t rx_mcast;
} RxTxStats;

#define TYPE_XGMAC "xgmac"
#define XGMAC(obj) OBJECT_CHECK(XgmacState, (obj), TYPE_XGMAC)

typedef struct XgmacState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq sbd_irq;
    qemu_irq pmt_irq;
    qemu_irq mci_irq;
    NICState *nic;
    NICConf conf;

    struct RxTxStats stats;
    uint32_t regs[R_MAX];
} XgmacState;

const VMStateDescription vmstate_rxtx_stats = {
    .name = "xgmac_stats",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT64(rx_bytes, RxTxStats),
        VMSTATE_UINT64(tx_bytes, RxTxStats),
        VMSTATE_UINT64(rx, RxTxStats),
        VMSTATE_UINT64(rx_bcast, RxTxStats),
        VMSTATE_UINT64(rx_mcast, RxTxStats),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xgmac = {
    .name = "xgmac",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(stats, XgmacState, 0, vmstate_rxtx_stats, RxTxStats),
        VMSTATE_UINT32_ARRAY(regs, XgmacState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void xgmac_read_desc(XgmacState *s, struct desc *d, int rx)
{
    uint32_t addr = rx ? s->regs[DMA_CUR_RX_DESC_ADDR] :
        s->regs[DMA_CUR_TX_DESC_ADDR];
    cpu_physical_memory_read(addr, d, sizeof(*d));
}

static void xgmac_write_desc(XgmacState *s, struct desc *d, int rx)
{
    int reg = rx ? DMA_CUR_RX_DESC_ADDR : DMA_CUR_TX_DESC_ADDR;
    uint32_t addr = s->regs[reg];

    if (!rx && (d->ctl_stat & 0x00200000)) {
        s->regs[reg] = s->regs[DMA_TX_BASE_ADDR];
    } else if (rx && (d->buffer1_size & 0x8000)) {
        s->regs[reg] = s->regs[DMA_RCV_BASE_ADDR];
    } else {
        s->regs[reg] += sizeof(*d);
    }
    cpu_physical_memory_write(addr, d, sizeof(*d));
}

static void xgmac_enet_send(XgmacState *s)
{
    struct desc bd;
    int frame_size;
    int len;
    uint8_t frame[8192];
    uint8_t *ptr;

    ptr = frame;
    frame_size = 0;
    while (1) {
        xgmac_read_desc(s, &bd, 0);
        if ((bd.ctl_stat & 0x80000000) == 0) {
            /* Run out of descriptors to transmit.  */
            break;
        }
        len = (bd.buffer1_size & 0xfff) + (bd.buffer2_size & 0xfff);

        if ((bd.buffer1_size & 0xfff) > 2048) {
            DEBUGF_BRK("qemu:%s:ERROR...ERROR...ERROR... -- "
                        "xgmac buffer 1 len on send > 2048 (0x%x)\n",
                         __func__, bd.buffer1_size & 0xfff);
        }
        if ((bd.buffer2_size & 0xfff) != 0) {
            DEBUGF_BRK("qemu:%s:ERROR...ERROR...ERROR... -- "
                        "xgmac buffer 2 len on send != 0 (0x%x)\n",
                        __func__, bd.buffer2_size & 0xfff);
        }
        if (len >= sizeof(frame)) {
            DEBUGF_BRK("qemu:%s: buffer overflow %d read into %zu "
                        "buffer\n" , __func__, len, sizeof(frame));
            DEBUGF_BRK("qemu:%s: buffer1.size=%d; buffer2.size=%d\n",
                        __func__, bd.buffer1_size, bd.buffer2_size);
        }

        cpu_physical_memory_read(bd.buffer1_addr, ptr, len);
        ptr += len;
        frame_size += len;
        if (bd.ctl_stat & 0x20000000) {
            /* Last buffer in frame.  */
            qemu_send_packet(qemu_get_queue(s->nic), frame, len);
            ptr = frame;
            frame_size = 0;
            s->regs[DMA_STATUS] |= DMA_STATUS_TI | DMA_STATUS_NIS;
        }
        bd.ctl_stat &= ~0x80000000;
        /* Write back the modified descriptor.  */
        xgmac_write_desc(s, &bd, 0);
    }
}

static void enet_update_irq(XgmacState *s)
{
    int stat = s->regs[DMA_STATUS] & s->regs[DMA_INTR_ENA];
    qemu_set_irq(s->sbd_irq, !!stat);
}

static uint64_t enet_read(void *opaque, hwaddr addr, unsigned size)
{
    XgmacState *s = opaque;
    uint64_t r = 0;
    addr >>= 2;

    switch (addr) {
    case XGMAC_VERSION:
        r = 0x1012;
        break;
    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            r = s->regs[addr];
        }
        break;
    }
    return r;
}

static void enet_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned size)
{
    XgmacState *s = opaque;

    addr >>= 2;
    switch (addr) {
    case DMA_BUS_MODE:
        s->regs[DMA_BUS_MODE] = value & ~0x1;
        break;
    case DMA_XMT_POLL_DEMAND:
        xgmac_enet_send(s);
        break;
    case DMA_STATUS:
        s->regs[DMA_STATUS] = s->regs[DMA_STATUS] & ~value;
        break;
    case DMA_RCV_BASE_ADDR:
        s->regs[DMA_RCV_BASE_ADDR] = s->regs[DMA_CUR_RX_DESC_ADDR] = value;
        break;
    case DMA_TX_BASE_ADDR:
        s->regs[DMA_TX_BASE_ADDR] = s->regs[DMA_CUR_TX_DESC_ADDR] = value;
        break;
    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            s->regs[addr] = value;
        }
        break;
    }
    enet_update_irq(s);
}

static const MemoryRegionOps enet_mem_ops = {
    .read = enet_read,
    .write = enet_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int eth_can_rx(NetClientState *nc)
{
    XgmacState *s = qemu_get_nic_opaque(nc);

    /* RX enabled?  */
    return s->regs[DMA_CONTROL] & DMA_CONTROL_SR;
}

static ssize_t eth_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    XgmacState *s = qemu_get_nic_opaque(nc);
    static const unsigned char sa_bcast[6] = {0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff};
    int unicast, broadcast, multicast;
    struct desc bd;
    ssize_t ret;

    unicast = ~buf[0] & 0x1;
    broadcast = memcmp(buf, sa_bcast, 6) == 0;
    multicast = !unicast && !broadcast;
    if (size < 12) {
        s->regs[DMA_STATUS] |= DMA_STATUS_RI | DMA_STATUS_NIS;
        ret = -1;
        goto out;
    }

    xgmac_read_desc(s, &bd, 1);
    if ((bd.ctl_stat & 0x80000000) == 0) {
        s->regs[DMA_STATUS] |= DMA_STATUS_RU | DMA_STATUS_AIS;
        ret = size;
        goto out;
    }

    cpu_physical_memory_write(bd.buffer1_addr, buf, size);

    /* Add in the 4 bytes for crc (the real hw returns length incl crc) */
    size += 4;
    bd.ctl_stat = (size << 16) | 0x300;
    xgmac_write_desc(s, &bd, 1);

    s->stats.rx_bytes += size;
    s->stats.rx++;
    if (multicast) {
        s->stats.rx_mcast++;
    } else if (broadcast) {
        s->stats.rx_bcast++;
    }

    s->regs[DMA_STATUS] |= DMA_STATUS_RI | DMA_STATUS_NIS;
    ret = size;

out:
    enet_update_irq(s);
    return ret;
}

static void eth_cleanup(NetClientState *nc)
{
    XgmacState *s = qemu_get_nic_opaque(nc);

    s->nic = NULL;
}

static NetClientInfo net_xgmac_enet_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = eth_can_rx,
    .receive = eth_rx,
    .cleanup = eth_cleanup,
};

static int xgmac_enet_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    XgmacState *s = XGMAC(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &enet_mem_ops, s,
                          "xgmac", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->sbd_irq);
    sysbus_init_irq(sbd, &s->pmt_irq);
    sysbus_init_irq(sbd, &s->mci_irq);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_xgmac_enet_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    s->regs[XGMAC_ADDR_HIGH(0)] = (s->conf.macaddr.a[5] << 8) |
                                   s->conf.macaddr.a[4];
    s->regs[XGMAC_ADDR_LOW(0)] = (s->conf.macaddr.a[3] << 24) |
                                 (s->conf.macaddr.a[2] << 16) |
                                 (s->conf.macaddr.a[1] << 8) |
                                  s->conf.macaddr.a[0];

    return 0;
}

static Property xgmac_properties[] = {
    DEFINE_NIC_PROPERTIES(XgmacState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void xgmac_enet_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sbc->init = xgmac_enet_init;
    dc->vmsd = &vmstate_xgmac;
    dc->props = xgmac_properties;
}

static const TypeInfo xgmac_enet_info = {
    .name          = TYPE_XGMAC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XgmacState),
    .class_init    = xgmac_enet_class_init,
};

static void xgmac_enet_register_types(void)
{
    type_register_static(&xgmac_enet_info);
}

type_init(xgmac_enet_register_types)
