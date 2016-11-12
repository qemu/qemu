/*
 * QEMU PowerMac Awacs Screamer device support
 *
 * Copyright (c) 2016 Mark Cave-Ayland
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
#include "hw/hw.h"
#include "hw/ppc/mac.h"
#include "hw/ppc/mac_dbdma.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

/* debug screamer */
#define DEBUG_SCREAMER

#ifdef DEBUG_SCREAMER
#define SCREAMER_DPRINTF(fmt, ...)                                  \
    do { printf("SCREAMER: " fmt , ## __VA_ARGS__); } while (0)
#else
#define SCREAMER_DPRINTF(fmt, ...)
#endif

/* chip registers */
#define SND_CTRL_REG   0x0
#define CODEC_CTRL_REG 0x1
#define CODEC_STAT_REG 0x2
#define CLIP_CNT_REG   0x3
#define BYTE_SWAP_REG  0x4

#define CODEC_CTRL_MASKECMD     (0x1 << 24)
#define CODEC_STAT_MASK_VALID   (0x1 << 22) 

static void pmac_screamer_tx(DBDMA_io *io)
{
    SCREAMER_DPRINTF("DMA tx!\n");
}

static void pmac_screamer_rx(DBDMA_io *io)
{
    SCREAMER_DPRINTF("DMA rx!\n");
}

static void pmac_screamer_flush(DBDMA_io *io)
{
    SCREAMER_DPRINTF("DMA flush!\n");
}

void macio_screamer_register_dma(ScreamerState *s, void *dbdma, int txchannel, int rxchannel)
{
    s->dbdma = dbdma;
    DBDMA_register_channel(dbdma, txchannel, s->dma_tx_irq,
                           pmac_screamer_tx, pmac_screamer_flush, s);
    DBDMA_register_channel(dbdma, rxchannel, s->dma_rx_irq,
                           pmac_screamer_rx, pmac_screamer_flush, s);
}

static void screamer_reset(DeviceState *dev)
{
    ScreamerState *s = SCREAMER(dev);
    int i = 0;

    for (i = 0; i < 6; i++) {
        s->regs[i] = 0;
    }
    
    for (i = 0; i < 7; i++) {
        s->codec_ctrl_regs[i] = 0;
    }

    return;
}

static void screamer_realizefn(DeviceState *dev, Error **errp)
{
    return;
}

static void screamer_codec_write(ScreamerState *s, hwaddr addr,
                           uint64_t val)
{
    SCREAMER_DPRINTF("%s: addr " TARGET_FMT_plx " val %" PRIx64 "\n", __func__, addr, val);

    s->codec_ctrl_regs[addr] = val;
}

static uint64_t screamer_read(void *opaque, hwaddr addr, unsigned size)
{
    ScreamerState *s = opaque;
    uint32_t val;

    addr = addr >> 4;
    switch (addr) {
    case SND_CTRL_REG:
        val = s->regs[addr];
        break;
    case CODEC_CTRL_REG:
        val = ~CODEC_CTRL_MASKECMD;
        break;
    case CODEC_STAT_REG:
        val = CODEC_STAT_MASK_VALID;
        break;
    case CLIP_CNT_REG:
    case BYTE_SWAP_REG:
        val = s->regs[addr];
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                  "screamer: Unimplemented register read "
                  "reg 0x%" HWADDR_PRIx " size 0x%x\n",
                  addr, size);
        val = 0;
        break;
    }
    
    SCREAMER_DPRINTF("%s: addr " TARGET_FMT_plx " -> %x\n", __func__, addr, val);

    return val;
}

static void screamer_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    ScreamerState *s = opaque;
    uint32_t codec_addr;

    SCREAMER_DPRINTF("%s: addr " TARGET_FMT_plx " val %" PRIx64 "\n", __func__, addr, val);

    addr = addr >> 4;
    switch (addr) {
    case SND_CTRL_REG:
        s->regs[addr] = val & 0xffffffff;
        break;
    case CODEC_CTRL_REG:
        codec_addr = (val & 0x7fff) >> 12;
        screamer_codec_write(s, codec_addr, val & 0xfff);
        break;
    case CODEC_STAT_REG:
    case CLIP_CNT_REG:
    case BYTE_SWAP_REG:
        s->regs[addr] = val & 0xffffffff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                  "screamer: Unimplemented register write "
                  "reg 0x%" HWADDR_PRIx " size 0x%x value 0x%" PRIx64 "\n",
                  addr, size, val);
        break;
    }

    return;
}

static const MemoryRegionOps screamer_ops = {
    .read = screamer_read,
    .write = screamer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void screamer_initfn(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    ScreamerState *s = SCREAMER(obj);

    memory_region_init_io(&s->mem, obj, &screamer_ops, s, "screamer", 0x1000);
    sysbus_init_mmio(d, &s->mem);
    sysbus_init_irq(d, &s->irq);
}

static Property screamer_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static void screamer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = screamer_realizefn;
    dc->reset = screamer_reset;
    dc->props = screamer_properties;
}

static const TypeInfo screamer_type_info = {
    .name = TYPE_SCREAMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ScreamerState),
    .instance_init = screamer_initfn,
    .class_init = screamer_class_init,
};

static void screamer_register_types(void)
{
    type_register_static(&screamer_type_info);
}

type_init(screamer_register_types)
