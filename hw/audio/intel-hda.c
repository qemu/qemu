/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * written by Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "hw/audio/audio.h"
#include "intel-hda.h"
#include "intel-hda-defs.h"
#include "sysemu/dma.h"

/* --------------------------------------------------------------------- */
/* hda bus                                                               */

static Property hda_props[] = {
    DEFINE_PROP_UINT32("cad", HDACodecDevice, cad, -1),
    DEFINE_PROP_END_OF_LIST()
};

static const TypeInfo hda_codec_bus_info = {
    .name = TYPE_HDA_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(HDACodecBus),
};

void hda_codec_bus_init(DeviceState *dev, HDACodecBus *bus, size_t bus_size,
                        hda_codec_response_func response,
                        hda_codec_xfer_func xfer)
{
    qbus_create_inplace(bus, bus_size, TYPE_HDA_BUS, dev, NULL);
    bus->response = response;
    bus->xfer = xfer;
}

static int hda_codec_dev_init(DeviceState *qdev)
{
    HDACodecBus *bus = DO_UPCAST(HDACodecBus, qbus, qdev->parent_bus);
    HDACodecDevice *dev = DO_UPCAST(HDACodecDevice, qdev, qdev);
    HDACodecDeviceClass *cdc = HDA_CODEC_DEVICE_GET_CLASS(dev);

    if (dev->cad == -1) {
        dev->cad = bus->next_cad;
    }
    if (dev->cad >= 15) {
        return -1;
    }
    bus->next_cad = dev->cad + 1;
    return cdc->init(dev);
}

static int hda_codec_dev_exit(DeviceState *qdev)
{
    HDACodecDevice *dev = DO_UPCAST(HDACodecDevice, qdev, qdev);
    HDACodecDeviceClass *cdc = HDA_CODEC_DEVICE_GET_CLASS(dev);

    if (cdc->exit) {
        cdc->exit(dev);
    }
    return 0;
}

HDACodecDevice *hda_codec_find(HDACodecBus *bus, uint32_t cad)
{
    BusChild *kid;
    HDACodecDevice *cdev;

    QTAILQ_FOREACH(kid, &bus->qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        cdev = DO_UPCAST(HDACodecDevice, qdev, qdev);
        if (cdev->cad == cad) {
            return cdev;
        }
    }
    return NULL;
}

void hda_codec_response(HDACodecDevice *dev, bool solicited, uint32_t response)
{
    HDACodecBus *bus = DO_UPCAST(HDACodecBus, qbus, dev->qdev.parent_bus);
    bus->response(dev, solicited, response);
}

bool hda_codec_xfer(HDACodecDevice *dev, uint32_t stnr, bool output,
                    uint8_t *buf, uint32_t len)
{
    HDACodecBus *bus = DO_UPCAST(HDACodecBus, qbus, dev->qdev.parent_bus);
    return bus->xfer(dev, stnr, output, buf, len);
}

/* --------------------------------------------------------------------- */
/* intel hda emulation                                                   */

typedef struct IntelHDAStream IntelHDAStream;
typedef struct IntelHDAState IntelHDAState;
typedef struct IntelHDAReg IntelHDAReg;

typedef struct bpl {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
} bpl;

struct IntelHDAStream {
    /* registers */
    uint32_t ctl;
    uint32_t lpib;
    uint32_t cbl;
    uint32_t lvi;
    uint32_t fmt;
    uint32_t bdlp_lbase;
    uint32_t bdlp_ubase;

    /* state */
    bpl      *bpl;
    uint32_t bentries;
    uint32_t bsize, be, bp;
};

struct IntelHDAState {
    PCIDevice pci;
    const char *name;
    HDACodecBus codecs;

    /* registers */
    uint32_t g_ctl;
    uint32_t wake_en;
    uint32_t state_sts;
    uint32_t int_ctl;
    uint32_t int_sts;
    uint32_t wall_clk;

    uint32_t corb_lbase;
    uint32_t corb_ubase;
    uint32_t corb_rp;
    uint32_t corb_wp;
    uint32_t corb_ctl;
    uint32_t corb_sts;
    uint32_t corb_size;

    uint32_t rirb_lbase;
    uint32_t rirb_ubase;
    uint32_t rirb_wp;
    uint32_t rirb_cnt;
    uint32_t rirb_ctl;
    uint32_t rirb_sts;
    uint32_t rirb_size;

    uint32_t dp_lbase;
    uint32_t dp_ubase;

    uint32_t icw;
    uint32_t irr;
    uint32_t ics;

    /* streams */
    IntelHDAStream st[8];

    /* state */
    MemoryRegion mmio;
    uint32_t rirb_count;
    int64_t wall_base_ns;

    /* debug logging */
    const IntelHDAReg *last_reg;
    uint32_t last_val;
    uint32_t last_write;
    uint32_t last_sec;
    uint32_t repeat_count;

    /* properties */
    uint32_t debug;
    uint32_t msi;
};

#define TYPE_INTEL_HDA_GENERIC "intel-hda-generic"

#define INTEL_HDA(obj) \
    OBJECT_CHECK(IntelHDAState, (obj), TYPE_INTEL_HDA_GENERIC)

