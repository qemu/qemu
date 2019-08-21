/*
 * QEMU Soundblaster 16 emulation
 *
 * Copyright (c) 2003-2005 Vassili Karpov (malc)
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
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

#define dolog(...) AUD_log ("sb16", __VA_ARGS__)

/* #define DEBUG */
/* #define DEBUG_SB16_MOST */

#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

static const char e3[] = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";

#define TYPE_SB16 "sb16"
#define SB16(obj) OBJECT_CHECK (SB16State, (obj), TYPE_SB16)

typedef struct SB16State {
    ISADevice parent_obj;

    QEMUSoundCard card;
    qemu_irq pic;
    uint32_t irq;
    uint32_t dma;
    uint32_t hdma;
    uint32_t port;
    uint32_t ver;
    IsaDma *isa_dma;
    IsaDma *isa_hdma;

    int in_index;
    int out_data_len;
    int fmt_stereo;
    int fmt_signed;
    int fmt_bits;
    AudioFormat fmt;
    int dma_auto;
    int block_size;
    int fifo;
    int freq;
    int time_const;
    int speaker;
    int needed_bytes;
    int cmd;
    int use_hdma;
    int highspeed;
    int can_write;

    int v2x6;

    uint8_t csp_param;
    uint8_t csp_value;
    uint8_t csp_mode;
    uint8_t csp_regs[256];
    uint8_t csp_index;
    uint8_t csp_reg83[4];
    int csp_reg83r;
    int csp_reg83w;

    uint8_t in2_data[10];
    uint8_t out_data[50];
    uint8_t test_reg;
    uint8_t last_read_byte;
    int nzero;

    int left_till_irq;

    int dma_running;
    int bytes_per_second;
    int align;
    int audio_free;
    SWVoiceOut *voice;

    QEMUTimer *aux_ts;
    /* mixer state */
    int mixer_nreg;
    uint8_t mixer_regs[256];
    PortioList portio_list;
} SB16State;

static void SB_audio_callback (void *opaque, int free);

static int magic_of_irq (int irq)
{
    switch (irq) {
    case 5:
        return 2;
    case 7:
        return 4;
    case 9:
        return 1;
    case 10:
        return 8;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad irq %d\n", irq);
        return 2;
    }
}

static int irq_of_magic (int magic)
{
    switch (magic) {
    case 1:
        return 9;
    case 2:
        return 5;
    case 4:
        return 7;
    case 8:
        return 10;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad irq magic %d\n", magic);
        return -1;
    }
}

#if 0
static void log_dsp (SB16State *dsp)
{
    ldebug ("%s:%s:%d:%s:dmasize=%d:freq=%d:const=%d:speaker=%d\n",
            dsp->fmt_stereo ? "Stereo" : "Mono",
            dsp->fmt_signed ? "Signed" : "Unsigned",
            dsp->fmt_bits,
            dsp->dma_auto ? "Auto" : "Single",
            dsp->block_size,
            dsp->freq,
            dsp->time_const,
            dsp->speaker);
}
#endif

static void speaker (SB16State *s, int on)
{
    s->speaker = on;
    /* AUD_enable (s->voice, on); */
}

static void control (SB16State *s, int hold)
{
    int dma = s->use_hdma ? s->hdma : s->dma;
    IsaDma *isa_dma = s->use_hdma ? s->isa_hdma : s->isa_dma;
    IsaDmaClass *k = ISADMA_GET_CLASS(isa_dma);
    s->dma_running = hold;

    ldebug ("hold %d high %d dma %d\n", hold, s->use_hdma, dma);

    if (hold) {
        k->hold_DREQ(isa_dma, dma);
        AUD_set_active_out (s->voice, 1);
    }
    else {
        k->release_DREQ(isa_dma, dma);
        AUD_set_active_out (s->voice, 0);
    }
}

static void aux_timer (void *opaque)
{
    SB16State *s = opaque;
    s->can_write = 1;
    qemu_irq_raise (s->pic);
}

#define DMA8_AUTO 1
#define DMA8_HIGH 2

static void continue_dma8 (SB16State *s)
{
    if (s->freq > 0) {
        struct audsettings as;

        s->audio_free = 0;

        as.freq = s->freq;
        as.nchannels = 1 << s->fmt_stereo;
        as.fmt = s->fmt;
        as.endianness = 0;

        s->voice = AUD_open_out (
            &s->card,
            s->voice,
            "sb16",
            s,
            SB_audio_callback,
            &as
            );
    }

    control (s, 1);
}

