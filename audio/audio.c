/*
 * QEMU Audio subsystem
 * 
 * Copyright (c) 2003-2004 Vassili Karpov (malc)
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
#include <assert.h>
#include "vl.h"

#define USE_SDL_AUDIO
#define USE_WAV_AUDIO

#include "audio/audio_int.h"

#define dolog(...) AUD_log ("audio", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog (__VA_ARGS__)
#else
#define ldebug(...)
#endif

#define QC_AUDIO_DRV    "QEMU_AUDIO_DRV"
#define QC_VOICES       "QEMU_VOICES"
#define QC_FIXED_FORMAT "QEMU_FIXED_FORMAT"
#define QC_FIXED_FREQ   "QEMU_FIXED_FREQ"

static HWVoice *hw_voices;

AudioState audio_state = {
    1,                          /* use fixed settings */
    44100,                      /* fixed frequency */
    2,                          /* fixed channels */
    AUD_FMT_S16,                /* fixed format */
    1,                          /* number of hw voices */
    -1                          /* voice size */
};

/* http://www.df.lth.se/~john_e/gems/gem002d.html */
/* http://www.multi-platforms.com/Tips/PopCount.htm */
uint32_t popcount (uint32_t u)
{
    u = ((u&0x55555555) + ((u>>1)&0x55555555));
    u = ((u&0x33333333) + ((u>>2)&0x33333333));
    u = ((u&0x0f0f0f0f) + ((u>>4)&0x0f0f0f0f));
    u = ((u&0x00ff00ff) + ((u>>8)&0x00ff00ff));
    u = ( u&0x0000ffff) + (u>>16);
    return u;
}

inline uint32_t lsbindex (uint32_t u)
{
    return popcount ((u&-u)-1);
}

int audio_get_conf_int (const char *key, int defval)
{
    int val = defval;
    char *strval;

    strval = getenv (key);
    if (strval) {
        val = atoi (strval);
    }

    return val;
}

const char *audio_get_conf_str (const char *key, const char *defval)
{
    const char *val = getenv (key);
    if (!val)
        return defval;
    else
        return val;
}