struct IntelHDAReg {
    const char *name;      /* register name */
    uint32_t   size;       /* size in bytes */
    uint32_t   reset;      /* reset value */
    uint32_t   wmask;      /* write mask */
    uint32_t   wclear;     /* write 1 to clear bits */
    uint32_t   offset;     /* location in IntelHDAState */
    uint32_t   shift;      /* byte access entries for dwords */
    uint32_t   stream;
    void       (*whandler)(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old);
    void       (*rhandler)(IntelHDAState *d, const IntelHDAReg *reg);
};

static void intel_hda_reset(DeviceState *dev);

/* --------------------------------------------------------------------- */

static hwaddr intel_hda_addr(uint32_t lbase, uint32_t ubase)
{
    hwaddr addr;

    addr = ((uint64_t)ubase << 32) | lbase;
    return addr;
}

static void intel_hda_update_int_sts(IntelHDAState *d)
{
    uint32_t sts = 0;
    uint32_t i;

    /* update controller status */
    if (d->rirb_sts & ICH6_RBSTS_IRQ) {
        sts |= (1 << 30);
    }
    if (d->rirb_sts & ICH6_RBSTS_OVERRUN) {
        sts |= (1 << 30);
    }
    if (d->state_sts & d->wake_en) {
        sts |= (1 << 30);
    }

    /* update stream status */
    for (i = 0; i < 8; i++) {
        /* buffer completion interrupt */
        if (d->st[i].ctl & (1 << 26)) {
            sts |= (1 << i);
        }
    }

    /* update global status */
    if (sts & d->int_ctl) {
        sts |= (1U << 31);
    }

    d->int_sts = sts;
}

static void intel_hda_update_irq(IntelHDAState *d)
{
    int msi = d->msi && msi_enabled(&d->pci);
    int level;

    intel_hda_update_int_sts(d);
    if (d->int_sts & (1U << 31) && d->int_ctl & (1U << 31)) {
        level = 1;
    } else {
        level = 0;
    }
    dprint(d, 2, "%s: level %d [%s]\n", __FUNCTION__,
           level, msi ? "msi" : "intx");
    if (msi) {
        if (level) {
            msi_notify(&d->pci, 0);
        }
    } else {
        pci_set_irq(&d->pci, level);
    }
}

static int intel_hda_send_command(IntelHDAState *d, uint32_t verb)
{
    uint32_t cad, nid, data;
    HDACodecDevice *codec;
    HDACodecDeviceClass *cdc;

    cad = (verb >> 28) & 0x0f;
    if (verb & (1 << 27)) {
        /* indirect node addressing, not specified in HDA 1.0 */
        dprint(d, 1, "%s: indirect node addressing (guest bug?)\n", __FUNCTION__);
        return -1;
    }
    nid = (verb >> 20) & 0x7f;
    data = verb & 0xfffff;

    codec = hda_codec_find(&d->codecs, cad);
    if (codec == NULL) {
        dprint(d, 1, "%s: addressed non-existing codec\n", __FUNCTION__);
        return -1;
    }
    cdc = HDA_CODEC_DEVICE_GET_CLASS(codec);
    cdc->command(codec, nid, data);
    return 0;
}

static void intel_hda_corb_run(IntelHDAState *d)
{
    hwaddr addr;
    uint32_t rp, verb;

    if (d->ics & ICH6_IRS_BUSY) {
        dprint(d, 2, "%s: [icw] verb 0x%08x\n", __FUNCTION__, d->icw);
        intel_hda_send_command(d, d->icw);
        return;
    }

    for (;;) {
        if (!(d->corb_ctl & ICH6_CORBCTL_RUN)) {
            dprint(d, 2, "%s: !run\n", __FUNCTION__);
            return;
        }
        if ((d->corb_rp & 0xff) == d->corb_wp) {
            dprint(d, 2, "%s: corb ring empty\n", __FUNCTION__);
            return;
        }
        if (d->rirb_count == d->rirb_cnt) {
            dprint(d, 2, "%s: rirb count reached\n", __FUNCTION__);
            return;
        }

        rp = (d->corb_rp + 1) & 0xff;
        addr = intel_hda_addr(d->corb_lbase, d->corb_ubase);
        verb = ldl_le_pci_dma(&d->pci, addr + 4*rp);
        d->corb_rp = rp;

        dprint(d, 2, "%s: [rp 0x%x] verb 0x%08x\n", __FUNCTION__, rp, verb);
        intel_hda_send_command(d, verb);
    }
}

