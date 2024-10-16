/*
 * QEMU model of Xilinx AXI-DMA block.
 *
 * Copyright (c) 2011 Edgar E. Iglesias.
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
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "sysemu/dma.h"
#include "hw/stream.h"
#include "qom/object.h"
#include "trace.h"

#define D(x)

#define TYPE_XILINX_AXI_DMA "xlnx.axi-dma"
#define TYPE_XILINX_AXI_DMA_DATA_STREAM "xilinx-axi-dma-data-stream"
#define TYPE_XILINX_AXI_DMA_CONTROL_STREAM "xilinx-axi-dma-control-stream"

OBJECT_DECLARE_SIMPLE_TYPE(XilinxAXIDMA, XILINX_AXI_DMA)

typedef struct XilinxAXIDMAStreamSink XilinxAXIDMAStreamSink;
DECLARE_INSTANCE_CHECKER(XilinxAXIDMAStreamSink, XILINX_AXI_DMA_DATA_STREAM,
                         TYPE_XILINX_AXI_DMA_DATA_STREAM)

DECLARE_INSTANCE_CHECKER(XilinxAXIDMAStreamSink, XILINX_AXI_DMA_CONTROL_STREAM,
                         TYPE_XILINX_AXI_DMA_CONTROL_STREAM)

#define R_DMACR             (0x00 / 4)
#define R_DMASR             (0x04 / 4)
#define R_CURDESC           (0x08 / 4)
#define R_TAILDESC          (0x10 / 4)
#define R_MAX               (0x30 / 4)

#define CONTROL_PAYLOAD_WORDS 5
#define CONTROL_PAYLOAD_SIZE (CONTROL_PAYLOAD_WORDS * (sizeof(uint32_t)))


enum {
    DMACR_RUNSTOP = 1,
    DMACR_TAILPTR_MODE = 2,
    DMACR_RESET = 4
};

enum {
    DMASR_HALTED = 1,
    DMASR_IDLE  = 2,
    DMASR_SLVERR = 1 << 5,
    DMASR_DECERR = 1 << 6,
    DMASR_IOC_IRQ  = 1 << 12,
    DMASR_DLY_IRQ  = 1 << 13,
    DMASR_ERR_IRQ  = 1 << 14,

    DMASR_IRQ_MASK = 7 << 12
};

struct SDesc {
    uint64_t nxtdesc;
    uint64_t buffer_address;
    uint64_t reserved;
    uint32_t control;
    uint32_t status;
    uint8_t app[CONTROL_PAYLOAD_SIZE];
};

enum {
    SDESC_CTRL_EOF = (1 << 26),
    SDESC_CTRL_SOF = (1 << 27),

    SDESC_CTRL_LEN_MASK = (1 << 23) - 1
};

enum {
    SDESC_STATUS_EOF = (1 << 26),
    SDESC_STATUS_SOF_BIT = 27,
    SDESC_STATUS_SOF = (1 << SDESC_STATUS_SOF_BIT),
    SDESC_STATUS_COMPLETE = (1 << 31)
};

struct Stream {
    struct XilinxAXIDMA *dma;
    ptimer_state *ptimer;
    qemu_irq irq;

    int nr;

    bool sof;
    struct SDesc desc;
    unsigned int complete_cnt;
    uint32_t regs[R_MAX];
    uint8_t app[20];
    unsigned char txbuf[16 * 1024];
};

struct XilinxAXIDMAStreamSink {
    Object parent;

    struct XilinxAXIDMA *dma;
};

struct XilinxAXIDMA {
    SysBusDevice busdev;
    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace as;

    uint32_t freqhz;
    StreamSink *tx_data_dev;
    StreamSink *tx_control_dev;
    XilinxAXIDMAStreamSink rx_data_dev;
    XilinxAXIDMAStreamSink rx_control_dev;

    struct Stream streams[2];

    StreamCanPushNotifyFn notify;
    void *notify_opaque;
};

/*
 * Helper calls to extract info from descriptors and other trivial
 * state from regs.
 */
static inline int stream_desc_sof(struct SDesc *d)
{
    return d->control & SDESC_CTRL_SOF;
}

static inline int stream_desc_eof(struct SDesc *d)
{
    return d->control & SDESC_CTRL_EOF;
}

