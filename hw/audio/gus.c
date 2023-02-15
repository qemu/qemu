/*
 * QEMU Proxy for Gravis Ultrasound GF1 emulation by Tibor "TS" Schütz
 *
 * Copyright (c) 2002-2005 Vassili Karpov (malc)
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
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "gusemu.h"
#include "gustate.h"
#include "qom/object.h"

#define dolog(...) AUD_log ("audio", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#define TYPE_GUS "gus"
OBJECT_DECLARE_SIMPLE_TYPE(GUSState, GUS)

struct GUSState {
    ISADevice dev;
    GUSEmuState emu;
    QEMUSoundCard card;
    uint32_t freq;
    uint32_t port;
    int pos, left, shift, irqs;
    int16_t *mixbuf;
    uint8_t himem[1024 * 1024 + 32 + 4096];
    int samples;
    SWVoiceOut *voice;
    int64_t last_ticks;
    qemu_irq pic;
    IsaDma *isa_dma;
    PortioList portio_list1;
    PortioList portio_list2;
};

static uint32_t gus_readb(void *opaque, uint32_t nport)
{
    GUSState *s = opaque;

    return gus_read (&s->emu, nport, 1);
}

static void gus_writeb(void *opaque, uint32_t nport, uint32_t val)
{
    GUSState *s = opaque;

    gus_write (&s->emu, nport, 1, val);
}

static int write_audio (GUSState *s, int samples)
{
    int net = 0;
    int pos = s->pos;

    while (samples) {
        int nbytes, wbytes, wsampl;

        nbytes = samples << s->shift;
        wbytes = AUD_write (
            s->voice,
            s->mixbuf + (pos << (s->shift - 1)),
            nbytes
            );

        if (wbytes) {
            wsampl = wbytes >> s->shift;

            samples -= wsampl;
            pos = (pos + wsampl) % s->samples;

            net += wsampl;
        }
        else {
            break;
        }
    }

    return net;
}

static void GUS_callback (void *opaque, int free)
{
    int samples, to_play, net = 0;
    GUSState *s = opaque;

    samples = free >> s->shift;
    to_play = MIN (samples, s->left);

    while (to_play) {
        int written = write_audio (s, to_play);

        if (!written) {
            goto reset;
        }

        s->left -= written;
        to_play -= written;
        samples -= written;
        net += written;
    }

    samples = MIN (samples, s->samples);
    if (samples) {
        gus_mixvoices (&s->emu, s->freq, samples, s->mixbuf);

        while (samples) {
            int written = write_audio (s, samples);
            if (!written) {
                break;
            }
            samples -= written;
            net += written;
        }
    }
    s->left = samples;

 reset:
    gus_irqgen (&s->emu, (uint64_t)net * 1000000 / s->freq);
}

int GUS_irqrequest (GUSEmuState *emu, int hwirq, int n)
{
    GUSState *s = emu->opaque;
    /* qemu_irq_lower (s->pic); */
    qemu_irq_raise (s->pic);
    s->irqs += n;
    ldebug ("irqrequest %d %d %d\n", hwirq, n, s->irqs);
    return n;
}

void GUS_irqclear (GUSEmuState *emu, int hwirq)
{
    GUSState *s = emu->opaque;
    ldebug ("irqclear %d %d\n", hwirq, s->irqs);
    qemu_irq_lower (s->pic);
    s->irqs -= 1;
#ifdef IRQ_STORM
    if (s->irqs > 0) {
        qemu_irq_raise (s->pic[hwirq]);
    }
#endif
}

void GUS_dmarequest (GUSEmuState *emu)
{
    GUSState *s = emu->opaque;
    IsaDmaClass *k = ISADMA_GET_CLASS(s->isa_dma);
    ldebug ("dma request %d\n", der->gusdma);
    k->hold_DREQ(s->isa_dma, s->emu.gusdma);
}

static int GUS_read_DMA (void *opaque, int nchan, int dma_pos, int dma_len)
{
    GUSState *s = opaque;
    IsaDmaClass *k = ISADMA_GET_CLASS(s->isa_dma);
    char tmpbuf[4096];
    int pos = dma_pos, mode, left = dma_len - dma_pos;

    ldebug ("read DMA %#x %d\n", dma_pos, dma_len);
    mode = k->has_autoinitialization(s->isa_dma, s->emu.gusdma);
    while (left) {
        int to_copy = MIN ((size_t) left, sizeof (tmpbuf));
        int copied;

        ldebug ("left=%d to_copy=%d pos=%d\n", left, to_copy, pos);
        copied = k->read_memory(s->isa_dma, nchan, tmpbuf, pos, to_copy);
        gus_dma_transferdata (&s->emu, tmpbuf, copied, left == copied);
        left -= copied;
        pos += copied;
    }

    if (((mode >> 4) & 1) == 0) {
        k->release_DREQ(s->isa_dma, s->emu.gusdma);
    }
    return dma_len;
}