static void intel_hda_response(HDACodecDevice *dev, bool solicited, uint32_t response)
{
    HDACodecBus *bus = DO_UPCAST(HDACodecBus, qbus, dev->qdev.parent_bus);
    IntelHDAState *d = container_of(bus, IntelHDAState, codecs);
    hwaddr addr;
    uint32_t wp, ex;

    if (d->ics & ICH6_IRS_BUSY) {
        dprint(d, 2, "%s: [irr] response 0x%x, cad 0x%x\n",
               __FUNCTION__, response, dev->cad);
        d->irr = response;
        d->ics &= ~(ICH6_IRS_BUSY | 0xf0);
        d->ics |= (ICH6_IRS_VALID | (dev->cad << 4));
        return;
    }

    if (!(d->rirb_ctl & ICH6_RBCTL_DMA_EN)) {
        dprint(d, 1, "%s: rirb dma disabled, drop codec response\n", __FUNCTION__);
        return;
    }

    ex = (solicited ? 0 : (1 << 4)) | dev->cad;
    wp = (d->rirb_wp + 1) & 0xff;
    addr = intel_hda_addr(d->rirb_lbase, d->rirb_ubase);
    stl_le_pci_dma(&d->pci, addr + 8*wp, response);
    stl_le_pci_dma(&d->pci, addr + 8*wp + 4, ex);
    d->rirb_wp = wp;

    dprint(d, 2, "%s: [wp 0x%x] response 0x%x, extra 0x%x\n",
           __FUNCTION__, wp, response, ex);

    d->rirb_count++;
    if (d->rirb_count == d->rirb_cnt) {
        dprint(d, 2, "%s: rirb count reached (%d)\n", __FUNCTION__, d->rirb_count);
        if (d->rirb_ctl & ICH6_RBCTL_IRQ_EN) {
            d->rirb_sts |= ICH6_RBSTS_IRQ;
            intel_hda_update_irq(d);
        }
    } else if ((d->corb_rp & 0xff) == d->corb_wp) {
        dprint(d, 2, "%s: corb ring empty (%d/%d)\n", __FUNCTION__,
               d->rirb_count, d->rirb_cnt);
        if (d->rirb_ctl & ICH6_RBCTL_IRQ_EN) {
            d->rirb_sts |= ICH6_RBSTS_IRQ;
            intel_hda_update_irq(d);
        }
    }
}

static bool intel_hda_xfer(HDACodecDevice *dev, uint32_t stnr, bool output,
                           uint8_t *buf, uint32_t len)
{
    HDACodecBus *bus = DO_UPCAST(HDACodecBus, qbus, dev->qdev.parent_bus);
    IntelHDAState *d = container_of(bus, IntelHDAState, codecs);
    hwaddr addr;
    uint32_t s, copy, left;
    IntelHDAStream *st;
    bool irq = false;

    st = output ? d->st + 4 : d->st;
    for (s = 0; s < 4; s++) {
        if (stnr == ((st[s].ctl >> 20) & 0x0f)) {
            st = st + s;
            break;
        }
    }
    if (s == 4) {
        return false;
    }
    if (st->bpl == NULL) {
        return false;
    }
    if (st->ctl & (1 << 26)) {
        /*
         * Wait with the next DMA xfer until the guest
         * has acked the buffer completion interrupt
         */
        return false;
    }

    left = len;
    while (left > 0) {
        copy = left;
        if (copy > st->bsize - st->lpib)
            copy = st->bsize - st->lpib;
        if (copy > st->bpl[st->be].len - st->bp)
            copy = st->bpl[st->be].len - st->bp;

        dprint(d, 3, "dma: entry %d, pos %d/%d, copy %d\n",
               st->be, st->bp, st->bpl[st->be].len, copy);

        pci_dma_rw(&d->pci, st->bpl[st->be].addr + st->bp, buf, copy, !output);
        st->lpib += copy;
        st->bp += copy;
        buf += copy;
        left -= copy;

        if (st->bpl[st->be].len == st->bp) {
            /* bpl entry filled */
            if (st->bpl[st->be].flags & 0x01) {
                irq = true;
            }
            st->bp = 0;
            st->be++;
            if (st->be == st->bentries) {
                /* bpl wrap around */
                st->be = 0;
                st->lpib = 0;
            }
        }
    }
    if (d->dp_lbase & 0x01) {
        s = st - d->st;
        addr = intel_hda_addr(d->dp_lbase & ~0x01, d->dp_ubase);
        stl_le_pci_dma(&d->pci, addr + 8*s, st->lpib);
    }
    dprint(d, 3, "dma: --\n");

    if (irq) {
        st->ctl |= (1 << 26); /* buffer completion interrupt */
        intel_hda_update_irq(d);
    }
    return true;
}

static void intel_hda_parse_bdl(IntelHDAState *d, IntelHDAStream *st)
{
    hwaddr addr;
    uint8_t buf[16];
    uint32_t i;

    addr = intel_hda_addr(st->bdlp_lbase, st->bdlp_ubase);
    st->bentries = st->lvi +1;
    g_free(st->bpl);
    st->bpl = g_malloc(sizeof(bpl) * st->bentries);
    for (i = 0; i < st->bentries; i++, addr += 16) {
        pci_dma_read(&d->pci, addr, buf, 16);
        st->bpl[i].addr  = le64_to_cpu(*(uint64_t *)buf);
        st->bpl[i].len   = le32_to_cpu(*(uint32_t *)(buf + 8));
        st->bpl[i].flags = le32_to_cpu(*(uint32_t *)(buf + 12));
        dprint(d, 1, "bdl/%d: 0x%" PRIx64 " +0x%x, 0x%x\n",
               i, st->bpl[i].addr, st->bpl[i].len, st->bpl[i].flags);
    }

    st->bsize = st->cbl;
    st->lpib  = 0;
    st->be    = 0;
    st->bp    = 0;
}

