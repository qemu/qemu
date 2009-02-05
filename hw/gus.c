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
#include "hw.h"
#include "audiodev.h"
#include "audio/audio.h"
#include "isa.h"
#include "gusemu.h"
#include "gustate.h"

#define dolog(...) AUD_log ("audio", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#ifdef WORDS_BIGENDIAN
#define GUS_ENDIANNESS 1
#else
#define GUS_ENDIANNESS 0
#endif

#define IO_READ_PROTO(name) \
    static uint32_t name (void *opaque, uint32_t nport)
#define IO_WRITE_PROTO(name) \
    static void name (void *opaque, uint32_t nport, uint32_t val)

static struct {
    int port;
    int irq;
    int dma;
    int freq;
} conf = {0x240, 7, 3, 44100};

typedef struct GUSState {
    GUSEmuState emu;
    QEMUSoundCard card;
    int freq;
    int pos, left, shift, irqs;
    GUSsample *mixbuf;
    uint8_t himem[1024 * 1024 + 32 + 4096];
    int samples;
    SWVoiceOut *voice;
    int64_t last_ticks;
    qemu_irq *pic;
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
    gus_irqgen (&s->emu, (double) (net * 1000000) / s->freq);
}

int GUS_irqrequest (GUSEmuState *emu, int hwirq, int n)
{
    GUSState *s = emu->opaque;
    /* qemu_irq_lower (s->pic[hwirq]); */
    qemu_irq_raise (s->pic[hwirq]);
    s->irqs += n;
    ldebug ("irqrequest %d %d %d\n", hwirq, n, s->irqs);
    return n;
}

void GUS_irqclear (GUSEmuState *emu, int hwirq)
{
    GUSState *s = emu->opaque;
    ldebug ("irqclear %d %d\n", hwirq, s->irqs);
    qemu_irq_lower (s->pic[hwirq]);
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

static void GUS_save (QEMUFile *f, void *opaque)
{
    GUSState *s = opaque;

    qemu_put_be32 (f, s->pos);
    qemu_put_be32 (f, s->left);
    qemu_put_be32 (f, s->shift);
    qemu_put_be32 (f, s->irqs);
    qemu_put_be32 (f, s->samples);
    qemu_put_be64 (f, s->last_ticks);
    qemu_put_buffer (f, s->himem, sizeof (s->himem));
}

static int GUS_load (QEMUFile *f, void *opaque, int version_id)
{
    GUSState *s = opaque;

    if (version_id != 2)
        return -EINVAL;

    s->pos = qemu_get_be32 (f);
    s->left = qemu_get_be32 (f);
    s->shift = qemu_get_be32 (f);
    s->irqs = qemu_get_be32 (f);
    s->samples = qemu_get_be32 (f);
    s->last_ticks = qemu_get_be64 (f);
    qemu_get_buffer (f, s->himem, sizeof (s->himem));
    return 0;
}

int GUS_init (AudioState *audio, qemu_irq *pic)
{
    GUSState *s;
    struct audsettings as;

    if (!audio) {
        dolog ("No audio state\n");
        return -1;
    }

    s = qemu_mallocz (sizeof (*s));

    AUD_register_card (audio, "gus", &s->card);

    as.freq = conf.freq;
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
        qemu_free (s);
        return -1;
    }

    s->shift = 2;
    s->samples = AUD_get_buffer_size_out (s->voice) >> s->shift;
    s->mixbuf = qemu_mallocz (s->samples << s->shift);

    register_ioport_write (conf.port, 1, 1, gus_writeb, s);
    register_ioport_write (conf.port, 1, 2, gus_writew, s);

    register_ioport_read ((conf.port + 0x100) & 0xf00, 1, 1, gus_readb, s);
    register_ioport_read ((conf.port + 0x100) & 0xf00, 1, 2, gus_readw, s);

    register_ioport_write (conf.port + 6, 10, 1, gus_writeb, s);
    register_ioport_write (conf.port + 6, 10, 2, gus_writew, s);
    register_ioport_read (conf.port + 6, 10, 1, gus_readb, s);
    register_ioport_read (conf.port + 6, 10, 2, gus_readw, s);


    register_ioport_write (conf.port + 0x100, 8, 1, gus_writeb, s);
    register_ioport_write (conf.port + 0x100, 8, 2, gus_writew, s);
    register_ioport_read (conf.port + 0x100, 8, 1, gus_readb, s);
    register_ioport_read (conf.port + 0x100, 8, 2, gus_readw, s);

    DMA_register_channel (conf.dma, GUS_read_DMA, s);
    s->emu.gusirq = conf.irq;
    s->emu.gusdma = conf.dma;
    s->emu.himemaddr = s->himem;
    s->emu.gusdatapos = s->emu.himemaddr + 1024 * 1024 + 32;
    s->emu.opaque = s;
    s->freq = conf.freq;
    s->pic = pic;

    AUD_set_active_out (s->voice, 1);

    register_savevm ("gus", 0, 2, GUS_save, GUS_load, s);
    return 0;
}
