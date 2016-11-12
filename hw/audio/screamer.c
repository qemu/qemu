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
#include "audio/audio.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/ppc/mac.h"
#include "hw/ppc/mac_dbdma.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/typedefs.h"

/* debug screamer */
//#define DEBUG_SCREAMER

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

/* Audio */
#define SCREAMER_SAMPLE_RATE 44100
static const char *s_spk = "screamer";

static void pmac_transfer(DBDMA_io *io)
{
    ScreamerState *s = io->opaque;     
    
    printf("DMA transfer: addr %" HWADDR_PRIx " len: %x\n", io->addr, io->len);
       
    dma_memory_read(&address_space_memory, io->addr, &s->buf[s->bpos], io->len);
    
    /* Indicate success */
    s->bpos += io->len;
    io->len = 0;
    
    /* Finish */
    qemu_irq_raise(s->irq);    
    io->dma_end(io);
}

static void pmac_screamer_tx(DBDMA_io *io)
{
    ScreamerState *s = io->opaque;
    
    printf("TX yeah!\n");
    
    if (s->bpos + io->len > SCREAMER_BUFFER_SIZE) {
        /* Not enough space in the buffer, so defer IRQ */
        memcpy(&s->io, io, sizeof(DBDMA_io));

	printf("====== DEFER!\n");
	
        return;
    }
    
    s->io.addr = 0;
    s->io.len = 0;
    
    pmac_transfer(io);
}

static void pmac_screamer_flush(DBDMA_io *io)
{
    SCREAMER_DPRINTF("DMA flush!\n");
}

void macio_screamer_register_dma(ScreamerState *s, void *dbdma, int txchannel)
{
    s->dbdma = dbdma;
    DBDMA_register_channel(dbdma, txchannel, s->dma_tx_irq,
                           pmac_screamer_tx, pmac_screamer_flush, s);
}

static void screamerspk_callback(void *opaque, int avail)
{
    ScreamerState *s = opaque;
    int n, len;
    
    SCREAMER_DPRINTF("speaker callback! %d\n", avail);

    if (s->bpos) {
        if (s->ppos < s->bpos) {
	    n = MIN(s->bpos - s->ppos, (unsigned int)avail);
	    printf("########### SPEAKER WRITE! %d / %d - %d\n", s->ppos, s->bpos, n);
            len = AUD_write(s->voice, &s->buf[s->ppos], n);
            s->ppos += len;
	    return;
        }
    }
    
    if (s->io.len) {
        /* Deferred IRQ */
        s->bpos = 0;
        s->ppos = 0;
        pmac_transfer(&s->io);
    }
}

static void screamer_reset(DeviceState *dev)
{
    ScreamerState *s = SCREAMER(dev);
    
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->codec_ctrl_regs, 0, sizeof(s->codec_ctrl_regs));

    s->bpos = 0;
    s->ppos = 0;

    return;
}

static void screamer_realizefn(DeviceState *dev, Error **errp)
{
    struct audsettings as = {SCREAMER_SAMPLE_RATE, 2, AUDIO_FORMAT_S16, 0};
    ScreamerState *s = SCREAMER(dev);

    AUD_register_card(s_spk, &s->card);
 
    s->voice = AUD_open_out(&s->card, s->voice, s_spk, s, screamerspk_callback, &as);
    if (!s->voice) {
        AUD_log(s_spk, "Could not open voice\n");
        return;
    }
    
    AUD_set_active_out(s->voice, true);
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
        val = s->regs[addr] & ~CODEC_CTRL_MASKECMD;
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

    addr = addr >> 4;

    SCREAMER_DPRINTF("%s: addr " TARGET_FMT_plx " val %" PRIx64 "\n", __func__, addr, val);

    switch (addr) {
    case SND_CTRL_REG:
        s->regs[addr] = val & 0xffffffff;
        break;
    case CODEC_CTRL_REG:
        s->regs[addr] = val & 0xffffffff;
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
    sysbus_init_irq(d, &s->dma_tx_irq);
}

static Property screamer_properties[] = {
    DEFINE_AUDIO_PROPERTIES(ScreamerState, card),
    DEFINE_PROP_END_OF_LIST()
};

static void screamer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = screamer_realizefn;
    dc->reset = screamer_reset;
    device_class_set_props(dc, screamer_properties);
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