static void dma_cmd8 (SB16State *s, int mask, int dma_len)
{
    s->fmt = AUDIO_FORMAT_U8;
    s->use_hdma = 0;
    s->fmt_bits = 8;
    s->fmt_signed = 0;
    s->fmt_stereo = (s->mixer_regs[0x0e] & 2) != 0;
    if (-1 == s->time_const) {
        if (s->freq <= 0)
            s->freq = 11025;
    }
    else {
        int tmp = (256 - s->time_const);
        s->freq = (1000000 + (tmp / 2)) / tmp;
    }

    if (dma_len != -1) {
        s->block_size = dma_len << s->fmt_stereo;
    }
    else {
        /* This is apparently the only way to make both Act1/PL
           and SecondReality/FC work

           Act1 sets block size via command 0x48 and it's an odd number
           SR does the same with even number
           Both use stereo, and Creatives own documentation states that
           0x48 sets block size in bytes less one.. go figure */
        s->block_size &= ~s->fmt_stereo;
    }

    s->freq >>= s->fmt_stereo;
    s->left_till_irq = s->block_size;
    s->bytes_per_second = (s->freq << s->fmt_stereo);
    /* s->highspeed = (mask & DMA8_HIGH) != 0; */
    s->dma_auto = (mask & DMA8_AUTO) != 0;
    s->align = (1 << s->fmt_stereo) - 1;

    if (s->block_size & s->align) {
        qemu_log_mask(LOG_GUEST_ERROR, "warning: misaligned block size %d,"
                      " alignment %d\n", s->block_size, s->align + 1);
    }

    ldebug ("freq %d, stereo %d, sign %d, bits %d, "
            "dma %d, auto %d, fifo %d, high %d\n",
            s->freq, s->fmt_stereo, s->fmt_signed, s->fmt_bits,
            s->block_size, s->dma_auto, s->fifo, s->highspeed);

    continue_dma8 (s);
    speaker (s, 1);
}

static void dma_cmd (SB16State *s, uint8_t cmd, uint8_t d0, int dma_len)
{
    s->use_hdma = cmd < 0xc0;
    s->fifo = (cmd >> 1) & 1;
    s->dma_auto = (cmd >> 2) & 1;
    s->fmt_signed = (d0 >> 4) & 1;
    s->fmt_stereo = (d0 >> 5) & 1;

    switch (cmd >> 4) {
    case 11:
        s->fmt_bits = 16;
        break;

    case 12:
        s->fmt_bits = 8;
        break;
    }

    if (-1 != s->time_const) {
#if 1
        int tmp = 256 - s->time_const;
        s->freq = (1000000 + (tmp / 2)) / tmp;
#else
        /* s->freq = 1000000 / ((255 - s->time_const) << s->fmt_stereo); */
        s->freq = 1000000 / ((255 - s->time_const));
#endif
        s->time_const = -1;
    }

    s->block_size = dma_len + 1;
    s->block_size <<= (s->fmt_bits == 16);
    if (!s->dma_auto) {
        /* It is clear that for DOOM and auto-init this value
           shouldn't take stereo into account, while Miles Sound Systems
           setsound.exe with single transfer mode wouldn't work without it
           wonders of SB16 yet again */
        s->block_size <<= s->fmt_stereo;
    }

    ldebug ("freq %d, stereo %d, sign %d, bits %d, "
            "dma %d, auto %d, fifo %d, high %d\n",
            s->freq, s->fmt_stereo, s->fmt_signed, s->fmt_bits,
            s->block_size, s->dma_auto, s->fifo, s->highspeed);

    if (16 == s->fmt_bits) {
        if (s->fmt_signed) {
            s->fmt = AUDIO_FORMAT_S16;
        }
        else {
            s->fmt = AUDIO_FORMAT_U16;
        }
    }
    else {
        if (s->fmt_signed) {
            s->fmt = AUDIO_FORMAT_S8;
        }
        else {
            s->fmt = AUDIO_FORMAT_U8;
        }
    }

    s->left_till_irq = s->block_size;

    s->bytes_per_second = (s->freq << s->fmt_stereo) << (s->fmt_bits == 16);
    s->highspeed = 0;
    s->align = (1 << (s->fmt_stereo + (s->fmt_bits == 16))) - 1;
    if (s->block_size & s->align) {
        qemu_log_mask(LOG_GUEST_ERROR, "warning: misaligned block size %d,"
                      " alignment %d\n", s->block_size, s->align + 1);
    }

    if (s->freq) {
        struct audsettings as;

        s->audio_free = 0;

        as.freq = s->freq;
        as.nchannels = 1 << s->fmt_stereo;
        as.fmt = s->fmt;
        as.endianness = 0;

        s->voice = AUD_open_out (
            &s->card,
            s->voice,
            "sb16",
            s,
            SB_audio_callback,
            &as
            );
    }

    control (s, 1);
    speaker (s, 1);
}

static inline void dsp_out_data (SB16State *s, uint8_t val)
{
    ldebug ("outdata %#x\n", val);
    if ((size_t) s->out_data_len < sizeof (s->out_data)) {
        s->out_data[s->out_data_len++] = val;
    }
}

static inline uint8_t dsp_get_data (SB16State *s)
{
    if (s->in_index) {
        return s->in2_data[--s->in_index];
    }
    else {
        dolog ("buffer underflow\n");
        return 0;
    }
}