static void intel_hda_notify_codecs(IntelHDAState *d, uint32_t stream, bool running, bool output)
{
    BusChild *kid;
    HDACodecDevice *cdev;

    QTAILQ_FOREACH(kid, &d->codecs.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        HDACodecDeviceClass *cdc;

        cdev = DO_UPCAST(HDACodecDevice, qdev, qdev);
        cdc = HDA_CODEC_DEVICE_GET_CLASS(cdev);
        if (cdc->stream) {
            cdc->stream(cdev, stream, running, output);
        }
    }
}

/* --------------------------------------------------------------------- */

static void intel_hda_set_g_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    if ((d->g_ctl & ICH6_GCTL_RESET) == 0) {
        intel_hda_reset(DEVICE(d));
    }
}

static void intel_hda_set_wake_en(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);
}

static void intel_hda_set_state_sts(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);
}

static void intel_hda_set_int_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);
}

static void intel_hda_get_wall_clk(IntelHDAState *d, const IntelHDAReg *reg)
{
    int64_t ns;

    ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - d->wall_base_ns;
    d->wall_clk = (uint32_t)(ns * 24 / 1000);  /* 24 MHz */
}

static void intel_hda_set_corb_wp(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_corb_run(d);
}

static void intel_hda_set_corb_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_corb_run(d);
}

static void intel_hda_set_rirb_wp(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    if (d->rirb_wp & ICH6_RIRBWP_RST) {
        d->rirb_wp = 0;
    }
}

static void intel_hda_set_rirb_sts(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    intel_hda_update_irq(d);

    if ((old & ICH6_RBSTS_IRQ) && !(d->rirb_sts & ICH6_RBSTS_IRQ)) {
        /* cleared ICH6_RBSTS_IRQ */
        d->rirb_count = 0;
        intel_hda_corb_run(d);
    }
}

static void intel_hda_set_ics(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    if (d->ics & ICH6_IRS_BUSY) {
        intel_hda_corb_run(d);
    }
}

static void intel_hda_set_st_ctl(IntelHDAState *d, const IntelHDAReg *reg, uint32_t old)
{
    bool output = reg->stream >= 4;
    IntelHDAStream *st = d->st + reg->stream;

    if (st->ctl & 0x01) {
        /* reset */
        dprint(d, 1, "st #%d: reset\n", reg->stream);
        st->ctl = SD_STS_FIFO_READY << 24;
    }
    if ((st->ctl & 0x02) != (old & 0x02)) {
        uint32_t stnr = (st->ctl >> 20) & 0x0f;
        /* run bit flipped */
        if (st->ctl & 0x02) {
            /* start */
            dprint(d, 1, "st #%d: start %d (ring buf %d bytes)\n",
                   reg->stream, stnr, st->cbl);
            intel_hda_parse_bdl(d, st);
            intel_hda_notify_codecs(d, stnr, true, output);
        } else {
            /* stop */
            dprint(d, 1, "st #%d: stop %d\n", reg->stream, stnr);
            intel_hda_notify_codecs(d, stnr, false, output);
        }
    }
    intel_hda_update_irq(d);
}

/* --------------------------------------------------------------------- */

#define ST_REG(_n, _o) (0x80 + (_n) * 0x20 + (_o))