static inline int stream_resetting(struct Stream *s)
{
    return !!(s->regs[R_DMACR] & DMACR_RESET);
}

static inline int stream_running(struct Stream *s)
{
    return s->regs[R_DMACR] & DMACR_RUNSTOP;
}

static inline int stream_idle(struct Stream *s)
{
    return !!(s->regs[R_DMASR] & DMASR_IDLE);
}

static inline int stream_halted(struct Stream *s)
{
    return !!(s->regs[R_DMASR] & DMASR_HALTED);
}

static void stream_reset(struct Stream *s)
{
    s->regs[R_DMASR] = DMASR_HALTED;  /* starts up halted.  */
    s->regs[R_DMACR] = 1 << 16; /* Starts with one in compl threshold.  */
    s->sof = true;
}

/* Map an offset addr into a channel index.  */
static inline int streamid_from_addr(hwaddr addr)
{
    int sid;

    sid = addr / (0x30);
    sid &= 1;
    return sid;
}

static MemTxResult stream_desc_load(struct Stream *s, hwaddr addr)
{
    struct SDesc *d = &s->desc;

    MemTxResult result = address_space_read(&s->dma->as,
                                            addr, MEMTXATTRS_UNSPECIFIED,
                                            d, sizeof *d);
    if (result != MEMTX_OK) {
        trace_xilinx_axidma_loading_desc_fail(result);

        if (result == MEMTX_DECODE_ERROR) {
            s->regs[R_DMASR] |= DMASR_DECERR;
        } else {
            s->regs[R_DMASR] |= DMASR_SLVERR;
        }

        s->regs[R_DMACR] &= ~DMACR_RUNSTOP;
        s->regs[R_DMASR] |= DMASR_HALTED;
        s->regs[R_DMASR] |= DMASR_ERR_IRQ;
        return result;
    }

    /* Convert from LE into host endianness.  */
    d->buffer_address = le64_to_cpu(d->buffer_address);
    d->nxtdesc = le64_to_cpu(d->nxtdesc);
    d->control = le32_to_cpu(d->control);
    d->status = le32_to_cpu(d->status);
    return result;
}

static void stream_desc_store(struct Stream *s, hwaddr addr)
{
    struct SDesc *d = &s->desc;

    /* Convert from host endianness into LE.  */
    d->buffer_address = cpu_to_le64(d->buffer_address);
    d->nxtdesc = cpu_to_le64(d->nxtdesc);
    d->control = cpu_to_le32(d->control);
    d->status = cpu_to_le32(d->status);
    address_space_write(&s->dma->as, addr, MEMTXATTRS_UNSPECIFIED,
                        d, sizeof *d);
}

static void stream_update_irq(struct Stream *s)
{
    unsigned int pending, mask, irq;

    pending = s->regs[R_DMASR] & DMASR_IRQ_MASK;
    mask = s->regs[R_DMACR] & DMASR_IRQ_MASK;

    irq = pending & mask;

    qemu_set_irq(s->irq, !!irq);
}

static void stream_reload_complete_cnt(struct Stream *s)
{
    unsigned int comp_th;
    comp_th = (s->regs[R_DMACR] >> 16) & 0xff;
    s->complete_cnt = comp_th;
}

static void timer_hit(void *opaque)
{
    struct Stream *s = opaque;

    stream_reload_complete_cnt(s);
    s->regs[R_DMASR] |= DMASR_DLY_IRQ;
    stream_update_irq(s);
}

static void stream_complete(struct Stream *s)
{
    unsigned int comp_delay;

    /* Start the delayed timer.  */
    ptimer_transaction_begin(s->ptimer);
    comp_delay = s->regs[R_DMACR] >> 24;
    if (comp_delay) {
        ptimer_stop(s->ptimer);
        ptimer_set_count(s->ptimer, comp_delay);
        ptimer_run(s->ptimer, 1);
    }

    s->complete_cnt--;
    if (s->complete_cnt == 0) {
        /* Raise the IOC irq.  */
        s->regs[R_DMASR] |= DMASR_IOC_IRQ;
        stream_reload_complete_cnt(s);
    }
    ptimer_transaction_commit(s->ptimer);
}