static void command (SB16State *s, uint8_t cmd)
{
    ldebug ("command %#x\n", cmd);

    if (cmd > 0xaf && cmd < 0xd0) {
        if (cmd & 8) {
            qemu_log_mask(LOG_UNIMP, "ADC not yet supported (command %#x)\n",
                          cmd);
        }

        switch (cmd >> 4) {
        case 11:
        case 12:
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%#x wrong bits\n", cmd);
        }
        s->needed_bytes = 3;
    }
    else {
        s->needed_bytes = 0;

        switch (cmd) {
        case 0x03:
            dsp_out_data (s, 0x10); /* s->csp_param); */
            goto warn;

        case 0x04:
            s->needed_bytes = 1;
            goto warn;

        case 0x05:
            s->needed_bytes = 2;
            goto warn;

        case 0x08:
            /* __asm__ ("int3"); */
            goto warn;

        case 0x0e:
            s->needed_bytes = 2;
            goto warn;

        case 0x09:
            dsp_out_data (s, 0xf8);
            goto warn;

        case 0x0f:
            s->needed_bytes = 1;
            goto warn;

        case 0x10:
            s->needed_bytes = 1;
            goto warn;

        case 0x14:
            s->needed_bytes = 2;
            s->block_size = 0;
            break;

        case 0x1c:              /* Auto-Initialize DMA DAC, 8-bit */
            dma_cmd8 (s, DMA8_AUTO, -1);
            break;

        case 0x20:              /* Direct ADC, Juice/PL */
            dsp_out_data (s, 0xff);
            goto warn;

        case 0x35:
            qemu_log_mask(LOG_UNIMP, "0x35 - MIDI command not implemented\n");
            break;

        case 0x40:
            s->freq = -1;
            s->time_const = -1;
            s->needed_bytes = 1;
            break;

        case 0x41:
            s->freq = -1;
            s->time_const = -1;
            s->needed_bytes = 2;
            break;

        case 0x42:
            s->freq = -1;
            s->time_const = -1;
            s->needed_bytes = 2;
            goto warn;

        case 0x45:
            dsp_out_data (s, 0xaa);
            goto warn;

        case 0x47:                /* Continue Auto-Initialize DMA 16bit */
            break;

        case 0x48:
            s->needed_bytes = 2;
            break;

        case 0x74:
            s->needed_bytes = 2; /* DMA DAC, 4-bit ADPCM */
            qemu_log_mask(LOG_UNIMP, "0x75 - DMA DAC, 4-bit ADPCM not"
                          " implemented\n");
            break;

        case 0x75:              /* DMA DAC, 4-bit ADPCM Reference */
            s->needed_bytes = 2;
            qemu_log_mask(LOG_UNIMP, "0x74 - DMA DAC, 4-bit ADPCM Reference not"
                          " implemented\n");
            break;

        case 0x76:              /* DMA DAC, 2.6-bit ADPCM */
            s->needed_bytes = 2;
            qemu_log_mask(LOG_UNIMP, "0x74 - DMA DAC, 2.6-bit ADPCM not"
                          " implemented\n");
            break;

        case 0x77:              /* DMA DAC, 2.6-bit ADPCM Reference */
            s->needed_bytes = 2;
            qemu_log_mask(LOG_UNIMP, "0x74 - DMA DAC, 2.6-bit ADPCM Reference"
                          " not implemented\n");
            break;

        case 0x7d:
            qemu_log_mask(LOG_UNIMP, "0x7d - Autio-Initialize DMA DAC, 4-bit"
                          " ADPCM Reference\n");
            qemu_log_mask(LOG_UNIMP, "not implemented\n");
            break;

        case 0x7f:
            qemu_log_mask(LOG_UNIMP, "0x7d - Autio-Initialize DMA DAC, 2.6-bit"
                          " ADPCM Reference\n");
            qemu_log_mask(LOG_UNIMP, "not implemented\n");
            break;

        case 0x80:
            s->needed_bytes = 2;
            break;

        case 0x90:
        case 0x91:
            dma_cmd8 (s, ((cmd & 1) == 0) | DMA8_HIGH, -1);
            break;

        case 0xd0:              /* halt DMA operation. 8bit */
            control (s, 0);
            break;

        case 0xd1:              /* speaker on */
            speaker (s, 1);
            break;

        case 0xd3:              /* speaker off */
            speaker (s, 0);
            break;

        case 0xd4:              /* continue DMA operation. 8bit */
            /* KQ6 (or maybe Sierras audblst.drv in general) resets
               the frequency between halt/continue */
            continue_dma8 (s);
            break;

        case 0xd5:              /* halt DMA operation. 16bit */
            control (s, 0);
            break;

        case 0xd6:              /* continue DMA operation. 16bit */
            control (s, 1);
            break;

        case 0xd9:              /* exit auto-init DMA after this block. 16bit */
            s->dma_auto = 0;
            break;

        case 0xda:              /* exit auto-init DMA after this block. 8bit */
            s->dma_auto = 0;
            break;

        case 0xe0:              /* DSP identification */
            s->needed_bytes = 1;
            break;

        case 0xe1:
            dsp_out_data (s, s->ver & 0xff);
            dsp_out_data (s, s->ver >> 8);
            break;

        case 0xe2:
            s->needed_bytes = 1;
            goto warn;

        case 0xe3:
            {
                int i;
                for (i = sizeof (e3) - 1; i >= 0; --i)
                    dsp_out_data (s, e3[i]);
            }
            break;

        case 0xe4:              /* write test reg */
            s->needed_bytes = 1;
            break;

        case 0xe7:
            qemu_log_mask(LOG_UNIMP, "Attempt to probe for ESS (0xe7)?\n");
            break;

        case 0xe8:              /* read test reg */
            dsp_out_data (s, s->test_reg);
            break;

        case 0xf2:
        case 0xf3:
            dsp_out_data (s, 0xaa);
            s->mixer_regs[0x82] |= (cmd == 0xf2) ? 1 : 2;
            qemu_irq_raise (s->pic);
            break;

        case 0xf9:
            s->needed_bytes = 1;
            goto warn;

        case 0xfa:
            dsp_out_data (s, 0);
            goto warn;

        case 0xfc:              /* FIXME */
            dsp_out_data (s, 0);
            goto warn;

        default:
            qemu_log_mask(LOG_UNIMP, "Unrecognized command %#x\n", cmd);
            break;
        }
    }

    if (!s->needed_bytes) {
        ldebug ("\n");
    }

 exit:
    if (!s->needed_bytes) {
        s->cmd = -1;
    }
    else {
        s->cmd = cmd;
    }
    return;

 warn:
    qemu_log_mask(LOG_UNIMP, "warning: command %#x,%d is not truly understood"
                  " yet\n", cmd, s->needed_bytes);
    goto exit;

}

