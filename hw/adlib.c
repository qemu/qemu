/*
 * QEMU Adlib emulation
 * 
 * Copyright (c) 2004 Vassili Karpov (malc)
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
#include "vl.h"

#define dolog(...) AUD_log ("adlib", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#ifdef USE_YMF262
#define HAS_YMF262 1
#include "ymf262.h"
void YMF262UpdateOneQEMU(int which, INT16 *dst, int length);
#define SHIFT 2
#else
#include "fmopl.h"
#define SHIFT 1
#endif

#ifdef _WIN32
#include <windows.h>
#define small_delay() Sleep (1)
#else
#define small_delay() usleep (1)
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
    int enabled;
    int active;
    int cparam;
    int64_t ticks;
    int bufpos;
    int16_t *mixbuf;
    double interval;
    QEMUTimer *ts, *opl_ts;
    SWVoice *voice;
    int left, pos, samples, bytes_per_second, old_free;
    int refcount;
#ifndef USE_YMF262
    FM_OPL *opl;
#endif
} AdlibState;

static AdlibState adlib;

static IO_WRITE_PROTO(adlib_write)
{
    AdlibState *s = opaque;
    int a = nport & 3;
    int status;

    s->ticks = qemu_get_clock (vm_clock);
    s->active = 1;
    AUD_enable (s->voice, 1);

#ifdef USE_YMF262
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

#ifdef USE_YMF262
    (void) s;
    data = YMF262Read (0, a);
#else
    data = OPLRead (s->opl, a);
#endif
    return data;
}

static void OPL_timer (void *opaque)
{
    AdlibState *s = opaque;
#ifdef USE_YMF262
    YMF262TimerOver (s->cparam >> 1, s->cparam & 1);
#else
    OPLTimerOver (s->opl, s->cparam);
#endif
    qemu_mod_timer (s->opl_ts, qemu_get_clock (vm_clock) + s->interval);
}

static void YMF262TimerHandler (int c, double interval_Sec)
{
    AdlibState *s = &adlib;
    if (interval_Sec == 0.0) {
        qemu_del_timer (s->opl_ts);
        return;
    }
    s->cparam = c;
    s->interval = ticks_per_sec * interval_Sec;
    qemu_mod_timer (s->opl_ts, qemu_get_clock (vm_clock) + s->interval);
    small_delay ();
}

static int write_audio (AdlibState *s, int samples)
{
    int net = 0;
    int ss = samples;
    while (samples) {
        int nbytes = samples << SHIFT;
        int wbytes = AUD_write (s->voice,
                                s->mixbuf + (s->pos << (SHIFT - 1)),
                                nbytes);
        int wsampl = wbytes >> SHIFT;
        samples -= wsampl;
        s->pos = (s->pos + wsampl) % s->samples;
        net += wsampl;
        if (!wbytes)
            break;
    }
    if (net > ss) {
        dolog ("WARNING: net > ss\n");
    }
    return net;
}

static void timer (void *opaque)
{
    AdlibState *s = opaque;
    int elapsed, samples, net = 0;

    if (s->refcount)
        dolog ("refcount=%d\n", s->refcount);

    s->refcount += 1;
    if (!(s->active && s->enabled))
        goto reset;

    AUD_run ();

    while (s->left) {
        int written = write_audio (s, s->left);
        net += written;
        if (!written)
            goto reset2;
        s->left -= written;
    }
    s->pos = 0;

    elapsed = AUD_calc_elapsed (s->voice);
    if (!elapsed)
        goto reset2;

    /* elapsed = AUD_get_free (s->voice); */
    samples = elapsed >> SHIFT;
    if (!samples)
        goto reset2;

    samples = audio_MIN (samples, s->samples - s->pos);
    if (s->left)
        dolog ("left=%d samples=%d elapsed=%d free=%d\n",
               s->left, samples, elapsed, AUD_get_free (s->voice));

    if (!samples)
        goto reset2;

#ifdef USE_YMF262
    YMF262UpdateOneQEMU (0, s->mixbuf + s->pos * 2, samples);
#else
    YM3812UpdateOne (s->opl, s->mixbuf + s->pos, samples);
#endif

    while (samples) {
        int written = write_audio (s, samples);
        net += written;
        if (!written)
            break;
        samples -= written;
    }
    if (!samples)
        s->pos = 0;
    s->left = samples;

reset2:
    AUD_adjust (s->voice, net << SHIFT);
reset:
    qemu_mod_timer (s->ts, qemu_get_clock (vm_clock) + ticks_per_sec / 1024);
    s->refcount -= 1;
}

static void Adlib_fini (AdlibState *s)
{
#ifdef USE_YMF262
    YMF262Shutdown ();
#else
    if (s->opl) {
        OPLDestroy (s->opl);
        s->opl = NULL;
    }
#endif

    if (s->opl_ts)
        qemu_free_timer (s->opl_ts);

    if (s->ts)
        qemu_free_timer (s->ts);

#define maybe_free(p) if (p) qemu_free (p)
    maybe_free (s->mixbuf);
#undef maybe_free

    s->active = 0;
    s->enabled = 0;
}

void Adlib_init (void)
{
    AdlibState *s = &adlib;

    memset (s, 0, sizeof (*s));

#ifdef USE_YMF262
    if (YMF262Init (1, 14318180, conf.freq)) {
        dolog ("YMF262Init %d failed\n", conf.freq);
        return;
    }
    else {
        YMF262SetTimerHandler (0, YMF262TimerHandler, 0);
        s->enabled = 1;
    }
#else
    s->opl = OPLCreate (OPL_TYPE_YM3812, 3579545, conf.freq);
    if (!s->opl) {
        dolog ("OPLCreate %d failed\n", conf.freq);
        return;
    }
    else {
        OPLSetTimerHandler (s->opl, YMF262TimerHandler, 0);
        s->enabled = 1;
    }
#endif

    s->opl_ts = qemu_new_timer (vm_clock, OPL_timer, s);
    if (!s->opl_ts) {
        dolog ("Can not get timer for adlib emulation\n");
        Adlib_fini (s);
        return;
    }

    s->ts = qemu_new_timer (vm_clock, timer, s);
    if (!s->opl_ts) {
        dolog ("Can not get timer for adlib emulation\n");
        Adlib_fini (s);
        return;
    }

    s->voice = AUD_open (s->voice, "adlib", conf.freq, SHIFT, AUD_FMT_S16);
    if (!s->voice) {
        Adlib_fini (s);
        return;
    }

    s->bytes_per_second = conf.freq << SHIFT;
    s->samples = AUD_get_buffer_size (s->voice) >> SHIFT;
    s->mixbuf = qemu_mallocz (s->samples << SHIFT);

    if (!s->mixbuf) {
        dolog ("not enough memory for adlib mixing buffer (%d)\n",
               s->samples << SHIFT);
        Adlib_fini (s);
        return;
    }
    register_ioport_read (0x388, 4, 1, adlib_read, s);
    register_ioport_write (0x388, 4, 1, adlib_write, s);

    register_ioport_read (conf.port, 4, 1, adlib_read, s);
    register_ioport_write (conf.port, 4, 1, adlib_write, s);

    register_ioport_read (conf.port + 8, 2, 1, adlib_read, s);
    register_ioport_write (conf.port + 8, 2, 1, adlib_write, s);

    qemu_mod_timer (s->ts, qemu_get_clock (vm_clock) + 1);
}