static const struct IntelHDAReg regtab[] = {
    /* global */
    [ ICH6_REG_GCAP ] = {
        .name     = "GCAP",
        .size     = 2,
        .reset    = 0x4401,
    },
    [ ICH6_REG_VMIN ] = {
        .name     = "VMIN",
        .size     = 1,
    },
    [ ICH6_REG_VMAJ ] = {
        .name     = "VMAJ",
        .size     = 1,
        .reset    = 1,
    },
    [ ICH6_REG_OUTPAY ] = {
        .name     = "OUTPAY",
        .size     = 2,
        .reset    = 0x3c,
    },
    [ ICH6_REG_INPAY ] = {
        .name     = "INPAY",
        .size     = 2,
        .reset    = 0x1d,
    },
    [ ICH6_REG_GCTL ] = {
        .name     = "GCTL",
        .size     = 4,
        .wmask    = 0x0103,
        .offset   = offsetof(IntelHDAState, g_ctl),
        .whandler = intel_hda_set_g_ctl,
    },
    [ ICH6_REG_WAKEEN ] = {
        .name     = "WAKEEN",
        .size     = 2,
        .wmask    = 0x7fff,
        .offset   = offsetof(IntelHDAState, wake_en),
        .whandler = intel_hda_set_wake_en,
    },
    [ ICH6_REG_STATESTS ] = {
        .name     = "STATESTS",
        .size     = 2,
        .wmask    = 0x7fff,
        .wclear   = 0x7fff,
        .offset   = offsetof(IntelHDAState, state_sts),
        .whandler = intel_hda_set_state_sts,
    },

    /* interrupts */
    [ ICH6_REG_INTCTL ] = {
        .name     = "INTCTL",
        .size     = 4,
        .wmask    = 0xc00000ff,
        .offset   = offsetof(IntelHDAState, int_ctl),
        .whandler = intel_hda_set_int_ctl,
    },
    [ ICH6_REG_INTSTS ] = {
        .name     = "INTSTS",
        .size     = 4,
        .wmask    = 0xc00000ff,
        .wclear   = 0xc00000ff,
        .offset   = offsetof(IntelHDAState, int_sts),
    },

    /* misc */
    [ ICH6_REG_WALLCLK ] = {
        .name     = "WALLCLK",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, wall_clk),
        .rhandler = intel_hda_get_wall_clk,
    },
    [ ICH6_REG_WALLCLK + 0x2000 ] = {
        .name     = "WALLCLK(alias)",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, wall_clk),
        .rhandler = intel_hda_get_wall_clk,
    },

    /* dma engine */
    [ ICH6_REG_CORBLBASE ] = {
        .name     = "CORBLBASE",
        .size     = 4,
        .wmask    = 0xffffff80,
        .offset   = offsetof(IntelHDAState, corb_lbase),
    },
    [ ICH6_REG_CORBUBASE ] = {
        .name     = "CORBUBASE",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, corb_ubase),
    },
    [ ICH6_REG_CORBWP ] = {
        .name     = "CORBWP",
        .size     = 2,
        .wmask    = 0xff,
        .offset   = offsetof(IntelHDAState, corb_wp),
        .whandler = intel_hda_set_corb_wp,
    },
    [ ICH6_REG_CORBRP ] = {
        .name     = "CORBRP",
        .size     = 2,
        .wmask    = 0x80ff,
        .offset   = offsetof(IntelHDAState, corb_rp),
    },
    [ ICH6_REG_CORBCTL ] = {
        .name     = "CORBCTL",
        .size     = 1,
        .wmask    = 0x03,
        .offset   = offsetof(IntelHDAState, corb_ctl),
        .whandler = intel_hda_set_corb_ctl,
    },
    [ ICH6_REG_CORBSTS ] = {
        .name     = "CORBSTS",
        .size     = 1,
        .wmask    = 0x01,
        .wclear   = 0x01,
        .offset   = offsetof(IntelHDAState, corb_sts),
    },
    [ ICH6_REG_CORBSIZE ] = {
        .name     = "CORBSIZE",
        .size     = 1,
        .reset    = 0x42,
        .offset   = offsetof(IntelHDAState, corb_size),
    },
    [ ICH6_REG_RIRBLBASE ] = {
        .name     = "RIRBLBASE",
        .size     = 4,
        .wmask    = 0xffffff80,
        .offset   = offsetof(IntelHDAState, rirb_lbase),
    },
    [ ICH6_REG_RIRBUBASE ] = {
        .name     = "RIRBUBASE",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, rirb_ubase),
    },
    [ ICH6_REG_RIRBWP ] = {
        .name     = "RIRBWP",
        .size     = 2,
        .wmask    = 0x8000,
        .offset   = offsetof(IntelHDAState, rirb_wp),
        .whandler = intel_hda_set_rirb_wp,
    },
    [ ICH6_REG_RINTCNT ] = {
        .name     = "RINTCNT",
        .size     = 2,
        .wmask    = 0xff,
        .offset   = offsetof(IntelHDAState, rirb_cnt),
    },
    [ ICH6_REG_RIRBCTL ] = {
        .name     = "RIRBCTL",
        .size     = 1,
        .wmask    = 0x07,
        .offset   = offsetof(IntelHDAState, rirb_ctl),
    },
    [ ICH6_REG_RIRBSTS ] = {
        .name     = "RIRBSTS",
        .size     = 1,
        .wmask    = 0x05,
        .wclear   = 0x05,
        .offset   = offsetof(IntelHDAState, rirb_sts),
        .whandler = intel_hda_set_rirb_sts,
    },
    [ ICH6_REG_RIRBSIZE ] = {
        .name     = "RIRBSIZE",
        .size     = 1,
        .reset    = 0x42,
        .offset   = offsetof(IntelHDAState, rirb_size),
    },

    [ ICH6_REG_DPLBASE ] = {
        .name     = "DPLBASE",
        .size     = 4,
        .wmask    = 0xffffff81,
        .offset   = offsetof(IntelHDAState, dp_lbase),
    },
    [ ICH6_REG_DPUBASE ] = {
        .name     = "DPUBASE",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, dp_ubase),
    },

    [ ICH6_REG_IC ] = {
        .name     = "ICW",
        .size     = 4,
        .wmask    = 0xffffffff,
        .offset   = offsetof(IntelHDAState, icw),
    },
    [ ICH6_REG_IR ] = {
        .name     = "IRR",
        .size     = 4,
        .offset   = offsetof(IntelHDAState, irr),
    },
    [ ICH6_REG_IRS ] = {
        .name     = "ICS",
        .size     = 2,
        .wmask    = 0x0003,
        .wclear   = 0x0002,
        .offset   = offsetof(IntelHDAState, ics),
        .whandler = intel_hda_set_ics,
    },