static uint16_t dsp_get_lohi (SB16State *s)
{
    uint8_t hi = dsp_get_data (s);
    uint8_t lo = dsp_get_data (s);
    return (hi << 8) | lo;
}

static uint16_t dsp_get_hilo (SB16State *s)
{
    uint8_t lo = dsp_get_data (s);
    uint8_t hi = dsp_get_data (s);
    return (hi << 8) | lo;
}

static void complete (SB16State *s)
{
    int d0, d1, d2;
    ldebug ("complete command %#x, in_index %d, needed_bytes %d\n",
            s->cmd, s->in_index, s->needed_bytes);

    if (s->cmd > 0xaf && s->cmd < 0xd0) {
        d2 = dsp_get_data (s);
        d1 = dsp_get_data (s);
        d0 = dsp_get_data (s);

        if (s->cmd & 8) {
            dolog ("ADC params cmd = %#x d0 = %d, d1 = %d, d2 = %d\n",
                   s->cmd, d0, d1, d2);
        }
        else {
            ldebug ("cmd = %#x d0 = %d, d1 = %d, d2 = %d\n",
                    s->cmd, d0, d1, d2);
            dma_cmd (s, s->cmd, d0, d1 + (d2 << 8));
        }
    }
    else {
        switch (s->cmd) {
        case 0x04:
            s->csp_mode = dsp_get_data (s);
            s->csp_reg83r = 0;
            s->csp_reg83w = 0;
            ldebug ("CSP command 0x04: mode=%#x\n", s->csp_mode);
            break;

        case 0x05:
            s->csp_param = dsp_get_data (s);
            s->csp_value = dsp_get_data (s);
            ldebug ("CSP command 0x05: param=%#x value=%#x\n",
                    s->csp_param,
                    s->csp_value);
            break;

        case 0x0e:
            d0 = dsp_get_data (s);
            d1 = dsp_get_data (s);
            ldebug ("write CSP register %d <- %#x\n", d1, d0);
            if (d1 == 0x83) {
                ldebug ("0x83[%d] <- %#x\n", s->csp_reg83r, d0);
                s->csp_reg83[s->csp_reg83r % 4] = d0;
                s->csp_reg83r += 1;
            }
            else {
                s->csp_regs[d1] = d0;
            }
            break;

        case 0x0f:
            d0 = dsp_get_data (s);
            ldebug ("read CSP register %#x -> %#x, mode=%#x\n",
                    d0, s->csp_regs[d0], s->csp_mode);
            if (d0 == 0x83) {
                ldebug ("0x83[%d] -> %#x\n",
                        s->csp_reg83w,
                        s->csp_reg83[s->csp_reg83w % 4]);
                dsp_out_data (s, s->csp_reg83[s->csp_reg83w % 4]);
                s->csp_reg83w += 1;
            }
            else {
                dsp_out_data (s, s->csp_regs[d0]);
            }
            break;

        case 0x10:
            d0 = dsp_get_data (s);
            dolog ("cmd 0x10 d0=%#x\n", d0);
            break;

        case 0x14:
            dma_cmd8 (s, 0, dsp_get_lohi (s) + 1);
            break;

        case 0x40:
            s->time_const = dsp_get_data (s);
            ldebug ("set time const %d\n", s->time_const);
            break;

        case 0x41:
        case 0x42:
            /*
             * 0x41 is documented as setting the output sample rate,
             * and 0x42 the input sample rate, but in fact SB16 hardware
             * seems to have only a single sample rate under the hood,
             * and FT2 sets output freq with this (go figure).  Compare:
             * http://homepages.cae.wisc.edu/~brodskye/sb16doc/sb16doc.html#SamplingRate
             */
            s->freq = dsp_get_hilo (s);
            ldebug ("set freq %d\n", s->freq);
            break;

        case 0x48:
            s->block_size = dsp_get_lohi (s) + 1;
            ldebug ("set dma block len %d\n", s->block_size);
            break;

        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
            /* ADPCM stuff, ignore */
            break;

        case 0x80:
            {
                int freq, samples, bytes;
                int64_t ticks;

                freq = s->freq > 0 ? s->freq : 11025;
                samples = dsp_get_lohi (s) + 1;
                bytes = samples << s->fmt_stereo << (s->fmt_bits == 16);
                ticks = muldiv64(bytes, NANOSECONDS_PER_SECOND, freq);
                if (ticks < NANOSECONDS_PER_SECOND / 1024) {
                    qemu_irq_raise (s->pic);
                }
                else {
                    if (s->aux_ts) {
                        timer_mod (
                            s->aux_ts,
                            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ticks
                            );
                    }
                }
                ldebug ("mix silence %d %d %" PRId64 "\n", samples, bytes, ticks);
            }
            break;

        case 0xe0:
            d0 = dsp_get_data (s);
            s->out_data_len = 0;
            ldebug ("E0 data = %#x\n", d0);
            dsp_out_data (s, ~d0);
            break;

        case 0xe2:
#ifdef DEBUG
            d0 = dsp_get_data (s);
            dolog ("E2 = %#x\n", d0);
#endif
            break;

        case 0xe4:
            s->test_reg = dsp_get_data (s);
            break;

        case 0xf9:
            d0 = dsp_get_data (s);
            ldebug ("command 0xf9 with %#x\n", d0);
            switch (d0) {
            case 0x0e:
                dsp_out_data (s, 0xff);
                break;

            case 0x0f:
                dsp_out_data (s, 0x07);
                break;

            case 0x37:
                dsp_out_data (s, 0x38);
                break;

            default:
                dsp_out_data (s, 0x00);
                break;
            }
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "complete: unrecognized command %#x\n",
                          s->cmd);
            return;
        }
    }

    ldebug ("\n");
    s->cmd = -1;
}

