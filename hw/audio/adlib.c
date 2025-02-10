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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

//#define DEBUG

#define ADLIB_KILL_TIMERS 1

#define ADLIB_DESC "Yamaha YM3812 (OPL2)"

#ifdef DEBUG
#include "qemu/timer.h"
#endif

#define dolog(...) AUD_log ("adlib", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#include "fmopl.h"
#define SHIFT 1

#define TYPE_ADLIB "adlib"
OBJECT_DECLARE_SIMPLE_TYPE(AdlibState, ADLIB)

struct AdlibState {
    ISADevice parent_obj;

    QEMUSoundCard card;
    uint32_t freq;
    uint32_t port;
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
    FM_OPL *opl;
    PortioList port_list;
};

static void adlib_stop_opl_timer (AdlibState *s, size_t n)
{
    OPLTimerOver (s->opl, n);
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

static void adlib_write(void *opaque, uint32_t nport, uint32_t val)
{
    AdlibState *s = opaque;
    int a = nport & 3;

    s->active = 1;
    AUD_set_active_out (s->voice, 1);

    adlib_kill_timers (s);

    OPLWrite (s->opl, a, val);
}

static uint32_t adlib_read(void *opaque, uint32_t nport)
{
    AdlibState *s = opaque;
    int a = nport & 3;

    adlib_kill_timers (s);
    return OPLRead (s->opl, a);
}

static void timer_handler (void *opaque, int c, double interval_Sec)
{
    AdlibState *s = opaque;
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
    interval = NANOSECONDS_PER_SECOND * interval_Sec;
    exp = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + interval;
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
    int samples, to_play, written;

    samples = free >> SHIFT;
    if (!(s->active && s->enabled) || !samples) {
        return;
    }

    to_play = MIN (s->left, samples);
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

    samples = MIN (samples, s->samples - s->pos);
    if (!samples) {
        return;
    }

    YM3812UpdateOne (s->opl, s->mixbuf + s->pos, samples);

    while (samples) {
        written = write_audio (s, samples);

        if (written) {
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
    if (s->opl) {
        OPLDestroy (s->opl);
        s->opl = NULL;
    }

    g_free(s->mixbuf);

    s->active = 0;
    s->enabled = 0;
    AUD_remove_card (&s->card);
}

static MemoryRegionPortio adlib_portio_list[] = {
    { 0, 4, 1, .read = adlib_read, .write = adlib_write, },
    { 0, 2, 1, .read = adlib_read, .write = adlib_write, },
    { 0x388, 4, 1, .read = adlib_read, .write = adlib_write, },
    PORTIO_END_OF_LIST(),
};

static void adlib_realizefn (DeviceState *dev, Error **errp)
{
    AdlibState *s = ADLIB(dev);
    struct audsettings as;

    if (!AUD_register_card ("adlib", &s->card, errp)) {
        return;
    }

    s->opl = OPLCreate (3579545, s->freq);
    if (!s->opl) {
        error_setg (errp, "OPLCreate %d failed", s->freq);
        return;
    }
    else {
        OPLSetTimerHandler(s->opl, timer_handler, s);
        s->enabled = 1;
    }

    as.freq = s->freq;
    as.nchannels = SHIFT;
    as.fmt = AUDIO_FORMAT_S16;
    as.endianness = AUDIO_HOST_ENDIANNESS;

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
        error_setg (errp, "Initializing audio voice failed");
        return;
    }

    s->samples = AUD_get_buffer_size_out (s->voice) >> SHIFT;
    s->mixbuf = g_malloc0 (s->samples << SHIFT);

    adlib_portio_list[0].offset = s->port;
    adlib_portio_list[1].offset = s->port + 8;
    portio_list_init (&s->port_list, OBJECT(s), adlib_portio_list, s, "adlib");
    portio_list_add (&s->port_list, isa_address_space_io(&s->parent_obj), 0);
}

static const Property adlib_properties[] = {
    DEFINE_AUDIO_PROPERTIES(AdlibState, card),
    DEFINE_PROP_UINT32 ("iobase",  AdlibState, port, 0x220),
    DEFINE_PROP_UINT32 ("freq",    AdlibState, freq,  44100),
};

static void adlib_class_initfn (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);

    dc->realize = adlib_realizefn;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = ADLIB_DESC;
    device_class_set_props(dc, adlib_properties);
}

static const TypeInfo adlib_info = {
    .name          = TYPE_ADLIB,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof (AdlibState),
    .class_init    = adlib_class_initfn,
};

static void adlib_register_types (void)
{
    type_register_static (&adlib_info);
    deprecated_register_soundhw("adlib", ADLIB_DESC, 1, TYPE_ADLIB);
}

type_init (adlib_register_types)