static void stream_process_mem2s(struct Stream *s, StreamSink *tx_data_dev,
                                 StreamSink *tx_control_dev)
{
    uint32_t prev_d;
    uint32_t txlen;
    uint64_t addr;
    bool eop;

    if (!stream_running(s) || stream_idle(s) || stream_halted(s)) {
        return;
    }

    while (1) {
        if (MEMTX_OK != stream_desc_load(s, s->regs[R_CURDESC])) {
            break;
        }

        if (s->desc.status & SDESC_STATUS_COMPLETE) {
            s->regs[R_DMASR] |= DMASR_HALTED;
            break;
        }

        if (stream_desc_sof(&s->desc)) {
            stream_push(tx_control_dev, s->desc.app, sizeof(s->desc.app), true);
        }

        txlen = s->desc.control & SDESC_CTRL_LEN_MASK;

        eop = stream_desc_eof(&s->desc);
        addr = s->desc.buffer_address;
        while (txlen) {
            unsigned int len;

            len = txlen > sizeof s->txbuf ? sizeof s->txbuf : txlen;
            address_space_read(&s->dma->as, addr,
                               MEMTXATTRS_UNSPECIFIED,
                               s->txbuf, len);
            stream_push(tx_data_dev, s->txbuf, len, eop && len == txlen);
            txlen -= len;
            addr += len;
        }

        if (eop) {
            stream_complete(s);
        }

        /* Update the descriptor.  */
        s->desc.status = txlen | SDESC_STATUS_COMPLETE;
        stream_desc_store(s, s->regs[R_CURDESC]);

        /* Advance.  */
        prev_d = s->regs[R_CURDESC];
        s->regs[R_CURDESC] = s->desc.nxtdesc;
        if (prev_d == s->regs[R_TAILDESC]) {
            s->regs[R_DMASR] |= DMASR_IDLE;
            break;
        }
    }
}

static size_t stream_process_s2mem(struct Stream *s, unsigned char *buf,
                                   size_t len, bool eop)
{
    uint32_t prev_d;
    unsigned int rxlen;
    size_t pos = 0;

    if (!stream_running(s) || stream_idle(s) || stream_halted(s)) {
        return 0;
    }

    while (len) {
        if (MEMTX_OK != stream_desc_load(s, s->regs[R_CURDESC])) {
            break;
        }

        if (s->desc.status & SDESC_STATUS_COMPLETE) {
            s->regs[R_DMASR] |= DMASR_HALTED;
            break;
        }

        rxlen = s->desc.control & SDESC_CTRL_LEN_MASK;
        if (rxlen > len) {
            /* It fits.  */
            rxlen = len;
        }

        address_space_write(&s->dma->as, s->desc.buffer_address,
                            MEMTXATTRS_UNSPECIFIED, buf + pos, rxlen);
        len -= rxlen;
        pos += rxlen;

        /* Update the descriptor.  */
        if (eop) {
            stream_complete(s);
            memcpy(s->desc.app, s->app, sizeof(s->desc.app));
            s->desc.status |= SDESC_STATUS_EOF;
        }

        s->desc.status |= s->sof << SDESC_STATUS_SOF_BIT;
        s->desc.status |= SDESC_STATUS_COMPLETE;
        stream_desc_store(s, s->regs[R_CURDESC]);
        s->sof = eop;

        /* Advance.  */
        prev_d = s->regs[R_CURDESC];
        s->regs[R_CURDESC] = s->desc.nxtdesc;
        if (prev_d == s->regs[R_TAILDESC]) {
            s->regs[R_DMASR] |= DMASR_IDLE;
            break;
        }
    }

    return pos;
}

static void xilinx_axidma_reset(DeviceState *dev)
{
    int i;
    XilinxAXIDMA *s = XILINX_AXI_DMA(dev);

    for (i = 0; i < 2; i++) {
        stream_reset(&s->streams[i]);
    }
}

static size_t
xilinx_axidma_control_stream_push(StreamSink *obj, unsigned char *buf,
                                  size_t len, bool eop)
{
    XilinxAXIDMAStreamSink *cs = XILINX_AXI_DMA_CONTROL_STREAM(obj);
    struct Stream *s = &cs->dma->streams[1];

    if (len != CONTROL_PAYLOAD_SIZE) {
        hw_error("AXI DMA requires %d byte control stream payload\n",
                 (int)CONTROL_PAYLOAD_SIZE);
    }

    memcpy(s->app, buf, len);
    return len;
}