static void legacy_reset (SB16State *s)
{
    struct audsettings as;

    s->freq = 11025;
    s->fmt_signed = 0;
    s->fmt_bits = 8;
    s->fmt_stereo = 0;

    as.freq = s->freq;
    as.nchannels = 1;
    as.fmt = AUDIO_FORMAT_U8;
    as.endianness = 0;

    s->voice = AUD_open_out (
        &s->card,
        s->voice,
        "sb16",
        s,
        SB_audio_callback,
        &as
        );

    /* Not sure about that... */
    /* AUD_set_active_out (s->voice, 1); */
}

static void reset (SB16State *s)
{
    qemu_irq_lower (s->pic);
    if (s->dma_auto) {
        qemu_irq_raise (s->pic);
        qemu_irq_lower (s->pic);
    }

    s->mixer_regs[0x82] = 0;
    s->dma_auto = 0;
    s->in_index = 0;
    s->out_data_len = 0;
    s->left_till_irq = 0;
    s->needed_bytes = 0;
    s->block_size = -1;
    s->nzero = 0;
    s->highspeed = 0;
    s->v2x6 = 0;
    s->cmd = -1;

    dsp_out_data (s, 0xaa);
    speaker (s, 0);
    control (s, 0);
    legacy_reset (s);
}

static void dsp_write(void *opaque, uint32_t nport, uint32_t val)
{
    SB16State *s = opaque;
    int iport;

    iport = nport - s->port;

    ldebug ("write %#x <- %#x\n", nport, val);
    switch (iport) {
    case 0x06:
        switch (val) {
        case 0x00:
            if (s->v2x6 == 1) {
                reset (s);
            }
            s->v2x6 = 0;
            break;

        case 0x01:
        case 0x03:              /* FreeBSD kludge */
            s->v2x6 = 1;
            break;

        case 0xc6:
            s->v2x6 = 0;        /* Prince of Persia, csp.sys, diagnose.exe */
            break;

        case 0xb8:              /* Panic */
            reset (s);
            break;

        case 0x39:
            dsp_out_data (s, 0x38);
            reset (s);
            s->v2x6 = 0x39;
            break;

        default:
            s->v2x6 = val;
            break;
        }
        break;

    case 0x0c:                  /* write data or command | write status */
/*         if (s->highspeed) */
/*             break; */

        if (s->needed_bytes == 0) {
            command (s, val);
#if 0
            if (0 == s->needed_bytes) {
                log_dsp (s);
            }
#endif
        }
        else {
            if (s->in_index == sizeof (s->in2_data)) {
                dolog ("in data overrun\n");
            }
            else {
                s->in2_data[s->in_index++] = val;
                if (s->in_index == s->needed_bytes) {
                    s->needed_bytes = 0;
                    complete (s);
#if 0
                    log_dsp (s);
#endif
                }
            }
        }
        break;

    default:
        ldebug ("(nport=%#x, val=%#x)\n", nport, val);
        break;
    }
}