void AUD_log (const char *cap, const char *fmt, ...)
{
    va_list ap;
    fprintf (stderr, "%s: ", cap);
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

/*
 * Soft Voice
 */
void pcm_sw_free_resources (SWVoice *sw)
{
    if (sw->buf) qemu_free (sw->buf);
    if (sw->rate) st_rate_stop (sw->rate);
    sw->buf = NULL;
    sw->rate = NULL;
}

int pcm_sw_alloc_resources (SWVoice *sw)
{
    sw->buf = qemu_mallocz (sw->hw->samples * sizeof (st_sample_t));
    if (!sw->buf)
        return -1;

    sw->rate = st_rate_start (sw->freq, sw->hw->freq);
    if (!sw->rate) {
        qemu_free (sw->buf);
        sw->buf = NULL;
        return -1;
    }
    return 0;
}

void pcm_sw_fini (SWVoice *sw)
{
    pcm_sw_free_resources (sw);
}

int pcm_sw_init (SWVoice *sw, HWVoice *hw, int freq,
                 int nchannels, audfmt_e fmt)
{
    int bits = 8, sign = 0;

    switch (fmt) {
    case AUD_FMT_S8:
        sign = 1;
    case AUD_FMT_U8:
        break;

    case AUD_FMT_S16:
        sign = 1;
    case AUD_FMT_U16:
        bits = 16;
        break;
    }

    sw->hw = hw;
    sw->freq = freq;
    sw->fmt = fmt;
    sw->nchannels = nchannels;
    sw->shift = (nchannels == 2) + (bits == 16);
    sw->align = (1 << sw->shift) - 1;
    sw->left = 0;
    sw->pos = 0;
    sw->wpos = 0;
    sw->live = 0;
    sw->ratio = (sw->hw->freq * ((int64_t) INT_MAX)) / sw->freq;
    sw->bytes_per_second = sw->freq << sw->shift;
    sw->conv = mixeng_conv[nchannels == 2][sign][bits == 16];

    pcm_sw_free_resources (sw);
    return pcm_sw_alloc_resources (sw);
}

/* Hard voice */
void pcm_hw_free_resources (HWVoice *hw)
{
    if (hw->mix_buf)
        qemu_free (hw->mix_buf);
    hw->mix_buf = NULL;
}

int pcm_hw_alloc_resources (HWVoice *hw)
{
    hw->mix_buf = qemu_mallocz (hw->samples * sizeof (st_sample_t));
    if (!hw->mix_buf)
        return -1;
    return 0;
}

void pcm_hw_fini (HWVoice *hw)
{
    if (hw->active) {
        ldebug ("pcm_hw_fini: %d %d %d\n", hw->freq, hw->nchannels, hw->fmt);
        pcm_hw_free_resources (hw);
        hw->pcm_ops->fini (hw);
        memset (hw, 0, audio_state.drv->voice_size);
    }
}

void pcm_hw_gc (HWVoice *hw)
{
    if (hw->nb_voices)
        return;

    pcm_hw_fini (hw);
}

int pcm_hw_get_live (HWVoice *hw)
{
    int i, alive = 0, live = hw->samples;

    for (i = 0; i < hw->nb_voices; i++) {
        if (hw->pvoice[i]->live) {
            live = audio_MIN (hw->pvoice[i]->live, live);
            alive += 1;
        }
    }

    if (alive)
        return live;
    else
        return -1;
}

int pcm_hw_get_live2 (HWVoice *hw, int *nb_active)
{
    int i, alive = 0, live = hw->samples;

    *nb_active = 0;
    for (i = 0; i < hw->nb_voices; i++) {
        if (hw->pvoice[i]->live) {
            if (hw->pvoice[i]->live < live) {
                *nb_active = hw->pvoice[i]->active != 0;
                live = hw->pvoice[i]->live;
            }
            alive += 1;
        }
    }

    if (alive)
        return live;
    else
        return -1;
}

void pcm_hw_dec_live (HWVoice *hw, int decr)
{
    int i;

    for (i = 0; i < hw->nb_voices; i++) {
        if (hw->pvoice[i]->live) {
            hw->pvoice[i]->live -= decr;
        }
    }
}

void pcm_hw_clear (HWVoice *hw, void *buf, int len)
{
    if (!len)
        return;

    switch (hw->fmt) {
    case AUD_FMT_S16:
    case AUD_FMT_S8:
        memset (buf, len << hw->shift, 0x00);
        break;

    case AUD_FMT_U8:
        memset (buf, len << hw->shift, 0x80);
        break;

    case AUD_FMT_U16:
        {
            unsigned int i;
            uint16_t *p = buf;
            int shift = hw->nchannels - 1;

            for (i = 0; i < len << shift; i++) {
                p[i] = INT16_MAX;
            }
        }
        break;
    }
}

int pcm_hw_write (SWVoice *sw, void *buf, int size)
{
    int hwsamples, samples, isamp, osamp, wpos, live, dead, left, swlim, blck;
    int ret = 0, pos = 0;
    if (!sw)
        return size;

    hwsamples = sw->hw->samples;
    samples = size >> sw->shift;

    if (!sw->live) {
        sw->wpos = sw->hw->rpos;
    }
    wpos = sw->wpos;
    live = sw->live;
    dead = hwsamples - live;
    swlim = (dead * ((int64_t) INT_MAX)) / sw->ratio;
    swlim = audio_MIN (swlim, samples);

    ldebug ("size=%d live=%d dead=%d swlim=%d wpos=%d\n",
           size, live, dead, swlim, wpos);
    if (swlim)
        sw->conv (sw->buf, buf, swlim);

    while (swlim) {
        dead = hwsamples - live;
        left = hwsamples - wpos;
        blck = audio_MIN (dead, left);
        if (!blck) {
            /* dolog ("swlim=%d\n", swlim); */
            break;
        }
        isamp = swlim;
        osamp = blck;
        st_rate_flow (sw->rate, sw->buf + pos, sw->hw->mix_buf + wpos, &isamp, &osamp);
        ret += isamp;
        swlim -= isamp;
        pos += isamp;
        live += osamp;
        wpos = (wpos + osamp) % hwsamples;
    }

    sw->wpos = wpos;
    sw->live = live;
    return ret << sw->shift;
}

int pcm_hw_init (HWVoice *hw, int freq, int nchannels, audfmt_e fmt)
{
    int sign = 0, bits = 8;

    pcm_hw_fini (hw);
    ldebug ("pcm_hw_init: %d %d %d\n", freq, nchannels, fmt);
    if (hw->pcm_ops->init (hw, freq, nchannels, fmt)) {
        memset (hw, 0, audio_state.drv->voice_size);
        return -1;
    }

    switch (hw->fmt) {
    case AUD_FMT_S8:
        sign = 1;
    case AUD_FMT_U8:
        break;

    case AUD_FMT_S16:
        sign = 1;
    case AUD_FMT_U16:
        bits = 16;
        break;
    }

    hw->nb_voices = 0;
    hw->active = 1;
    hw->shift = (hw->nchannels == 2) + (bits == 16);
    hw->bytes_per_second = hw->freq << hw->shift;
    hw->align = (1 << hw->shift) - 1;
    hw->samples = hw->bufsize >> hw->shift;
    hw->clip = mixeng_clip[hw->nchannels == 2][sign][bits == 16];
    if (pcm_hw_alloc_resources (hw)) {
        pcm_hw_fini (hw);
        return -1;
    }
    return 0;
}

static int dist (void *hw)
{
    if (hw) {
        return (((uint8_t *) hw - (uint8_t *) hw_voices)
                / audio_state.voice_size) + 1;
    }
    else {
        return 0;
    }
}

#define ADVANCE(hw) hw ? advance (hw, audio_state.voice_size) : hw_voices

HWVoice *pcm_hw_find_any (HWVoice *hw)
{
    int i = dist (hw);
    for (; i < audio_state.nb_hw_voices; i++) {
        hw = ADVANCE (hw);
        return hw;
    }
    return NULL;
}

HWVoice *pcm_hw_find_any_active (HWVoice *hw)
{
    int i = dist (hw);
    for (; i < audio_state.nb_hw_voices; i++) {
        hw = ADVANCE (hw);
        if (hw->active)
            return hw;
    }
    return NULL;
}

HWVoice *pcm_hw_find_any_active_enabled (HWVoice *hw)
{
    int i = dist (hw);
    for (; i < audio_state.nb_hw_voices; i++) {
        hw = ADVANCE (hw);
        if (hw->active && hw->enabled)
            return hw;
    }
    return NULL;
}

HWVoice *pcm_hw_find_any_passive (HWVoice *hw)
{
    int i = dist (hw);
    for (; i < audio_state.nb_hw_voices; i++) {
        hw = ADVANCE (hw);
        if (!hw->active)
            return hw;
    }
    return NULL;
}

HWVoice *pcm_hw_find_specific (HWVoice *hw, int freq,
                               int nchannels, audfmt_e fmt)
{
    while ((hw = pcm_hw_find_any_active (hw))) {
        if (hw->freq == freq &&
            hw->nchannels == nchannels &&
            hw->fmt == fmt)
            return hw;
    }
    return NULL;
}

HWVoice *pcm_hw_add (int freq, int nchannels, audfmt_e fmt)
{
    HWVoice *hw;

    if (audio_state.fixed_format) {
        freq = audio_state.fixed_freq;
        nchannels = audio_state.fixed_channels;
        fmt = audio_state.fixed_fmt;
    }

    hw = pcm_hw_find_specific (NULL, freq, nchannels, fmt);

    if (hw)
        return hw;

    hw = pcm_hw_find_any_passive (NULL);
    if (hw) {
        hw->pcm_ops = audio_state.drv->pcm_ops;
        if (!hw->pcm_ops)
            return NULL;

        if (pcm_hw_init (hw, freq, nchannels, fmt)) {
            pcm_hw_gc (hw);
            return NULL;
        }
        else
            return hw;
    }

    return pcm_hw_find_any (NULL);
}

int pcm_hw_add_sw (HWVoice *hw, SWVoice *sw)
{
    SWVoice **pvoice = qemu_mallocz ((hw->nb_voices + 1) * sizeof (sw));
    if (!pvoice)
        return -1;

    memcpy (pvoice, hw->pvoice, hw->nb_voices * sizeof (sw));
    qemu_free (hw->pvoice);
    hw->pvoice = pvoice;
    hw->pvoice[hw->nb_voices++] = sw;
    return 0;
}

int pcm_hw_del_sw (HWVoice *hw, SWVoice *sw)
{
    int i, j;
    if (hw->nb_voices > 1) {
        SWVoice **pvoice = qemu_mallocz ((hw->nb_voices - 1) * sizeof (sw));

        if (!pvoice) {
            dolog ("Can not maintain consistent state (not enough memory)\n");
            return -1;
        }

        for (i = 0, j = 0; i < hw->nb_voices; i++) {
            if (j >= hw->nb_voices - 1) {
                dolog ("Can not maintain consistent state "
                       "(invariant violated)\n");
                return -1;
            }
            if (hw->pvoice[i] != sw)
                pvoice[j++] = hw->pvoice[i];
        }
        qemu_free (hw->pvoice);
        hw->pvoice = pvoice;
        hw->nb_voices -= 1;
    }
    else {
        qemu_free (hw->pvoice);
        hw->pvoice = NULL;
        hw->nb_voices = 0;
    }
    return 0;
}

SWVoice *pcm_create_voice_pair (int freq, int nchannels, audfmt_e fmt)
{
    SWVoice *sw;
    HWVoice *hw;

    sw = qemu_mallocz (sizeof (*sw));
    if (!sw)
        goto err1;

    hw = pcm_hw_add (freq, nchannels, fmt);
    if (!hw)
        goto err2;

    if (pcm_hw_add_sw (hw, sw))
        goto err3;

    if (pcm_sw_init (sw, hw, freq, nchannels, fmt))
        goto err4;

    return sw;

err4:
    pcm_hw_del_sw (hw, sw);
err3:
    pcm_hw_gc (hw);
err2:
    qemu_free (sw);
err1:
    return NULL;
}

SWVoice *AUD_open (SWVoice *sw, const char *name,
                   int freq, int nchannels, audfmt_e fmt)
{
    if (!audio_state.drv) {
        return NULL;
    }

    if (sw && freq == sw->freq && sw->nchannels == nchannels && sw->fmt == fmt) {
        return sw;
    }

    if (sw) {
        ldebug ("Different format %s %d %d %d\n",
                name,
                sw->freq == freq,
                sw->nchannels == nchannels,
                sw->fmt == fmt);
    }

    if (nchannels != 1 && nchannels != 2) {
        dolog ("Bogus channel count %d for voice %s\n", nchannels, name);
        return NULL;
    }

    if (!audio_state.fixed_format && sw) {
        pcm_sw_fini (sw);
        pcm_hw_del_sw (sw->hw, sw);
        pcm_hw_gc (sw->hw);
        if (sw->name) {
            qemu_free (sw->name);
            sw->name = NULL;
        }
        qemu_free (sw);
        sw = NULL;
    }

    if (sw) {
        HWVoice *hw = sw->hw;
        if (!hw) {
            dolog ("Internal logic error voice %s has no hardware store\n",
                   name);
            return sw;
        }

        if (pcm_sw_init (sw, hw, freq, nchannels, fmt)) {
            pcm_sw_fini (sw);
            pcm_hw_del_sw (hw, sw);
            pcm_hw_gc (hw);
            if (sw->name) {
                qemu_free (sw->name);
                sw->name = NULL;
            }
            qemu_free (sw);
            return NULL;
        }
    }
    else {
        sw = pcm_create_voice_pair (freq, nchannels, fmt);
        if (!sw) {
            dolog ("Failed to create voice %s\n", name);
            return NULL;
        }
    }

    if (sw->name) {
        qemu_free (sw->name);
        sw->name = NULL;
    }
    sw->name = qemu_strdup (name);
    return sw;
}

void AUD_close (SWVoice *sw)
{
    if (!sw)
        return;

    pcm_sw_fini (sw);
    pcm_hw_del_sw (sw->hw, sw);
    pcm_hw_gc (sw->hw);
    if (sw->name) {
        qemu_free (sw->name);
        sw->name = NULL;
    }
    qemu_free (sw);
}

int AUD_write (SWVoice *sw, void *buf, int size)
{
    int bytes;

    if (!sw->hw->enabled)
        dolog ("Writing to disabled voice %s\n", sw->name);
    bytes = sw->hw->pcm_ops->write (sw, buf, size);
    return bytes;
}

void AUD_run (void)
{
    HWVoice *hw = NULL;

    while ((hw = pcm_hw_find_any_active_enabled (hw))) {
        int i;
        if (hw->pending_disable && pcm_hw_get_live (hw) <= 0) {
            hw->enabled = 0;
            hw->pcm_ops->ctl (hw, VOICE_DISABLE);
            for (i = 0; i < hw->nb_voices; i++) {
                hw->pvoice[i]->live = 0;
                /* hw->pvoice[i]->old_ticks = 0; */
            }
            continue;
        }

        hw->pcm_ops->run (hw);
        assert (hw->rpos < hw->samples);
        for (i = 0; i < hw->nb_voices; i++) {
            SWVoice *sw = hw->pvoice[i];
            if (!sw->active && !sw->live && sw->old_ticks) {
                int64_t delta = qemu_get_clock (vm_clock) - sw->old_ticks;
                if (delta > audio_state.ticks_threshold) {
                    ldebug ("resetting old_ticks for %s\n", sw->name);
                    sw->old_ticks = 0;
                }
            }
        }
    }
}

int AUD_get_free (SWVoice *sw)
{
    int free;

    if (!sw)
        return 4096;

    free = ((sw->hw->samples - sw->live) << sw->hw->shift) * sw->ratio
        / INT_MAX;

    free &= ~sw->hw->align;
    if (!free) return 0;

    return free;
}

int AUD_get_buffer_size (SWVoice *sw)
{
    return sw->hw->bufsize;
}

void AUD_adjust (SWVoice *sw, int bytes)
{
    if (!sw)
        return;
    sw->old_ticks += (ticks_per_sec * (int64_t) bytes) / sw->bytes_per_second;
}

void AUD_reset (SWVoice *sw)
{
    sw->active = 0;
    sw->old_ticks = 0;
}

int AUD_calc_elapsed (SWVoice *sw)
{
    int64_t now, delta, bytes;
    int dead, swlim;

    if (!sw)
        return 0;

    now = qemu_get_clock (vm_clock);
    delta = now - sw->old_ticks;
    bytes = (delta * sw->bytes_per_second) / ticks_per_sec;
    if (delta < 0) {
        dolog ("whoops delta(<0)=%lld\n", delta);
        return 0;
    }

    dead = sw->hw->samples - sw->live;
    swlim = ((dead * (int64_t) INT_MAX) / sw->ratio);

    if (bytes > swlim) {
        return swlim;
    }
    else {
        return bytes;
    }
}

void AUD_enable (SWVoice *sw, int on)
{
    int i;
    HWVoice *hw;

    if (!sw)
        return;

    hw = sw->hw;
    if (on) {
        if (!sw->live)
            sw->wpos = sw->hw->rpos;
        if (!sw->old_ticks) {
            sw->old_ticks = qemu_get_clock (vm_clock);
        }
    }

    if (sw->active != on) {
        if (on) {
            hw->pending_disable = 0;
            if (!hw->enabled) {
                hw->enabled = 1;
                for (i = 0; i < hw->nb_voices; i++) {
                    ldebug ("resetting voice\n");
                    sw = hw->pvoice[i];
                    sw->old_ticks = qemu_get_clock (vm_clock);
                }
                hw->pcm_ops->ctl (hw, VOICE_ENABLE);
            }
        }
        else {
            if (hw->enabled && !hw->pending_disable) {
                int nb_active = 0;
                for (i = 0; i < hw->nb_voices; i++) {
                    nb_active += hw->pvoice[i]->active != 0;
                }

                if (nb_active == 1) {
                    hw->pending_disable = 1;
                }
            }
        }
        sw->active = on;
    }
}

static struct audio_output_driver *drvtab[] = {
#ifdef CONFIG_OSS
    &oss_output_driver,
#endif
#ifdef USE_FMOD_AUDIO
    &fmod_output_driver,
#endif
#ifdef CONFIG_SDL
    &sdl_output_driver,
#endif
    &no_output_driver,
#ifdef USE_WAV_AUDIO
    &wav_output_driver,
#endif
};

static int voice_init (struct audio_output_driver *drv)
{
    audio_state.opaque = drv->init ();
    if (audio_state.opaque) {
        if (audio_state.nb_hw_voices > drv->max_voices) {
            dolog ("`%s' does not support %d multiple hardware channels\n"
                   "Resetting to %d\n",
                   drv->name, audio_state.nb_hw_voices, drv->max_voices);
            audio_state.nb_hw_voices = drv->max_voices;
        }
        hw_voices = qemu_mallocz (audio_state.nb_hw_voices * drv->voice_size);
        if (hw_voices) {
            audio_state.drv = drv;
            return 1;
        }
        else {
            dolog ("Not enough memory for %d `%s' voices (each %d bytes)\n",
                   audio_state.nb_hw_voices, drv->name, drv->voice_size);
            drv->fini (audio_state.opaque);
            return 0;
        }
    }
    else {
        dolog ("Could not init `%s' audio\n", drv->name);
        return 0;
    }
}

static void audio_vm_stop_handler (void *opaque, int reason)
{
    HWVoice *hw = NULL;

    while ((hw = pcm_hw_find_any (hw))) {
        if (!hw->pcm_ops)
            continue;

        hw->pcm_ops->ctl (hw, reason ? VOICE_ENABLE : VOICE_DISABLE);
    }
}

static void audio_atexit (void)
{
    HWVoice *hw = NULL;

    while ((hw = pcm_hw_find_any (hw))) {
        if (!hw->pcm_ops)
            continue;

        hw->pcm_ops->ctl (hw, VOICE_DISABLE);
        hw->pcm_ops->fini (hw);
    }
    audio_state.drv->fini (audio_state.opaque);
}

static void audio_save (QEMUFile *f, void *opaque)
{
}

static int audio_load (QEMUFile *f, void *opaque, int version_id)
{
    if (version_id != 1)
        return -EINVAL;

    return 0;
}

void AUD_init (void)
{
    int i;
    int done = 0;
    const char *drvname;

    audio_state.fixed_format =
        !!audio_get_conf_int (QC_FIXED_FORMAT, audio_state.fixed_format);
    audio_state.fixed_freq =
        audio_get_conf_int (QC_FIXED_FREQ, audio_state.fixed_freq);
    audio_state.nb_hw_voices =
        audio_get_conf_int (QC_VOICES, audio_state.nb_hw_voices);

    if (audio_state.nb_hw_voices <= 0) {
        dolog ("Bogus number of voices %d, resetting to 1\n",
               audio_state.nb_hw_voices);
    }

    drvname = audio_get_conf_str (QC_AUDIO_DRV, NULL);
    if (drvname) {
        int found = 0;
        for (i = 0; i < sizeof (drvtab) / sizeof (drvtab[0]); i++) {
            if (!strcmp (drvname, drvtab[i]->name)) {
                done = voice_init (drvtab[i]);
                found = 1;
                break;
            }
        }
        if (!found) {
            dolog ("Unknown audio driver `%s'\n", drvname);
        }
    }

    qemu_add_vm_stop_handler (audio_vm_stop_handler, NULL);
    atexit (audio_atexit);

    if (!done) {
        for (i = 0; !done && i < sizeof (drvtab) / sizeof (drvtab[0]); i++) {
            if (drvtab[i]->can_be_default)
                done = voice_init (drvtab[i]);
        }
    }

    audio_state.ticks_threshold = ticks_per_sec / 50;
    audio_state.freq_threshold = 100;

    register_savevm ("audio", 0, 1, audio_save, audio_load, NULL);
    if (!done) {
        dolog ("Can not initialize audio subsystem\n");
        voice_init (&no_output_driver);
    }
}
