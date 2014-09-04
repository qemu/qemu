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
#include "hw/hw.h"
#include "hw/audio/audio.h"
#include "audio/audio.h"
#include "hw/isa/isa.h"
#include "gusemu.h"
#include "gustate.h"

#define dolog(...) AUD_log ("audio", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#ifdef HOST_WORDS_BIGENDIAN
#define GUS_ENDIANNESS 1
#else
#define GUS_ENDIANNESS 0
#endif

#define IO_READ_PROTO(name) \
    static uint32_t name (void *opaque, uint32_t nport)
#define IO_WRITE_PROTO(name) \
    static void name (void *opaque, uint32_t nport, uint32_t val)

#define TYPE_GUS "gus"
#define GUS(obj) OBJECT_CHECK (GUSState, (obj), TYPE_GUS)

typedef struct GUSState {
    ISADevice dev;
    GUSEmuState emu;
    QEMUSoundCard card;
    uint32_t freq;
    uint32_t port;
    int pos, left, shift, irqs;
    GUSsample *mixbuf;
    uint8_t himem[1024 * 1024 + 32 + 4096];
    int samples;
    SWVoiceOut *voice;
    int64_t last_ticks;
    qemu_irq pic;
} GUSState;

IO_READ_PROTO (gus_readb)
{
    GUSState *s = opaque;

    return gus_read (&s->emu, nport, 1);
}

IO_READ_PROTO (gus_readw)
{
    GUSState *s = opaque;

    return gus_read (&s->emu, nport, 2);
}

IO_WRITE_PROTO (gus_writeb)
{
    GUSState *s = opaque;

    gus_write (&s->emu, nport, 1, val);
}

IO_WRITE_PROTO (gus_writew)
{
    GUSState *s = opaque;

    gus_write (&s->emu, nport, 2, val);
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
    to_play = audio_MIN (samples, s->left);

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

    samples = audio_MIN (samples, s->samples);
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
    gus_irqgen (&s->emu, muldiv64 (net, 1000000, s->freq));
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

void GUS_dmarequest (GUSEmuState *der)
{
    /* GUSState *s = (GUSState *) der; */
    ldebug ("dma request %d\n", der->gusdma);
    DMA_hold_DREQ (der->gusdma);
}

static int GUS_read_DMA (void *opaque, int nchan, int dma_pos, int dma_len)
{
    GUSState *s = opaque;
    char tmpbuf[4096];
    int pos = dma_pos, mode, left = dma_len - dma_pos;

    ldebug ("read DMA %#x %d\n", dma_pos, dma_len);
    mode = DMA_get_channel_mode (s->emu.gusdma);
    while (left) {
        int to_copy = audio_MIN ((size_t) left, sizeof (tmpbuf));
        int copied;

        ldebug ("left=%d to_copy=%d pos=%d\n", left, to_copy, pos);
        copied = DMA_read_memory (nchan, tmpbuf, pos, to_copy);
        gus_dma_transferdata (&s->emu, tmpbuf, copied, left == copied);
        left -= copied;
        pos += copied;
    }

    if (0 == ((mode >> 4) & 1)) {
        DMA_release_DREQ (s->emu.gusdma);
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
    {0x000,  1, 2, .write = gus_writew },
    {0x006, 10, 1, .read = gus_readb, .write = gus_writeb },
    {0x006, 10, 2, .read = gus_readw, .write = gus_writew },
    {0x100,  8, 1, .read = gus_readb, .write = gus_writeb },
    {0x100,  8, 2, .read = gus_readw, .write = gus_writew },
    PORTIO_END_OF_LIST (),
};

static const MemoryRegionPortio gus_portio_list2[] = {
    {0, 1, 1, .read = gus_readb },
    {0, 1, 2, .read = gus_readw },
    PORTIO_END_OF_LIST (),
};

static void gus_realizefn (DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    GUSState *s = GUS (dev);
    struct audsettings as;

    AUD_register_card ("gus", &s->card);

    as.freq = s->freq;
    as.nchannels = 2;
    as.fmt = AUD_FMT_S16;
    as.endianness = GUS_ENDIANNESS;

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

    isa_register_portio_list (d, s->port, gus_portio_list1, s, "gus");
    isa_register_portio_list (d, (s->port + 0x100) & 0xf00,
                              gus_portio_list2, s, "gus");

    DMA_register_channel (s->emu.gusdma, GUS_read_DMA, s);
    s->emu.himemaddr = s->himem;
    s->emu.gusdatapos = s->emu.himemaddr + 1024 * 1024 + 32;
    s->emu.opaque = s;
    isa_init_irq (d, &s->pic, s->emu.gusirq);

    AUD_set_active_out (s->voice, 1);
}

static int GUS_init (ISABus *bus)
{
    isa_create_simple (bus, TYPE_GUS);
    return 0;
}

static Property gus_properties[] = {
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
    dc->props = gus_properties;
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
    isa_register_soundhw("gus", "Gravis Ultrasound GF1", GUS_init);
}

type_init (gus_register_types)
