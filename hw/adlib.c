/*
 * QEMU Proxy for OPL2/3 emulation by MAME team
 *
 * Copyright (c) 2004-2005 Vassili Karpov (malc)
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

//#define DEBUG

#define ADLIB_KILL_TIMERS 1

#ifdef DEBUG
#include "qemu-timer.h"
#endif

#define dolog(...) AUD_log ("adlib", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#ifdef HAS_YMF262
#include "ymf262.h"
void YMF262UpdateOneQEMU (int which, INT16 *dst, int length);
#define SHIFT 2
#else
#include "fmopl.h"
#define SHIFT 1
#endif

#define IO_READ_PROTO(name) \
    uint32_t name (void *opaque, uint32_t nport)
#define IO_WRITE_PROTO(name) \
    void name (void *opaque, uint32_t nport, uint32_t val)

static struct {
    int port;
    int freq;
} conf = {0x220, 44100};

typedef struct {
    QEMUSoundCard card;
    int ticking[2];
    int enabled;
    int active;
    int bufpos;
#ifdef DEBUG
    int64_t exp[2];
#endif
    int16_t *mixbuf;
    uint64_t dexp[2];
    SWVoiceOut *voice;
    int left, pos, samples;
    QEMUAudioTimeStamp ats;
#ifndef HAS_YMF262
    FM_OPL *opl;
#endif
} AdlibState;

static AdlibState glob_adlib;

static void adlib_stop_opl_timer (AdlibState *s, size_t n)
{
#ifdef HAS_YMF262
    YMF262TimerOver (0, n);
#else
    OPLTimerOver (s->opl, n);
#endif
    s->ticking[n] = 0;
}

static void adlib_kill_timers (AdlibState *s)
{
    size_t i;

    for (i = 0; i < 2; ++i) {
        if (s->ticking[i]) {
            uint64_t delta;

            delta = AUD_get_elapsed_usec_out (s->voice, &s->ats);
            ldebug (
                "delta = %f dexp = %f expired => %d\n",
                delta / 1000000.0,
                s->dexp[i] / 1000000.0,
                delta >= s->dexp[i]
                );
            if (ADLIB_KILL_TIMERS || delta >= s->dexp[i]) {
                adlib_stop_opl_timer (s, i);
                AUD_init_time_stamp_out (s->voice, &s->ats);
            }
        }
    }
}

static IO_WRITE_PROTO(adlib_write)
{
    AdlibState *s = opaque;
    int a = nport & 3;
    int status;

    s->active = 1;
    AUD_set_active_out (s->voice, 1);

    adlib_kill_timers (s);

#ifdef HAS_YMF262
    status = YMF262Write (0, a, val);
#else
    status = OPLWrite (s->opl, a, val);
#endif
}

static IO_READ_PROTO(adlib_read)
{
    AdlibState *s = opaque;
    uint8_t data;
    int a = nport & 3;

    adlib_kill_timers (s);

#ifdef HAS_YMF262
    data = YMF262Read (0, a);
#else
    data = OPLRead (s->opl, a);
#endif
    return data;
}

static void timer_handler (int c, double interval_Sec)
{
    AdlibState *s = &glob_adlib;
    unsigned n = c & 1;
#ifdef DEBUG
    double interval;
    int64_t exp;
#endif

    if (interval_Sec == 0.0) {
        s->ticking[n] = 0;
        return;
    }

    s->ticking[n] = 1;
#ifdef DEBUG
    interval = ticks_per_sec * interval_Sec;
    exp = qemu_get_clock (vm_clock) + interval;
    s->exp[n] = exp;
#endif

    s->dexp[n] = interval_Sec * 1000000.0;
    AUD_init_time_stamp_out (s->voice, &s->ats);
}

static int write_audio (AdlibState *s, int samples)
{
    int net = 0;
    int pos = s->pos;

    while (samples) {
        int nbytes, wbytes, wsampl;

        nbytes = samples << SHIFT;
        wbytes = AUD_write (
            s->voice,
            s->mixbuf + (pos << (SHIFT - 1)),
            nbytes
            );

        if (wbytes) {
            wsampl = wbytes >> SHIFT;

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

static void adlib_callback (void *opaque, int free)
{
    AdlibState *s = opaque;
    int samples, net = 0, to_play, written;

    samples = free >> SHIFT;
    if (!(s->active && s->enabled) || !samples) {
        return;
    }

    to_play = audio_MIN (s->left, samples);
    while (to_play) {
        written = write_audio (s, to_play);

        if (written) {
            s->left -= written;
            samples -= written;
            to_play -= written;
            s->pos = (s->pos + written) % s->samples;
        }
        else {
            return;
        }
    }

    samples = audio_MIN (samples, s->samples - s->pos);
    if (!samples) {
        return;
    }

#ifdef HAS_YMF262
    YMF262UpdateOneQEMU (0, s->mixbuf + s->pos * 2, samples);
#else
    YM3812UpdateOne (s->opl, s->mixbuf + s->pos, samples);
#endif

    while (samples) {
        written = write_audio (s, samples);

        if (written) {
            net += written;
            samples -= written;
            s->pos = (s->pos + written) % s->samples;
        }
        else {
            s->left = samples;
            return;
        }
    }
}

static void Adlib_fini (AdlibState *s)
{
#ifdef HAS_YMF262
    YMF262Shutdown ();
#else
    if (s->opl) {
        OPLDestroy (s->opl);
        s->opl = NULL;
    }
#endif

    if (s->mixbuf) {
        qemu_free (s->mixbuf);
    }

    s->active = 0;
    s->enabled = 0;
    AUD_remove_card (&s->card);
}

int Adlib_init (qemu_irq *pic)
{
    AudioState *audio = AUD_init();
    AdlibState *s = &glob_adlib;
    struct audsettings as;

#ifdef HAS_YMF262
    if (YMF262Init (1, 14318180, conf.freq)) {
        dolog ("YMF262Init %d failed\n", conf.freq);
        return -1;
    }
    else {
        YMF262SetTimerHandler (0, timer_handler, 0);
        s->enabled = 1;
    }
#else
    s->opl = OPLCreate (OPL_TYPE_YM3812, 3579545, conf.freq);
    if (!s->opl) {
        dolog ("OPLCreate %d failed\n", conf.freq);
        return -1;
    }
    else {
        OPLSetTimerHandler (s->opl, timer_handler, 0);
        s->enabled = 1;
    }
#endif

    as.freq = conf.freq;
    as.nchannels = SHIFT;
    as.fmt = AUD_FMT_S16;
    as.endianness = AUDIO_HOST_ENDIANNESS;

    AUD_register_card (audio, "adlib", &s->card);

    s->voice = AUD_open_out (
        &s->card,
        s->voice,
        "adlib",
        s,
        adlib_callback,
        &as
        );
    if (!s->voice) {
        Adlib_fini (s);
        return -1;
    }

    s->samples = AUD_get_buffer_size_out (s->voice) >> SHIFT;
    s->mixbuf = qemu_mallocz (s->samples << SHIFT);

    register_ioport_read (0x388, 4, 1, adlib_read, s);
    register_ioport_write (0x388, 4, 1, adlib_write, s);

    register_ioport_read (conf.port, 4, 1, adlib_read, s);
    register_ioport_write (conf.port, 4, 1, adlib_write, s);

    register_ioport_read (conf.port + 8, 2, 1, adlib_read, s);
    register_ioport_write (conf.port + 8, 2, 1, adlib_write, s);

    return 0;
}