static uint32_t dsp_read(void *opaque, uint32_t nport)
{
    SB16State *s = opaque;
    int iport, retval, ack = 0;

    iport = nport - s->port;

    switch (iport) {
    case 0x06:                  /* reset */
        retval = 0xff;
        break;

    case 0x0a:                  /* read data */
        if (s->out_data_len) {
            retval = s->out_data[--s->out_data_len];
            s->last_read_byte = retval;
        }
        else {
            if (s->cmd != -1) {
                dolog ("empty output buffer for command %#x\n",
                       s->cmd);
            }
            retval = s->last_read_byte;
            /* goto error; */
        }
        break;

    case 0x0c:                  /* 0 can write */
        retval = s->can_write ? 0 : 0x80;
        break;

    case 0x0d:                  /* timer interrupt clear */
        /* dolog ("timer interrupt clear\n"); */
        retval = 0;
        break;

    case 0x0e:                  /* data available status | irq 8 ack */
        retval = (!s->out_data_len || s->highspeed) ? 0 : 0x80;
        if (s->mixer_regs[0x82] & 1) {
            ack = 1;
            s->mixer_regs[0x82] &= ~1;
            qemu_irq_lower (s->pic);
        }
        break;

    case 0x0f:                  /* irq 16 ack */
        retval = 0xff;
        if (s->mixer_regs[0x82] & 2) {
            ack = 1;
            s->mixer_regs[0x82] &= ~2;
            qemu_irq_lower (s->pic);
        }
        break;

    default:
        goto error;
    }

    if (!ack) {
        ldebug ("read %#x -> %#x\n", nport, retval);
    }

    return retval;

 error:
    dolog ("warning: dsp_read %#x error\n", nport);
    return 0xff;
}

static void reset_mixer (SB16State *s)
{
    int i;

    memset (s->mixer_regs, 0xff, 0x7f);
    memset (s->mixer_regs + 0x83, 0xff, sizeof (s->mixer_regs) - 0x83);

    s->mixer_regs[0x02] = 4;    /* master volume 3bits */
    s->mixer_regs[0x06] = 4;    /* MIDI volume 3bits */
    s->mixer_regs[0x08] = 0;    /* CD volume 3bits */
    s->mixer_regs[0x0a] = 0;    /* voice volume 2bits */

    /* d5=input filt, d3=lowpass filt, d1,d2=input source */
    s->mixer_regs[0x0c] = 0;

    /* d5=output filt, d1=stereo switch */
    s->mixer_regs[0x0e] = 0;

    /* voice volume L d5,d7, R d1,d3 */
    s->mixer_regs[0x04] = (4 << 5) | (4 << 1);
    /* master ... */
    s->mixer_regs[0x22] = (4 << 5) | (4 << 1);
    /* MIDI ... */
    s->mixer_regs[0x26] = (4 << 5) | (4 << 1);

    for (i = 0x30; i < 0x48; i++) {
        s->mixer_regs[i] = 0x20;
    }
}

static void mixer_write_indexb(void *opaque, uint32_t nport, uint32_t val)
{
    SB16State *s = opaque;
    (void) nport;
    s->mixer_nreg = val;
}

static void mixer_write_datab(void *opaque, uint32_t nport, uint32_t val)
{
    SB16State *s = opaque;

    (void) nport;
    ldebug ("mixer_write [%#x] <- %#x\n", s->mixer_nreg, val);

    switch (s->mixer_nreg) {
    case 0x00:
        reset_mixer (s);
        break;

    case 0x80:
        {
            int irq = irq_of_magic (val);
            ldebug ("setting irq to %d (val=%#x)\n", irq, val);
            if (irq > 0) {
                s->irq = irq;
            }
        }
        break;

    case 0x81:
        {
            int dma, hdma;

            dma = ctz32 (val & 0xf);
            hdma = ctz32 (val & 0xf0);
            if (dma != s->dma || hdma != s->hdma) {
                qemu_log_mask(LOG_GUEST_ERROR, "attempt to change DMA 8bit"
                              " %d(%d), 16bit %d(%d) (val=%#x)\n", dma, s->dma,
                              hdma, s->hdma, val);
            }
#if 0
            s->dma = dma;
            s->hdma = hdma;
#endif
        }
        break;

    case 0x82:
        qemu_log_mask(LOG_GUEST_ERROR, "attempt to write into IRQ status"
                      " register (val=%#x)\n", val);
        return;

    default:
        if (s->mixer_nreg >= 0x80) {
            ldebug ("attempt to write mixer[%#x] <- %#x\n", s->mixer_nreg, val);
        }
        break;
    }

    s->mixer_regs[s->mixer_nreg] = val;
}

static uint32_t mixer_read(void *opaque, uint32_t nport)
{
    SB16State *s = opaque;

    (void) nport;
#ifndef DEBUG_SB16_MOST
    if (s->mixer_nreg != 0x82) {
        ldebug ("mixer_read[%#x] -> %#x\n",
                s->mixer_nreg, s->mixer_regs[s->mixer_nreg]);
    }
#else
    ldebug ("mixer_read[%#x] -> %#x\n",
            s->mixer_nreg, s->mixer_regs[s->mixer_nreg]);
#endif
    return s->mixer_regs[s->mixer_nreg];
}