#define HDA_STREAM(_t, _i)                                            \
    [ ST_REG(_i, ICH6_REG_SD_CTL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL",                          \
        .size     = 4,                                                \
        .wmask    = 0x1cff001f,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_CTL) + 2] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL(stnr)",                    \
        .size     = 1,                                                \
        .shift    = 16,                                               \
        .wmask    = 0x00ff0000,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_STS)] = {                                \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CTL(sts)",                     \
        .size     = 1,                                                \
        .shift    = 24,                                               \
        .wmask    = 0x1c000000,                                       \
        .wclear   = 0x1c000000,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].ctl),              \
        .whandler = intel_hda_set_st_ctl,                             \
        .reset    = SD_STS_FIFO_READY << 24                           \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_LPIB) ] = {                              \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LPIB",                         \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, st[_i].lpib),             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_LPIB) + 0x2000 ] = {                     \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LPIB(alias)",                  \
        .size     = 4,                                                \
        .offset   = offsetof(IntelHDAState, st[_i].lpib),             \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_CBL) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " CBL",                          \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].cbl),              \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_LVI) ] = {                               \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " LVI",                          \
        .size     = 2,                                                \
        .wmask    = 0x00ff,                                           \
        .offset   = offsetof(IntelHDAState, st[_i].lvi),              \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_FIFOSIZE) ] = {                          \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " FIFOS",                        \
        .size     = 2,                                                \
        .reset    = HDA_BUFFER_SIZE,                                  \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_FORMAT) ] = {                            \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " FMT",                          \
        .size     = 2,                                                \
        .wmask    = 0x7f7f,                                           \
        .offset   = offsetof(IntelHDAState, st[_i].fmt),              \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_BDLPL) ] = {                             \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " BDLPL",                        \
        .size     = 4,                                                \
        .wmask    = 0xffffff80,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].bdlp_lbase),       \
    },                                                                \
    [ ST_REG(_i, ICH6_REG_SD_BDLPU) ] = {                             \
        .stream   = _i,                                               \
        .name     = _t stringify(_i) " BDLPU",                        \
        .size     = 4,                                                \
        .wmask    = 0xffffffff,                                       \
        .offset   = offsetof(IntelHDAState, st[_i].bdlp_ubase),       \
    },                                                                \

    HDA_STREAM("IN", 0)
    HDA_STREAM("IN", 1)
    HDA_STREAM("IN", 2)
    HDA_STREAM("IN", 3)

    HDA_STREAM("OUT", 4)
    HDA_STREAM("OUT", 5)
    HDA_STREAM("OUT", 6)
    HDA_STREAM("OUT", 7)

};

static const IntelHDAReg *intel_hda_reg_find(IntelHDAState *d, hwaddr addr)
{
    const IntelHDAReg *reg;

    if (addr >= ARRAY_SIZE(regtab)) {
        goto noreg;
    }
    reg = regtab+addr;
    if (reg->name == NULL) {
        goto noreg;
    }
    return reg;

noreg:
    dprint(d, 1, "unknown register, addr 0x%x\n", (int) addr);
    return NULL;
}

static uint32_t *intel_hda_reg_addr(IntelHDAState *d, const IntelHDAReg *reg)
{
    uint8_t *addr = (void*)d;

    addr += reg->offset;
    return (uint32_t*)addr;
}

static void intel_hda_reg_write(IntelHDAState *d, const IntelHDAReg *reg, uint32_t val,
                                uint32_t wmask)
{
    uint32_t *addr;
    uint32_t old;

    if (!reg) {
        return;
    }

    if (d->debug) {
        time_t now = time(NULL);
        if (d->last_write && d->last_reg == reg && d->last_val == val) {
            d->repeat_count++;
            if (d->last_sec != now) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
                d->last_sec = now;
                d->repeat_count = 0;
            }
        } else {
            if (d->repeat_count) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
            }
            dprint(d, 2, "write %-16s: 0x%x (%x)\n", reg->name, val, wmask);
            d->last_write = 1;
            d->last_reg   = reg;
            d->last_val   = val;
            d->last_sec   = now;
            d->repeat_count = 0;
        }
    }
    assert(reg->offset != 0);

    addr = intel_hda_reg_addr(d, reg);
    old = *addr;

    if (reg->shift) {
        val <<= reg->shift;
        wmask <<= reg->shift;
    }
    wmask &= reg->wmask;
    *addr &= ~wmask;
    *addr |= wmask & val;
    *addr &= ~(val & reg->wclear);

    if (reg->whandler) {
        reg->whandler(d, reg, old);
    }
}

static uint32_t intel_hda_reg_read(IntelHDAState *d, const IntelHDAReg *reg,
                                   uint32_t rmask)
{
    uint32_t *addr, ret;

    if (!reg) {
        return 0;
    }

    if (reg->rhandler) {
        reg->rhandler(d, reg);
    }

    if (reg->offset == 0) {
        /* constant read-only register */
        ret = reg->reset;
    } else {
        addr = intel_hda_reg_addr(d, reg);
        ret = *addr;
        if (reg->shift) {
            ret >>= reg->shift;
        }
        ret &= rmask;
    }
    if (d->debug) {
        time_t now = time(NULL);
        if (!d->last_write && d->last_reg == reg && d->last_val == ret) {
            d->repeat_count++;
            if (d->last_sec != now) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
                d->last_sec = now;
                d->repeat_count = 0;
            }
        } else {
            if (d->repeat_count) {
                dprint(d, 2, "previous register op repeated %d times\n", d->repeat_count);
            }
            dprint(d, 2, "read  %-16s: 0x%x (%x)\n", reg->name, ret, rmask);
            d->last_write = 0;
            d->last_reg   = reg;
            d->last_val   = ret;
            d->last_sec   = now;
            d->repeat_count = 0;
        }
    }
    return ret;
}

