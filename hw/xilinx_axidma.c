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

#include "sysbus.h"
#include "qemu-char.h"
#include "qemu-timer.h"
#include "qemu-log.h"
#include "qdev-addr.h"

#include "xilinx_axidma.h"

#define D(x)

#define R_DMACR             (0x00 / 4)
#define R_DMASR             (0x04 / 4)
#define R_CURDESC           (0x08 / 4)
#define R_TAILDESC          (0x10 / 4)
#define R_MAX               (0x30 / 4)

enum {
    DMACR_RUNSTOP = 1,
    DMACR_TAILPTR_MODE = 2,
    DMACR_RESET = 4
};

enum {
    DMASR_HALTED = 1,
    DMASR_IDLE  = 2,
    DMASR_IOC_IRQ  = 1 << 12,
    DMASR_DLY_IRQ  = 1 << 13,

    DMASR_IRQ_MASK = 7 << 12
};

struct SDesc {
    uint64_t nxtdesc;
    uint64_t buffer_address;
    uint64_t reserved;
    uint32_t control;
    uint32_t status;
    uint32_t app[6];
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

struct AXIStream {
    QEMUBH *bh;
    ptimer_state *ptimer;
    qemu_irq irq;

    int nr;

    struct SDesc desc;
    int pos;
    unsigned int complete_cnt;
    uint32_t regs[R_MAX];
};

struct XilinxAXIDMA {
    SysBusDevice busdev;
    uint32_t freqhz;
    void *dmach;

    struct AXIStream streams[2];
};

/*
 * Helper calls to extract info from desriptors and other trivial
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

static inline int stream_resetting(struct AXIStream *s)
{
    return !!(s->regs[R_DMACR] & DMACR_RESET);
}

static inline int stream_running(struct AXIStream *s)
{
    return s->regs[R_DMACR] & DMACR_RUNSTOP;
}

static inline int stream_halted(struct AXIStream *s)
{
    return s->regs[R_DMASR] & DMASR_HALTED;
}

static inline int stream_idle(struct AXIStream *s)
{
    return !!(s->regs[R_DMASR] & DMASR_IDLE);
}

static void stream_reset(struct AXIStream *s)
{
    s->regs[R_DMASR] = DMASR_HALTED;  /* starts up halted.  */
    s->regs[R_DMACR] = 1 << 16; /* Starts with one in compl threshold.  */
}

/* Map an offset addr into a channel index.  */
static inline int streamid_from_addr(target_phys_addr_t addr)
{
    int sid;

    sid = addr / (0x30);
    sid &= 1;
    return sid;
}

#ifdef DEBUG_ENET
static void stream_desc_show(struct SDesc *d)
{
    qemu_log("buffer_addr  = " PRIx64 "\n", d->buffer_address);
    qemu_log("nxtdesc      = " PRIx64 "\n", d->nxtdesc);
    qemu_log("control      = %x\n", d->control);
    qemu_log("status       = %x\n", d->status);
}
#endif

static void stream_desc_load(struct AXIStream *s, target_phys_addr_t addr)
{
    struct SDesc *d = &s->desc;
    int i;

    cpu_physical_memory_read(addr, (void *) d, sizeof *d);

    /* Convert from LE into host endianness.  */
    d->buffer_address = le64_to_cpu(d->buffer_address);
    d->nxtdesc = le64_to_cpu(d->nxtdesc);
    d->control = le32_to_cpu(d->control);
    d->status = le32_to_cpu(d->status);
    for (i = 0; i < ARRAY_SIZE(d->app); i++) {
        d->app[i] = le32_to_cpu(d->app[i]);
    }
}

static void stream_desc_store(struct AXIStream *s, target_phys_addr_t addr)
{
    struct SDesc *d = &s->desc;
    int i;

    /* Convert from host endianness into LE.  */
    d->buffer_address = cpu_to_le64(d->buffer_address);
    d->nxtdesc = cpu_to_le64(d->nxtdesc);
    d->control = cpu_to_le32(d->control);
    d->status = cpu_to_le32(d->status);
    for (i = 0; i < ARRAY_SIZE(d->app); i++) {
        d->app[i] = cpu_to_le32(d->app[i]);
    }
    cpu_physical_memory_write(addr, (void *) d, sizeof *d);
}

static void stream_update_irq(struct AXIStream *s)
{
    unsigned int pending, mask, irq;

    pending = s->regs[R_DMASR] & DMASR_IRQ_MASK;
    mask = s->regs[R_DMACR] & DMASR_IRQ_MASK;

    irq = pending & mask;

    qemu_set_irq(s->irq, !!irq);
}

static void stream_reload_complete_cnt(struct AXIStream *s)
{
    unsigned int comp_th;
    comp_th = (s->regs[R_DMACR] >> 16) & 0xff;
    s->complete_cnt = comp_th;
}

static void timer_hit(void *opaque)
{
    struct AXIStream *s = opaque;

    stream_reload_complete_cnt(s);
    s->regs[R_DMASR] |= DMASR_DLY_IRQ;
    stream_update_irq(s);
}

static void stream_complete(struct AXIStream *s)
{
    unsigned int comp_delay;

    /* Start the delayed timer.  */
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
}

static void stream_process_mem2s(struct AXIStream *s,
                                 struct XilinxDMAConnection *dmach)
{
    uint32_t prev_d;
    unsigned char txbuf[16 * 1024];
    unsigned int txlen;
    uint32_t app[6];

    if (!stream_running(s) || stream_idle(s)) {
        return;
    }