static int write_audio (SB16State *s, int nchan, int dma_pos,
                        int dma_len, int len)
{
    IsaDma *isa_dma = nchan == s->dma ? s->isa_dma : s->isa_hdma;
    IsaDmaClass *k = ISADMA_GET_CLASS(isa_dma);
    int temp, net;
    uint8_t tmpbuf[4096];

    temp = len;
    net = 0;

    while (temp) {
        int left = dma_len - dma_pos;
        int copied;
        size_t to_copy;

        to_copy = MIN (temp, left);
        if (to_copy > sizeof (tmpbuf)) {
            to_copy = sizeof (tmpbuf);
        }

        copied = k->read_memory(isa_dma, nchan, tmpbuf, dma_pos, to_copy);
        copied = AUD_write (s->voice, tmpbuf, copied);

        temp -= copied;
        dma_pos = (dma_pos + copied) % dma_len;
        net += copied;

        if (!copied) {
            break;
        }
    }

    return net;
}

static int SB_read_DMA (void *opaque, int nchan, int dma_pos, int dma_len)
{
    SB16State *s = opaque;
    int till, copy, written, free;

    if (s->block_size <= 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "invalid block size=%d nchan=%d"
                      " dma_pos=%d dma_len=%d\n", s->block_size, nchan,
                      dma_pos, dma_len);
        return dma_pos;
    }

    if (s->left_till_irq < 0) {
        s->left_till_irq = s->block_size;
    }

    if (s->voice) {
        free = s->audio_free & ~s->align;
        if ((free <= 0) || !dma_len) {
            return dma_pos;
        }
    }
    else {
        free = dma_len;
    }

    copy = free;
    till = s->left_till_irq;

#ifdef DEBUG_SB16_MOST
    dolog ("pos:%06d %d till:%d len:%d\n",
           dma_pos, free, till, dma_len);
#endif

    if (till <= copy) {
        if (s->dma_auto == 0) {
            copy = till;
        }
    }

    written = write_audio (s, nchan, dma_pos, dma_len, copy);
    dma_pos = (dma_pos + written) % dma_len;
    s->left_till_irq -= written;

    if (s->left_till_irq <= 0) {
        s->mixer_regs[0x82] |= (nchan & 4) ? 2 : 1;
        qemu_irq_raise (s->pic);
        if (s->dma_auto == 0) {
            control (s, 0);
            speaker (s, 0);
        }
    }

#ifdef DEBUG_SB16_MOST
    ldebug ("pos %5d free %5d size %5d till % 5d copy %5d written %5d size %5d\n",
            dma_pos, free, dma_len, s->left_till_irq, copy, written,
            s->block_size);
#endif

    while (s->left_till_irq <= 0) {
        s->left_till_irq = s->block_size + s->left_till_irq;
    }

    return dma_pos;
}

static void SB_audio_callback (void *opaque, int free)
{
    SB16State *s = opaque;
    s->audio_free = free;
}

static int sb16_post_load (void *opaque, int version_id)
{
    SB16State *s = opaque;

    if (s->voice) {
        AUD_close_out (&s->card, s->voice);
        s->voice = NULL;
    }

    if (s->dma_running) {
        if (s->freq) {
            struct audsettings as;

            s->audio_free = 0;

            as.freq = s->freq;
            as.nchannels = 1 << s->fmt_stereo;
            as.fmt = s->fmt;
            as.endianness = 0;

            s->voice = AUD_open_out (
                &s->card,
                s->voice,
                "sb16",
                s,
                SB_audio_callback,
                &as
                );
        }

        control (s, 1);
        speaker (s, s->speaker);
    }
    return 0;
}