static bool
xilinx_axidma_data_stream_can_push(StreamSink *obj,
                                   StreamCanPushNotifyFn notify,
                                   void *notify_opaque)
{
    XilinxAXIDMAStreamSink *ds = XILINX_AXI_DMA_DATA_STREAM(obj);
    struct Stream *s = &ds->dma->streams[1];

    if (!stream_running(s) || stream_idle(s) || stream_halted(s)) {
        ds->dma->notify = notify;
        ds->dma->notify_opaque = notify_opaque;
        return false;
    }

    return true;
}

static size_t
xilinx_axidma_data_stream_push(StreamSink *obj, unsigned char *buf, size_t len,
                               bool eop)
{
    XilinxAXIDMAStreamSink *ds = XILINX_AXI_DMA_DATA_STREAM(obj);
    struct Stream *s = &ds->dma->streams[1];
    size_t ret;

    ret = stream_process_s2mem(s, buf, len, eop);
    stream_update_irq(s);
    return ret;
}

static uint64_t axidma_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    XilinxAXIDMA *d = opaque;
    struct Stream *s;
    uint32_t r = 0;
    int sid;

    sid = streamid_from_addr(addr);
    s = &d->streams[sid];

    addr = addr % 0x30;
    addr >>= 2;
    switch (addr) {
        case R_DMACR:
            /* Simulate one cycles reset delay.  */
            s->regs[addr] &= ~DMACR_RESET;
            r = s->regs[addr];
            break;
        case R_DMASR:
            s->regs[addr] &= 0xffff;
            s->regs[addr] |= (s->complete_cnt & 0xff) << 16;
            s->regs[addr] |= (ptimer_get_count(s->ptimer) & 0xff) << 24;
            r = s->regs[addr];
            break;
        default:
            r = s->regs[addr];
            D(qemu_log("%s ch=%d addr=" HWADDR_FMT_plx " v=%x\n",
                           __func__, sid, addr * 4, r));
            break;
    }
    return r;

}

static void axidma_write(void *opaque, hwaddr addr,
                         uint64_t value, unsigned size)
{
    XilinxAXIDMA *d = opaque;
    struct Stream *s;
    int sid;

    sid = streamid_from_addr(addr);
    s = &d->streams[sid];

    addr = addr % 0x30;
    addr >>= 2;
    switch (addr) {
        case R_DMACR:
            /* Tailptr mode is always on.  */
            value |= DMACR_TAILPTR_MODE;
            /* Remember our previous reset state.  */
            value |= (s->regs[addr] & DMACR_RESET);
            s->regs[addr] = value;

            if (value & DMACR_RESET) {
                stream_reset(s);
            }

            if ((value & 1) && !stream_resetting(s)) {
                /* Start processing.  */
                s->regs[R_DMASR] &= ~(DMASR_HALTED | DMASR_IDLE);
            }
            stream_reload_complete_cnt(s);
            break;

        case R_DMASR:
            /* Mask away write to clear irq lines.  */
            value &= ~(value & DMASR_IRQ_MASK);
            s->regs[addr] = value;
            break;

        case R_TAILDESC:
            s->regs[addr] = value;
            s->regs[R_DMASR] &= ~DMASR_IDLE; /* Not idle.  */
            if (!sid) {
                stream_process_mem2s(s, d->tx_data_dev, d->tx_control_dev);
            }
            break;
        default:
            D(qemu_log("%s: ch=%d addr=" HWADDR_FMT_plx " v=%x\n",
                  __func__, sid, addr * 4, (unsigned)value));
            s->regs[addr] = value;
            break;
    }
    if (sid == 1 && d->notify) {
        StreamCanPushNotifyFn notifytmp = d->notify;
        d->notify = NULL;
        notifytmp(d->notify_opaque);
    }
    stream_update_irq(s);
}