static void intel_hda_regs_reset(IntelHDAState *d)
{
    uint32_t *addr;
    int i;

    for (i = 0; i < ARRAY_SIZE(regtab); i++) {
        if (regtab[i].name == NULL) {
            continue;
        }
        if (regtab[i].offset == 0) {
            continue;
        }
        addr = intel_hda_reg_addr(d, regtab + i);
        *addr = regtab[i].reset;
    }
}

/* --------------------------------------------------------------------- */

static void intel_hda_mmio_writeb(void *opaque, hwaddr addr, uint32_t val)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    intel_hda_reg_write(d, reg, val, 0xff);
}

static void intel_hda_mmio_writew(void *opaque, hwaddr addr, uint32_t val)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    intel_hda_reg_write(d, reg, val, 0xffff);
}

static void intel_hda_mmio_writel(void *opaque, hwaddr addr, uint32_t val)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    intel_hda_reg_write(d, reg, val, 0xffffffff);
}

static uint32_t intel_hda_mmio_readb(void *opaque, hwaddr addr)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    return intel_hda_reg_read(d, reg, 0xff);
}

static uint32_t intel_hda_mmio_readw(void *opaque, hwaddr addr)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    return intel_hda_reg_read(d, reg, 0xffff);
}

static uint32_t intel_hda_mmio_readl(void *opaque, hwaddr addr)
{
    IntelHDAState *d = opaque;
    const IntelHDAReg *reg = intel_hda_reg_find(d, addr);

    return intel_hda_reg_read(d, reg, 0xffffffff);
}