    while (1) {
        stream_desc_load(s, s->regs[R_CURDESC]);

        if (s->desc.status & SDESC_STATUS_COMPLETE) {
            s->regs[R_DMASR] |= DMASR_IDLE;
            break;
        }

        if (stream_desc_sof(&s->desc)) {
            s->pos = 0;
            memcpy(app, s->desc.app, sizeof app);
        }

        txlen = s->desc.control & SDESC_CTRL_LEN_MASK;
        if ((txlen + s->pos) > sizeof txbuf) {
            hw_error("%s: too small internal txbuf! %d\n", __func__,
                     txlen + s->pos);
        }

        cpu_physical_memory_read(s->desc.buffer_address,
                                 txbuf + s->pos, txlen);
        s->pos += txlen;

        if (stream_desc_eof(&s->desc)) {
            xlx_dma_push_to_client(dmach, txbuf, s->pos, app);
            s->pos = 0;
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

static void stream_process_s2mem(struct AXIStream *s,
                                 unsigned char *buf, size_t len, uint32_t *app)
{
    uint32_t prev_d;
    unsigned int rxlen;
    int pos = 0;
    int sof = 1;

    if (!stream_running(s) || stream_idle(s)) {
        return;
    }

    while (len) {
        stream_desc_load(s, s->regs[R_CURDESC]);

        if (s->desc.status & SDESC_STATUS_COMPLETE) {
            s->regs[R_DMASR] |= DMASR_IDLE;
            break;
        }

        rxlen = s->desc.control & SDESC_CTRL_LEN_MASK;
        if (rxlen > len) {
            /* It fits.  */
            rxlen = len;
        }

        cpu_physical_memory_write(s->desc.buffer_address, buf + pos, rxlen);
        len -= rxlen;
        pos += rxlen;

        /* Update the descriptor.  */
        if (!len) {
            int i;

            stream_complete(s);
            for (i = 0; i < 5; i++) {
                s->desc.app[i] = app[i];
            }
            s->desc.status |= SDESC_STATUS_EOF;
        }

        s->desc.status |= sof << SDESC_STATUS_SOF_BIT;
        s->desc.status |= SDESC_STATUS_COMPLETE;
        stream_desc_store(s, s->regs[R_CURDESC]);
        sof = 0;

        /* Advance.  */
        prev_d = s->regs[R_CURDESC];
        s->regs[R_CURDESC] = s->desc.nxtdesc;
        if (prev_d == s->regs[R_TAILDESC]) {
            s->regs[R_DMASR] |= DMASR_IDLE;
            break;
        }
    }
}

static
void axidma_push(void *opaque, unsigned char *buf, size_t len, uint32_t *app)
{
    struct XilinxAXIDMA *d = opaque;
    struct AXIStream *s = &d->streams[1];

    if (!app) {
        hw_error("No stream app data!\n");
    }
    stream_process_s2mem(s, buf, len, app);
    stream_update_irq(s);
}

static uint32_t axidma_readl(void *opaque, target_phys_addr_t addr)
{
    struct XilinxAXIDMA *d = opaque;
    struct AXIStream *s;
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
            D(qemu_log("%s ch=%d addr=" TARGET_FMT_plx " v=%x\n",
                           __func__, sid, addr * 4, r));
            break;
    }
    return r;

}

static void
axidma_writel(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct XilinxAXIDMA *d = opaque;
    struct AXIStream *s;
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
                stream_process_mem2s(s, d->dmach);
            }
            break;
        default:
            D(qemu_log("%s: ch=%d addr=" TARGET_FMT_plx " v=%x\n",
                  __func__, sid, addr * 4, value));
            s->regs[addr] = value;
            break;
    }
    stream_update_irq(s);
}

static CPUReadMemoryFunc * const axidma_read[] = {
    &axidma_readl,
    &axidma_readl,
    &axidma_readl,
};

static CPUWriteMemoryFunc * const axidma_write[] = {
    &axidma_writel,
    &axidma_writel,
    &axidma_writel,
};

static int xilinx_axidma_init(SysBusDevice *dev)
{
    struct XilinxAXIDMA *s = FROM_SYSBUS(typeof(*s), dev);
    int axidma_regs;
    int i;

    sysbus_init_irq(dev, &s->streams[1].irq);
    sysbus_init_irq(dev, &s->streams[0].irq);

    if (!s->dmach) {
        hw_error("Unconnected DMA channel.\n");
    }

    xlx_dma_connect_dma(s->dmach, s, axidma_push);

    axidma_regs = cpu_register_io_memory(axidma_read, axidma_write, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4 * 2, axidma_regs);

    for (i = 0; i < 2; i++) {
        stream_reset(&s->streams[i]);
        s->streams[i].nr = i;
        s->streams[i].bh = qemu_bh_new(timer_hit, &s->streams[i]);
        s->streams[i].ptimer = ptimer_init(s->streams[i].bh);
        ptimer_set_freq(s->streams[i].ptimer, s->freqhz);
    }
    return 0;
}

static SysBusDeviceInfo axidma_info = {
    .init = xilinx_axidma_init,
    .qdev.name  = "xilinx,axidma",
    .qdev.size  = sizeof(struct XilinxAXIDMA),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("freqhz", struct XilinxAXIDMA, freqhz, 50000000),
        DEFINE_PROP_PTR("dmach", struct XilinxAXIDMA, dmach),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void xilinx_axidma_register(void)
{
    sysbus_register_withprop(&axidma_info);
}

device_init(xilinx_axidma_register)