static const MemoryRegionOps axidma_ops = {
    .read = axidma_read,
    .write = axidma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void xilinx_axidma_realize(DeviceState *dev, Error **errp)
{
    XilinxAXIDMA *s = XILINX_AXI_DMA(dev);
    XilinxAXIDMAStreamSink *ds = XILINX_AXI_DMA_DATA_STREAM(&s->rx_data_dev);
    XilinxAXIDMAStreamSink *cs = XILINX_AXI_DMA_CONTROL_STREAM(
                                                            &s->rx_control_dev);
    int i;

    object_property_add_link(OBJECT(ds), "dma", TYPE_XILINX_AXI_DMA,
                             (Object **)&ds->dma,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(OBJECT(cs), "dma", TYPE_XILINX_AXI_DMA,
                             (Object **)&cs->dma,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_set_link(OBJECT(ds), "dma", OBJECT(s), &error_abort);
    object_property_set_link(OBJECT(cs), "dma", OBJECT(s), &error_abort);

    for (i = 0; i < 2; i++) {
        struct Stream *st = &s->streams[i];

        st->dma = s;
        st->nr = i;
        st->ptimer = ptimer_init(timer_hit, st, PTIMER_POLICY_LEGACY);
        ptimer_transaction_begin(st->ptimer);
        ptimer_set_freq(st->ptimer, s->freqhz);
        ptimer_transaction_commit(st->ptimer);
    }

    address_space_init(&s->as,
                       s->dma_mr ? s->dma_mr : get_system_memory(), "dma");
}

static void xilinx_axidma_init(Object *obj)
{
    XilinxAXIDMA *s = XILINX_AXI_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    object_initialize_child(OBJECT(s), "axistream-connected-target",
                            &s->rx_data_dev, TYPE_XILINX_AXI_DMA_DATA_STREAM);
    object_initialize_child(OBJECT(s), "axistream-control-connected-target",
                            &s->rx_control_dev,
                            TYPE_XILINX_AXI_DMA_CONTROL_STREAM);

    sysbus_init_irq(sbd, &s->streams[0].irq);
    sysbus_init_irq(sbd, &s->streams[1].irq);

    memory_region_init_io(&s->iomem, obj, &axidma_ops, s,
                          "xlnx.axi-dma", R_MAX * 4 * 2);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property axidma_properties[] = {
    DEFINE_PROP_UINT32("freqhz", XilinxAXIDMA, freqhz, 50000000),
    DEFINE_PROP_LINK("axistream-connected", XilinxAXIDMA,
                     tx_data_dev, TYPE_STREAM_SINK, StreamSink *),
    DEFINE_PROP_LINK("axistream-control-connected", XilinxAXIDMA,
                     tx_control_dev, TYPE_STREAM_SINK, StreamSink *),
    DEFINE_PROP_LINK("dma", XilinxAXIDMA, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void axidma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_axidma_realize;
    device_class_set_legacy_reset(dc, xilinx_axidma_reset);
    device_class_set_props(dc, axidma_properties);
}

static StreamSinkClass xilinx_axidma_data_stream_class = {
    .push = xilinx_axidma_data_stream_push,
    .can_push = xilinx_axidma_data_stream_can_push,
};

static StreamSinkClass xilinx_axidma_control_stream_class = {
    .push = xilinx_axidma_control_stream_push,
};

static void xilinx_axidma_stream_class_init(ObjectClass *klass, void *data)
{
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);

    ssc->push = ((StreamSinkClass *)data)->push;
    ssc->can_push = ((StreamSinkClass *)data)->can_push;
}

static const TypeInfo axidma_info = {
    .name          = TYPE_XILINX_AXI_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAXIDMA),
    .class_init    = axidma_class_init,
    .instance_init = xilinx_axidma_init,
};

static const TypeInfo xilinx_axidma_data_stream_info = {
    .name          = TYPE_XILINX_AXI_DMA_DATA_STREAM,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(XilinxAXIDMAStreamSink),
    .class_init    = xilinx_axidma_stream_class_init,
    .class_data    = &xilinx_axidma_data_stream_class,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    }
};

static const TypeInfo xilinx_axidma_control_stream_info = {
    .name          = TYPE_XILINX_AXI_DMA_CONTROL_STREAM,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(XilinxAXIDMAStreamSink),
    .class_init    = xilinx_axidma_stream_class_init,
    .class_data    = &xilinx_axidma_control_stream_class,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    }
};

static void xilinx_axidma_register_types(void)
{
    type_register_static(&axidma_info);
    type_register_static(&xilinx_axidma_data_stream_info);
    type_register_static(&xilinx_axidma_control_stream_info);
}

type_init(xilinx_axidma_register_types)