static const MemoryRegionOps intel_hda_mmio_ops = {
    .old_mmio = {
        .read = {
            intel_hda_mmio_readb,
            intel_hda_mmio_readw,
            intel_hda_mmio_readl,
        },
        .write = {
            intel_hda_mmio_writeb,
            intel_hda_mmio_writew,
            intel_hda_mmio_writel,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* --------------------------------------------------------------------- */

static void intel_hda_reset(DeviceState *dev)
{
    BusChild *kid;
    IntelHDAState *d = INTEL_HDA(dev);
    HDACodecDevice *cdev;

    intel_hda_regs_reset(d);
    d->wall_base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* reset codecs */
    QTAILQ_FOREACH(kid, &d->codecs.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        cdev = DO_UPCAST(HDACodecDevice, qdev, qdev);
        device_reset(DEVICE(cdev));
        d->state_sts |= (1 << cdev->cad);
    }
    intel_hda_update_irq(d);
}

static int intel_hda_init(PCIDevice *pci)
{
    IntelHDAState *d = INTEL_HDA(pci);
    uint8_t *conf = d->pci.config;

    d->name = object_get_typename(OBJECT(d));

    pci_config_set_interrupt_pin(conf, 1);

    /* HDCTL off 0x40 bit 0 selects signaling mode (1-HDA, 0 - Ac97) 18.1.19 */
    conf[0x40] = 0x01;

    memory_region_init_io(&d->mmio, OBJECT(d), &intel_hda_mmio_ops, d,
                          "intel-hda", 0x4000);
    pci_register_bar(&d->pci, 0, 0, &d->mmio);
    if (d->msi) {
        msi_init(&d->pci, 0x50, 1, true, false);
    }

    hda_codec_bus_init(DEVICE(pci), &d->codecs, sizeof(d->codecs),
                       intel_hda_response, intel_hda_xfer);

    return 0;
}

static void intel_hda_exit(PCIDevice *pci)
{
    IntelHDAState *d = INTEL_HDA(pci);

    msi_uninit(&d->pci);
    memory_region_destroy(&d->mmio);
}

static int intel_hda_post_load(void *opaque, int version)
{
    IntelHDAState* d = opaque;
    int i;

    dprint(d, 1, "%s\n", __FUNCTION__);
    for (i = 0; i < ARRAY_SIZE(d->st); i++) {
        if (d->st[i].ctl & 0x02) {
            intel_hda_parse_bdl(d, &d->st[i]);
        }
    }
    intel_hda_update_irq(d);
    return 0;
}

static const VMStateDescription vmstate_intel_hda_stream = {
    .name = "intel-hda-stream",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctl, IntelHDAStream),
        VMSTATE_UINT32(lpib, IntelHDAStream),
        VMSTATE_UINT32(cbl, IntelHDAStream),
        VMSTATE_UINT32(lvi, IntelHDAStream),
        VMSTATE_UINT32(fmt, IntelHDAStream),
        VMSTATE_UINT32(bdlp_lbase, IntelHDAStream),
        VMSTATE_UINT32(bdlp_ubase, IntelHDAStream),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_intel_hda = {
    .name = "intel-hda",
    .version_id = 1,
    .post_load = intel_hda_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pci, IntelHDAState),

        /* registers */
        VMSTATE_UINT32(g_ctl, IntelHDAState),
        VMSTATE_UINT32(wake_en, IntelHDAState),
        VMSTATE_UINT32(state_sts, IntelHDAState),
        VMSTATE_UINT32(int_ctl, IntelHDAState),
        VMSTATE_UINT32(int_sts, IntelHDAState),
        VMSTATE_UINT32(wall_clk, IntelHDAState),
        VMSTATE_UINT32(corb_lbase, IntelHDAState),
        VMSTATE_UINT32(corb_ubase, IntelHDAState),
        VMSTATE_UINT32(corb_rp, IntelHDAState),
        VMSTATE_UINT32(corb_wp, IntelHDAState),
        VMSTATE_UINT32(corb_ctl, IntelHDAState),
        VMSTATE_UINT32(corb_sts, IntelHDAState),
        VMSTATE_UINT32(corb_size, IntelHDAState),
        VMSTATE_UINT32(rirb_lbase, IntelHDAState),
        VMSTATE_UINT32(rirb_ubase, IntelHDAState),
        VMSTATE_UINT32(rirb_wp, IntelHDAState),
        VMSTATE_UINT32(rirb_cnt, IntelHDAState),
        VMSTATE_UINT32(rirb_ctl, IntelHDAState),
        VMSTATE_UINT32(rirb_sts, IntelHDAState),
        VMSTATE_UINT32(rirb_size, IntelHDAState),
        VMSTATE_UINT32(dp_lbase, IntelHDAState),
        VMSTATE_UINT32(dp_ubase, IntelHDAState),
        VMSTATE_UINT32(icw, IntelHDAState),
        VMSTATE_UINT32(irr, IntelHDAState),
        VMSTATE_UINT32(ics, IntelHDAState),
        VMSTATE_STRUCT_ARRAY(st, IntelHDAState, 8, 0,
                             vmstate_intel_hda_stream,
                             IntelHDAStream),

        /* additional state info */
        VMSTATE_UINT32(rirb_count, IntelHDAState),
        VMSTATE_INT64(wall_base_ns, IntelHDAState),

        VMSTATE_END_OF_LIST()
    }
};

static Property intel_hda_properties[] = {
    DEFINE_PROP_UINT32("debug", IntelHDAState, debug, 0),
    DEFINE_PROP_UINT32("msi", IntelHDAState, msi, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void intel_hda_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = intel_hda_init;
    k->exit = intel_hda_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->class_id = PCI_CLASS_MULTIMEDIA_HD_AUDIO;
    dc->reset = intel_hda_reset;
    dc->vmsd = &vmstate_intel_hda;
    dc->props = intel_hda_properties;
}

static void intel_hda_class_init_ich6(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x2668;
    k->revision = 1;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (ich6)";
}

static void intel_hda_class_init_ich9(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = 0x293e;
    k->revision = 3;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel HD Audio Controller (ich9)";
}

static const TypeInfo intel_hda_info = {
    .name          = TYPE_INTEL_HDA_GENERIC,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IntelHDAState),
    .class_init    = intel_hda_class_init,
    .abstract      = true,
};

static const TypeInfo intel_hda_info_ich6 = {
    .name          = "intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_ich6,
};

static const TypeInfo intel_hda_info_ich9 = {
    .name          = "ich9-intel-hda",
    .parent        = TYPE_INTEL_HDA_GENERIC,
    .class_init    = intel_hda_class_init_ich9,
};

static void hda_codec_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = hda_codec_dev_init;
    k->exit = hda_codec_dev_exit;
    set_bit(DEVICE_CATEGORY_SOUND, k->categories);
    k->bus_type = TYPE_HDA_BUS;
    k->props = hda_props;
}

static const TypeInfo hda_codec_device_type_info = {
    .name = TYPE_HDA_CODEC_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HDACodecDevice),
    .abstract = true,
    .class_size = sizeof(HDACodecDeviceClass),
    .class_init = hda_codec_device_class_init,
};

/*
 * create intel hda controller with codec attached to it,
 * so '-soundhw hda' works.
 */
static int intel_hda_and_codec_init(PCIBus *bus)
{
    DeviceState *controller;
    BusState *hdabus;
    DeviceState *codec;

    controller = DEVICE(pci_create_simple(bus, -1, "intel-hda"));
    hdabus = QLIST_FIRST(&controller->child_bus);
    codec = qdev_create(hdabus, "hda-duplex");
    qdev_init_nofail(codec);
    return 0;
}

static void intel_hda_register_types(void)
{
    type_register_static(&hda_codec_bus_info);
    type_register_static(&intel_hda_info);
    type_register_static(&intel_hda_info_ich6);
    type_register_static(&intel_hda_info_ich9);
    type_register_static(&hda_codec_device_type_info);
    pci_register_soundhw("hda", "Intel HD Audio", intel_hda_and_codec_init);
}

type_init(intel_hda_register_types)