static const VMStateDescription vmstate_sb16 = {
    .name = "sb16",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = sb16_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32 (irq, SB16State),
        VMSTATE_UINT32 (dma, SB16State),
        VMSTATE_UINT32 (hdma, SB16State),
        VMSTATE_UINT32 (port, SB16State),
        VMSTATE_UINT32 (ver, SB16State),
        VMSTATE_INT32 (in_index, SB16State),
        VMSTATE_INT32 (out_data_len, SB16State),
        VMSTATE_INT32 (fmt_stereo, SB16State),
        VMSTATE_INT32 (fmt_signed, SB16State),
        VMSTATE_INT32 (fmt_bits, SB16State),
        VMSTATE_UINT32 (fmt, SB16State),
        VMSTATE_INT32 (dma_auto, SB16State),
        VMSTATE_INT32 (block_size, SB16State),
        VMSTATE_INT32 (fifo, SB16State),
        VMSTATE_INT32 (freq, SB16State),
        VMSTATE_INT32 (time_const, SB16State),
        VMSTATE_INT32 (speaker, SB16State),
        VMSTATE_INT32 (needed_bytes, SB16State),
        VMSTATE_INT32 (cmd, SB16State),
        VMSTATE_INT32 (use_hdma, SB16State),
        VMSTATE_INT32 (highspeed, SB16State),
        VMSTATE_INT32 (can_write, SB16State),
        VMSTATE_INT32 (v2x6, SB16State),

        VMSTATE_UINT8 (csp_param, SB16State),
        VMSTATE_UINT8 (csp_value, SB16State),
        VMSTATE_UINT8 (csp_mode, SB16State),
        VMSTATE_UINT8 (csp_param, SB16State),
        VMSTATE_BUFFER (csp_regs, SB16State),
        VMSTATE_UINT8 (csp_index, SB16State),
        VMSTATE_BUFFER (csp_reg83, SB16State),
        VMSTATE_INT32 (csp_reg83r, SB16State),
        VMSTATE_INT32 (csp_reg83w, SB16State),

        VMSTATE_BUFFER (in2_data, SB16State),
        VMSTATE_BUFFER (out_data, SB16State),
        VMSTATE_UINT8 (test_reg, SB16State),
        VMSTATE_UINT8 (last_read_byte, SB16State),

        VMSTATE_INT32 (nzero, SB16State),
        VMSTATE_INT32 (left_till_irq, SB16State),
        VMSTATE_INT32 (dma_running, SB16State),
        VMSTATE_INT32 (bytes_per_second, SB16State),
        VMSTATE_INT32 (align, SB16State),

        VMSTATE_INT32 (mixer_nreg, SB16State),
        VMSTATE_BUFFER (mixer_regs, SB16State),

        VMSTATE_END_OF_LIST ()
    }
};

static const MemoryRegionPortio sb16_ioport_list[] = {
    {  4, 1, 1, .write = mixer_write_indexb },
    {  5, 1, 1, .read = mixer_read, .write = mixer_write_datab },
    {  6, 1, 1, .read = dsp_read, .write = dsp_write },
    { 10, 1, 1, .read = dsp_read },
    { 12, 1, 1, .write = dsp_write },
    { 12, 4, 1, .read = dsp_read },
    PORTIO_END_OF_LIST (),
};


static void sb16_initfn (Object *obj)
{
    SB16State *s = SB16 (obj);

    s->cmd = -1;
}

static void sb16_realizefn (DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE (dev);
    SB16State *s = SB16 (dev);
    IsaDmaClass *k;

    s->isa_hdma = isa_get_dma(isa_bus_from_device(isadev), s->hdma);
    s->isa_dma = isa_get_dma(isa_bus_from_device(isadev), s->dma);
    if (!s->isa_dma || !s->isa_hdma) {
        error_setg(errp, "ISA controller does not support DMA");
        return;
    }

    isa_init_irq (isadev, &s->pic, s->irq);

    s->mixer_regs[0x80] = magic_of_irq (s->irq);
    s->mixer_regs[0x81] = (1 << s->dma) | (1 << s->hdma);
    s->mixer_regs[0x82] = 2 << 5;

    s->csp_regs[5] = 1;
    s->csp_regs[9] = 0xf8;

    reset_mixer (s);
    s->aux_ts = timer_new_ns(QEMU_CLOCK_VIRTUAL, aux_timer, s);
    if (!s->aux_ts) {
        error_setg(errp, "warning: Could not create auxiliary timer");
    }

    isa_register_portio_list(isadev, &s->portio_list, s->port,
                             sb16_ioport_list, s, "sb16");

    k = ISADMA_GET_CLASS(s->isa_hdma);
    k->register_channel(s->isa_hdma, s->hdma, SB_read_DMA, s);

    k = ISADMA_GET_CLASS(s->isa_dma);
    k->register_channel(s->isa_dma, s->dma, SB_read_DMA, s);

    s->can_write = 1;

    AUD_register_card ("sb16", &s->card);
}

static int SB16_init (ISABus *bus)
{
    isa_create_simple (bus, TYPE_SB16);
    return 0;
}

static Property sb16_properties[] = {
    DEFINE_AUDIO_PROPERTIES(SB16State, card),
    DEFINE_PROP_UINT32 ("version", SB16State, ver,  0x0405), /* 4.5 */
    DEFINE_PROP_UINT32 ("iobase",  SB16State, port, 0x220),
    DEFINE_PROP_UINT32 ("irq",     SB16State, irq,  5),
    DEFINE_PROP_UINT32 ("dma",     SB16State, dma,  1),
    DEFINE_PROP_UINT32 ("dma16",   SB16State, hdma, 5),
    DEFINE_PROP_END_OF_LIST (),
};

static void sb16_class_initfn (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);

    dc->realize = sb16_realizefn;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Creative Sound Blaster 16";
    dc->vmsd = &vmstate_sb16;
    dc->props = sb16_properties;
}

static const TypeInfo sb16_info = {
    .name          = TYPE_SB16,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof (SB16State),
    .instance_init = sb16_initfn,
    .class_init    = sb16_class_initfn,
};

static void sb16_register_types (void)
{
    type_register_static (&sb16_info);
    isa_register_soundhw("sb16", "Creative Sound Blaster 16", SB16_init);
}

type_init (sb16_register_types)