static const VMStateDescription vmstate_gus = {
    .name = "gus",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_INT32 (pos, GUSState),
        VMSTATE_INT32 (left, GUSState),
        VMSTATE_INT32 (shift, GUSState),
        VMSTATE_INT32 (irqs, GUSState),
        VMSTATE_INT32 (samples, GUSState),
        VMSTATE_INT64 (last_ticks, GUSState),
        VMSTATE_BUFFER (himem, GUSState),
        VMSTATE_END_OF_LIST ()
    }
};

static const MemoryRegionPortio gus_portio_list1[] = {
    {0x000,  1, 1, .write = gus_writeb },
    {0x006, 10, 1, .read = gus_readb, .write = gus_writeb },
    {0x100,  8, 1, .read = gus_readb, .write = gus_writeb },
    PORTIO_END_OF_LIST (),
};

static const MemoryRegionPortio gus_portio_list2[] = {
    {0, 2, 1, .read = gus_readb },
    PORTIO_END_OF_LIST (),
};

static void gus_realizefn (DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    ISABus *bus = isa_bus_from_device(d);
    GUSState *s = GUS (dev);
    IsaDmaClass *k;
    struct audsettings as;

    s->isa_dma = isa_bus_get_dma(bus, s->emu.gusdma);
    if (!s->isa_dma) {
        error_setg(errp, "ISA controller does not support DMA");
        return;
    }

    AUD_register_card ("gus", &s->card);

    as.freq = s->freq;
    as.nchannels = 2;
    as.fmt = AUDIO_FORMAT_S16;
    as.endianness = AUDIO_HOST_ENDIANNESS;

    s->voice = AUD_open_out (
        &s->card,
        NULL,
        "gus",
        s,
        GUS_callback,
        &as
        );

    if (!s->voice) {
        AUD_remove_card (&s->card);
        error_setg(errp, "No voice");
        return;
    }

    s->shift = 2;
    s->samples = AUD_get_buffer_size_out (s->voice) >> s->shift;
    s->mixbuf = g_malloc0 (s->samples << s->shift);

    isa_register_portio_list(d, &s->portio_list1, s->port,
                             gus_portio_list1, s, "gus");
    isa_register_portio_list(d, &s->portio_list2, (s->port + 0x100) & 0xf00,
                             gus_portio_list2, s, "gus");

    k = ISADMA_GET_CLASS(s->isa_dma);
    k->register_channel(s->isa_dma, s->emu.gusdma, GUS_read_DMA, s);
    s->emu.himemaddr = s->himem;
    s->emu.gusdatapos = s->emu.himemaddr + 1024 * 1024 + 32;
    s->emu.opaque = s;
    s->pic = isa_bus_get_irq(bus, s->emu.gusirq);

    AUD_set_active_out (s->voice, 1);
}

static Property gus_properties[] = {
    DEFINE_AUDIO_PROPERTIES(GUSState, card),
    DEFINE_PROP_UINT32 ("freq",    GUSState, freq,        44100),
    DEFINE_PROP_UINT32 ("iobase",  GUSState, port,        0x240),
    DEFINE_PROP_UINT32 ("irq",     GUSState, emu.gusirq,  7),
    DEFINE_PROP_UINT32 ("dma",     GUSState, emu.gusdma,  3),
    DEFINE_PROP_END_OF_LIST (),
};

static void gus_class_initfn (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);

    dc->realize = gus_realizefn;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Gravis Ultrasound GF1";
    dc->vmsd = &vmstate_gus;
    device_class_set_props(dc, gus_properties);
}

static const TypeInfo gus_info = {
    .name          = TYPE_GUS,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof (GUSState),
    .class_init    = gus_class_initfn,
};

static void gus_register_types (void)
{
    type_register_static (&gus_info);
    deprecated_register_soundhw("gus", "Gravis Ultrasound GF1", 1, TYPE_GUS);
}

type_init (gus_register_types)
