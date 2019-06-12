/*
 * QEMU Audio subsystem
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
#include "hw/hw.h"
#include "audio.h"
#include "monitor/monitor.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-audio.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "sysemu/replay.h"
#include "trace.h"

#define AUDIO_CAP "audio"
#include "audio_int.h"

/* #define DEBUG_LIVE */
/* #define DEBUG_OUT */
/* #define DEBUG_CAPTURE */
/* #define DEBUG_POLL */

#define SW_NAME(sw) (sw)->name ? (sw)->name : "unknown"


/* Order of CONFIG_AUDIO_DRIVERS is import.
   The 1st one is the one used by default, that is the reason
    that we generate the list.
*/
const char *audio_prio_list[] = {
    "spice",
    CONFIG_AUDIO_DRIVERS
    "none",
    "wav",
    NULL
};

static QLIST_HEAD(, audio_driver) audio_drivers;
static AudiodevListHead audiodevs = QSIMPLEQ_HEAD_INITIALIZER(audiodevs);

void audio_driver_register(audio_driver *drv)
{
    QLIST_INSERT_HEAD(&audio_drivers, drv, next);
}

audio_driver *audio_driver_lookup(const char *name)
{
    struct audio_driver *d;

    QLIST_FOREACH(d, &audio_drivers, next) {
        if (strcmp(name, d->name) == 0) {
            return d;
        }
    }

    audio_module_load_one(name);
    QLIST_FOREACH(d, &audio_drivers, next) {
        if (strcmp(name, d->name) == 0) {
            return d;
        }
    }

    return NULL;
}

static AudioState glob_audio_state;

const struct mixeng_volume nominal_volume = {
    .mute = 0,
#ifdef FLOAT_MIXENG
    .r = 1.0,
    .l = 1.0,
#else
    .r = 1ULL << 32,
    .l = 1ULL << 32,
#endif
};

#ifdef AUDIO_IS_FLAWLESS_AND_NO_CHECKS_ARE_REQURIED
#error No its not
#else
int audio_bug (const char *funcname, int cond)
{
    if (cond) {
        static int shown;

        AUD_log (NULL, "A bug was just triggered in %s\n", funcname);
        if (!shown) {
            shown = 1;
            AUD_log (NULL, "Save all your work and restart without audio\n");
            AUD_log (NULL, "I am sorry\n");
        }
        AUD_log (NULL, "Context:\n");

#if defined AUDIO_BREAKPOINT_ON_BUG
#  if defined HOST_I386
#    if defined __GNUC__
        __asm__ ("int3");
#    elif defined _MSC_VER
        _asm _emit 0xcc;
#    else
        abort ();
#    endif
#  else
        abort ();
#  endif
#endif
    }

    return cond;
}
#endif

static inline int audio_bits_to_index (int bits)
{
    switch (bits) {
    case 8:
        return 0;

    case 16:
        return 1;

    case 32:
        return 2;

    default:
        audio_bug ("bits_to_index", 1);
        AUD_log (NULL, "invalid bits %d\n", bits);
        return 0;
    }
}

void *audio_calloc (const char *funcname, int nmemb, size_t size)
{
    int cond;
    size_t len;

    len = nmemb * size;
    cond = !nmemb || !size;
    cond |= nmemb < 0;
    cond |= len < size;

    if (audio_bug ("audio_calloc", cond)) {
        AUD_log (NULL, "%s passed invalid arguments to audio_calloc\n",
                 funcname);
        AUD_log (NULL, "nmemb=%d size=%zu (len=%zu)\n", nmemb, size, len);
        return NULL;
    }

    return g_malloc0 (len);
}

void AUD_vlog (const char *cap, const char *fmt, va_list ap)
{
    if (cap) {
        fprintf(stderr, "%s: ", cap);
    }

    vfprintf(stderr, fmt, ap);
}

void AUD_log (const char *cap, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (cap, fmt, ap);
    va_end (ap);
}

static void audio_print_settings (struct audsettings *as)
{
    dolog ("frequency=%d nchannels=%d fmt=", as->freq, as->nchannels);

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
        AUD_log (NULL, "S8");
        break;
    case AUDIO_FORMAT_U8:
        AUD_log (NULL, "U8");
        break;
    case AUDIO_FORMAT_S16:
        AUD_log (NULL, "S16");
        break;
    case AUDIO_FORMAT_U16:
        AUD_log (NULL, "U16");
        break;
    case AUDIO_FORMAT_S32:
        AUD_log (NULL, "S32");
        break;
    case AUDIO_FORMAT_U32:
        AUD_log (NULL, "U32");
        break;
    default:
        AUD_log (NULL, "invalid(%d)", as->fmt);
        break;
    }

    AUD_log (NULL, " endianness=");
    switch (as->endianness) {
    case 0:
        AUD_log (NULL, "little");
        break;
    case 1:
        AUD_log (NULL, "big");
        break;
    default:
        AUD_log (NULL, "invalid");
        break;
    }
    AUD_log (NULL, "\n");
}

static int audio_validate_settings (struct audsettings *as)
{
    int invalid;

    invalid = as->nchannels != 1 && as->nchannels != 2;
    invalid |= as->endianness != 0 && as->endianness != 1;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
    case AUDIO_FORMAT_U8:
    case AUDIO_FORMAT_S16:
    case AUDIO_FORMAT_U16:
    case AUDIO_FORMAT_S32:
    case AUDIO_FORMAT_U32:
        break;
    default:
        invalid = 1;
        break;
    }

    invalid |= as->freq <= 0;
    return invalid ? -1 : 0;
}

static int audio_pcm_info_eq (struct audio_pcm_info *info, struct audsettings *as)
{
    int bits = 8, sign = 0;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
        sign = 1;
        /* fall through */
    case AUDIO_FORMAT_U8:
        break;

    case AUDIO_FORMAT_S16:
        sign = 1;
        /* fall through */
    case AUDIO_FORMAT_U16:
        bits = 16;
        break;

    case AUDIO_FORMAT_S32:
        sign = 1;
        /* fall through */
    case AUDIO_FORMAT_U32:
        bits = 32;
        break;

    default:
        abort();
    }
    return info->freq == as->freq
        && info->nchannels == as->nchannels
        && info->sign == sign
        && info->bits == bits
        && info->swap_endianness == (as->endianness != AUDIO_HOST_ENDIANNESS);
}

void audio_pcm_init_info (struct audio_pcm_info *info, struct audsettings *as)
{
    int bits = 8, sign = 0, shift = 0;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
        sign = 1;
    case AUDIO_FORMAT_U8:
        break;

    case AUDIO_FORMAT_S16:
        sign = 1;
    case AUDIO_FORMAT_U16:
        bits = 16;
        shift = 1;
        break;

    case AUDIO_FORMAT_S32:
        sign = 1;
    case AUDIO_FORMAT_U32:
        bits = 32;
        shift = 2;
        break;

    default:
        abort();
    }

    info->freq = as->freq;
    info->bits = bits;
    info->sign = sign;
    info->nchannels = as->nchannels;
    info->shift = (as->nchannels == 2) + shift;
    info->align = (1 << info->shift) - 1;
    info->bytes_per_second = info->freq << info->shift;
    info->swap_endianness = (as->endianness != AUDIO_HOST_ENDIANNESS);
}

void audio_pcm_info_clear_buf (struct audio_pcm_info *info, void *buf, int len)
{
    if (!len) {
        return;
    }

    if (info->sign) {
        memset (buf, 0x00, len << info->shift);
    }
    else {
        switch (info->bits) {
        case 8:
            memset (buf, 0x80, len << info->shift);
            break;

        case 16:
            {
                int i;
                uint16_t *p = buf;
                int shift = info->nchannels - 1;
                short s = INT16_MAX;

                if (info->swap_endianness) {
                    s = bswap16 (s);
                }

                for (i = 0; i < len << shift; i++) {
                    p[i] = s;
                }
            }
            break;

        case 32:
            {
                int i;
                uint32_t *p = buf;
                int shift = info->nchannels - 1;
                int32_t s = INT32_MAX;

                if (info->swap_endianness) {
                    s = bswap32 (s);
                }

                for (i = 0; i < len << shift; i++) {
                    p[i] = s;
                }
            }
            break;

        default:
            AUD_log (NULL, "audio_pcm_info_clear_buf: invalid bits %d\n",
                     info->bits);
            break;
        }
    }
}

/*
 * Capture
 */
static void noop_conv (struct st_sample *dst, const void *src, int samples)
{
    (void) src;
    (void) dst;
    (void) samples;
}

static CaptureVoiceOut *audio_pcm_capture_find_specific (
    struct audsettings *as
    )
{
    CaptureVoiceOut *cap;
    AudioState *s = &glob_audio_state;

    for (cap = s->cap_head.lh_first; cap; cap = cap->entries.le_next) {
        if (audio_pcm_info_eq (&cap->hw.info, as)) {
            return cap;
        }
    }
    return NULL;
}

static void audio_notify_capture (CaptureVoiceOut *cap, audcnotification_e cmd)
{
    struct capture_callback *cb;

#ifdef DEBUG_CAPTURE
    dolog ("notification %d sent\n", cmd);
#endif
    for (cb = cap->cb_head.lh_first; cb; cb = cb->entries.le_next) {
        cb->ops.notify (cb->opaque, cmd);
    }
}

static void audio_capture_maybe_changed (CaptureVoiceOut *cap, int enabled)
{
    if (cap->hw.enabled != enabled) {
        audcnotification_e cmd;
        cap->hw.enabled = enabled;
        cmd = enabled ? AUD_CNOTIFY_ENABLE : AUD_CNOTIFY_DISABLE;
        audio_notify_capture (cap, cmd);
    }
}

static void audio_recalc_and_notify_capture (CaptureVoiceOut *cap)
{
    HWVoiceOut *hw = &cap->hw;
    SWVoiceOut *sw;
    int enabled = 0;

    for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
        if (sw->active) {
            enabled = 1;
            break;
        }
    }
    audio_capture_maybe_changed (cap, enabled);
}

static void audio_detach_capture (HWVoiceOut *hw)
{
    SWVoiceCap *sc = hw->cap_head.lh_first;

    while (sc) {
        SWVoiceCap *sc1 = sc->entries.le_next;
        SWVoiceOut *sw = &sc->sw;
        CaptureVoiceOut *cap = sc->cap;
        int was_active = sw->active;

        if (sw->rate) {
            st_rate_stop (sw->rate);
            sw->rate = NULL;
        }

        QLIST_REMOVE (sw, entries);
        QLIST_REMOVE (sc, entries);
        g_free (sc);
        if (was_active) {
            /* We have removed soft voice from the capture:
               this might have changed the overall status of the capture
               since this might have been the only active voice */
            audio_recalc_and_notify_capture (cap);
        }
        sc = sc1;
    }
}

static int audio_attach_capture (HWVoiceOut *hw)
{
    AudioState *s = &glob_audio_state;
    CaptureVoiceOut *cap;

    audio_detach_capture (hw);
    for (cap = s->cap_head.lh_first; cap; cap = cap->entries.le_next) {
        SWVoiceCap *sc;
        SWVoiceOut *sw;
        HWVoiceOut *hw_cap = &cap->hw;

        sc = g_malloc0(sizeof(*sc));

        sc->cap = cap;
        sw = &sc->sw;
        sw->hw = hw_cap;
        sw->info = hw->info;
        sw->empty = 1;
        sw->active = hw->enabled;
        sw->conv = noop_conv;
        sw->ratio = ((int64_t) hw_cap->info.freq << 32) / sw->info.freq;
        sw->vol = nominal_volume;
        sw->rate = st_rate_start (sw->info.freq, hw_cap->info.freq);
        if (!sw->rate) {
            dolog ("Could not start rate conversion for `%s'\n", SW_NAME (sw));
            g_free (sw);
            return -1;
        }
        QLIST_INSERT_HEAD (&hw_cap->sw_head, sw, entries);
        QLIST_INSERT_HEAD (&hw->cap_head, sc, entries);
#ifdef DEBUG_CAPTURE
        sw->name = g_strdup_printf ("for %p %d,%d,%d",
                                    hw, sw->info.freq, sw->info.bits,
                                    sw->info.nchannels);
        dolog ("Added %s active = %d\n", sw->name, sw->active);
#endif
        if (sw->active) {
            audio_capture_maybe_changed (cap, 1);
        }
    }
    return 0;
}

/*
 * Hard voice (capture)
 */
static int audio_pcm_hw_find_min_in (HWVoiceIn *hw)
{
    SWVoiceIn *sw;
    int m = hw->total_samples_captured;

    for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
        if (sw->active) {
            m = audio_MIN (m, sw->total_hw_samples_acquired);
        }
    }
    return m;
}

int audio_pcm_hw_get_live_in (HWVoiceIn *hw)
{
    int live = hw->total_samples_captured - audio_pcm_hw_find_min_in (hw);
    if (audio_bug(__func__, live < 0 || live > hw->samples)) {
        dolog ("live=%d hw->samples=%d\n", live, hw->samples);
        return 0;
    }
    return live;
}

int audio_pcm_hw_clip_out (HWVoiceOut *hw, void *pcm_buf,
                           int live, int pending)
{
    int left = hw->samples - pending;
    int len = audio_MIN (left, live);
    int clipped = 0;

    while (len) {
        struct st_sample *src = hw->mix_buf + hw->rpos;
        uint8_t *dst = advance (pcm_buf, hw->rpos << hw->info.shift);
        int samples_till_end_of_buf = hw->samples - hw->rpos;
        int samples_to_clip = audio_MIN (len, samples_till_end_of_buf);

        hw->clip (dst, src, samples_to_clip);

        hw->rpos = (hw->rpos + samples_to_clip) % hw->samples;
        len -= samples_to_clip;
        clipped += samples_to_clip;
    }
    return clipped;
}

/*
 * Soft voice (capture)
 */
static int audio_pcm_sw_get_rpos_in (SWVoiceIn *sw)
{
    HWVoiceIn *hw = sw->hw;
    int live = hw->total_samples_captured - sw->total_hw_samples_acquired;
    int rpos;

    if (audio_bug(__func__, live < 0 || live > hw->samples)) {
        dolog ("live=%d hw->samples=%d\n", live, hw->samples);
        return 0;
    }

    rpos = hw->wpos - live;
    if (rpos >= 0) {
        return rpos;
    }
    else {
        return hw->samples + rpos;
    }
}

int audio_pcm_sw_read (SWVoiceIn *sw, void *buf, int size)
{
    HWVoiceIn *hw = sw->hw;
    int samples, live, ret = 0, swlim, isamp, osamp, rpos, total = 0;
    struct st_sample *src, *dst = sw->buf;

    rpos = audio_pcm_sw_get_rpos_in (sw) % hw->samples;

    live = hw->total_samples_captured - sw->total_hw_samples_acquired;
    if (audio_bug(__func__, live < 0 || live > hw->samples)) {
        dolog ("live_in=%d hw->samples=%d\n", live, hw->samples);
        return 0;
    }

    samples = size >> sw->info.shift;
    if (!live) {
        return 0;
    }

    swlim = (live * sw->ratio) >> 32;
    swlim = audio_MIN (swlim, samples);

    while (swlim) {
        src = hw->conv_buf + rpos;
        isamp = hw->wpos - rpos;
        /* XXX: <= ? */
        if (isamp <= 0) {
            isamp = hw->samples - rpos;
        }

        if (!isamp) {
            break;
        }
        osamp = swlim;

        if (audio_bug(__func__, osamp < 0)) {
            dolog ("osamp=%d\n", osamp);
            return 0;
        }

        st_rate_flow (sw->rate, src, dst, &isamp, &osamp);
        swlim -= osamp;
        rpos = (rpos + isamp) % hw->samples;
        dst += osamp;
        ret += osamp;
        total += isamp;
    }

    if (!(hw->ctl_caps & VOICE_VOLUME_CAP)) {
        mixeng_volume (sw->buf, ret, &sw->vol);
    }

    sw->clip (buf, sw->buf, ret);
    sw->total_hw_samples_acquired += total;
    return ret << sw->info.shift;
}

/*
 * Hard voice (playback)
 */
static int audio_pcm_hw_find_min_out (HWVoiceOut *hw, int *nb_livep)
{
    SWVoiceOut *sw;
    int m = INT_MAX;
    int nb_live = 0;

    for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
        if (sw->active || !sw->empty) {
            m = audio_MIN (m, sw->total_hw_samples_mixed);
            nb_live += 1;
        }
    }

    *nb_livep = nb_live;
    return m;
}

static int audio_pcm_hw_get_live_out (HWVoiceOut *hw, int *nb_live)
{
    int smin;
    int nb_live1;

    smin = audio_pcm_hw_find_min_out (hw, &nb_live1);
    if (nb_live) {
        *nb_live = nb_live1;
    }

    if (nb_live1) {
        int live = smin;

        if (audio_bug(__func__, live < 0 || live > hw->samples)) {
            dolog ("live=%d hw->samples=%d\n", live, hw->samples);
            return 0;
        }
        return live;
    }
    return 0;
}

/*
 * Soft voice (playback)
 */
int audio_pcm_sw_write (SWVoiceOut *sw, void *buf, int size)
{
    int hwsamples, samples, isamp, osamp, wpos, live, dead, left, swlim, blck;
    int ret = 0, pos = 0, total = 0;

    if (!sw) {
        return size;
    }

    hwsamples = sw->hw->samples;

    live = sw->total_hw_samples_mixed;
    if (audio_bug(__func__, live < 0 || live > hwsamples)) {
        dolog ("live=%d hw->samples=%d\n", live, hwsamples);
        return 0;
    }

    if (live == hwsamples) {
#ifdef DEBUG_OUT
        dolog ("%s is full %d\n", sw->name, live);
#endif
        return 0;
    }

    wpos = (sw->hw->rpos + live) % hwsamples;
    samples = size >> sw->info.shift;

    dead = hwsamples - live;
    swlim = ((int64_t) dead << 32) / sw->ratio;
    swlim = audio_MIN (swlim, samples);
    if (swlim) {
        sw->conv (sw->buf, buf, swlim);

        if (!(sw->hw->ctl_caps & VOICE_VOLUME_CAP)) {
            mixeng_volume (sw->buf, swlim, &sw->vol);
        }
    }

    while (swlim) {
        dead = hwsamples - live;
        left = hwsamples - wpos;
        blck = audio_MIN (dead, left);
        if (!blck) {
            break;
        }
        isamp = swlim;
        osamp = blck;
        st_rate_flow_mix (
            sw->rate,
            sw->buf + pos,
            sw->hw->mix_buf + wpos,
            &isamp,
            &osamp
            );
        ret += isamp;
        swlim -= isamp;
        pos += isamp;
        live += osamp;
        wpos = (wpos + osamp) % hwsamples;
        total += osamp;
    }

    sw->total_hw_samples_mixed += total;
    sw->empty = sw->total_hw_samples_mixed == 0;

#ifdef DEBUG_OUT
    dolog (
        "%s: write size %d ret %d total sw %d\n",
        SW_NAME (sw),
        size >> sw->info.shift,
        ret,
        sw->total_hw_samples_mixed
        );
#endif

    return ret << sw->info.shift;
}

#ifdef DEBUG_AUDIO
static void audio_pcm_print_info (const char *cap, struct audio_pcm_info *info)
{
    dolog ("%s: bits %d, sign %d, freq %d, nchan %d\n",
           cap, info->bits, info->sign, info->freq, info->nchannels);
}
#endif

#define DAC
#include "audio_template.h"
#undef DAC
#include "audio_template.h"

/*
 * Timer
 */

static bool audio_timer_running;
static uint64_t audio_timer_last;

static int audio_is_timer_needed (void)
{
    HWVoiceIn *hwi = NULL;
    HWVoiceOut *hwo = NULL;

    while ((hwo = audio_pcm_hw_find_any_enabled_out (hwo))) {
        if (!hwo->poll_mode) return 1;
    }
    while ((hwi = audio_pcm_hw_find_any_enabled_in (hwi))) {
        if (!hwi->poll_mode) return 1;
    }
    return 0;
}

static void audio_reset_timer (AudioState *s)
{
    if (audio_is_timer_needed ()) {
        timer_mod_anticipate_ns(s->ts,
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->period_ticks);
        if (!audio_timer_running) {
            audio_timer_running = true;
            audio_timer_last = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            trace_audio_timer_start(s->period_ticks / SCALE_MS);
        }
    } else {
        timer_del(s->ts);
        if (audio_timer_running) {
            audio_timer_running = false;
            trace_audio_timer_stop();
        }
    }
}

static void audio_timer (void *opaque)
{
    int64_t now, diff;
    AudioState *s = opaque;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    diff = now - audio_timer_last;
    if (diff > s->period_ticks * 3 / 2) {
        trace_audio_timer_delayed(diff / SCALE_MS);
    }
    audio_timer_last = now;

    audio_run("timer");
    audio_reset_timer(s);
}

/*
 * Public API
 */
int AUD_write (SWVoiceOut *sw, void *buf, int size)
{
    if (!sw) {
        /* XXX: Consider options */
        return size;
    }

    if (!sw->hw->enabled) {
        dolog ("Writing to disabled voice %s\n", SW_NAME (sw));
        return 0;
    }

    return sw->hw->pcm_ops->write(sw, buf, size);
}

int AUD_read (SWVoiceIn *sw, void *buf, int size)
{
    if (!sw) {
        /* XXX: Consider options */
        return size;
    }

    if (!sw->hw->enabled) {
        dolog ("Reading from disabled voice %s\n", SW_NAME (sw));
        return 0;
    }

    return sw->hw->pcm_ops->read(sw, buf, size);
}

int AUD_get_buffer_size_out (SWVoiceOut *sw)
{
    return sw->hw->samples << sw->hw->info.shift;
}

void AUD_set_active_out (SWVoiceOut *sw, int on)
{
    HWVoiceOut *hw;

    if (!sw) {
        return;
    }

    hw = sw->hw;
    if (sw->active != on) {
        AudioState *s = &glob_audio_state;
        SWVoiceOut *temp_sw;
        SWVoiceCap *sc;

        if (on) {
            hw->pending_disable = 0;
            if (!hw->enabled) {
                hw->enabled = 1;
                if (s->vm_running) {
                    hw->pcm_ops->ctl_out(hw, VOICE_ENABLE);
                    audio_reset_timer (s);
                }
            }
        }
        else {
            if (hw->enabled) {
                int nb_active = 0;

                for (temp_sw = hw->sw_head.lh_first; temp_sw;
                     temp_sw = temp_sw->entries.le_next) {
                    nb_active += temp_sw->active != 0;
                }

                hw->pending_disable = nb_active == 1;
            }
        }

        for (sc = hw->cap_head.lh_first; sc; sc = sc->entries.le_next) {
            sc->sw.active = hw->enabled;
            if (hw->enabled) {
                audio_capture_maybe_changed (sc->cap, 1);
            }
        }
        sw->active = on;
    }
}

void AUD_set_active_in (SWVoiceIn *sw, int on)
{
    HWVoiceIn *hw;

    if (!sw) {
        return;
    }

    hw = sw->hw;
    if (sw->active != on) {
        AudioState *s = &glob_audio_state;
        SWVoiceIn *temp_sw;

        if (on) {
            if (!hw->enabled) {
                hw->enabled = 1;
                if (s->vm_running) {
                    hw->pcm_ops->ctl_in(hw, VOICE_ENABLE);
                    audio_reset_timer (s);
                }
            }
            sw->total_hw_samples_acquired = hw->total_samples_captured;
        }
        else {
            if (hw->enabled) {
                int nb_active = 0;

                for (temp_sw = hw->sw_head.lh_first; temp_sw;
                     temp_sw = temp_sw->entries.le_next) {
                    nb_active += temp_sw->active != 0;
                }

                if (nb_active == 1) {
                    hw->enabled = 0;
                    hw->pcm_ops->ctl_in (hw, VOICE_DISABLE);
                }
            }
        }
        sw->active = on;
    }
}

static int audio_get_avail (SWVoiceIn *sw)
{
    int live;

    if (!sw) {
        return 0;
    }

    live = sw->hw->total_samples_captured - sw->total_hw_samples_acquired;
    if (audio_bug(__func__, live < 0 || live > sw->hw->samples)) {
        dolog ("live=%d sw->hw->samples=%d\n", live, sw->hw->samples);
        return 0;
    }

    ldebug (
        "%s: get_avail live %d ret %" PRId64 "\n",
        SW_NAME (sw),
        live, (((int64_t) live << 32) / sw->ratio) << sw->info.shift
        );

    return (((int64_t) live << 32) / sw->ratio) << sw->info.shift;
}

static int audio_get_free (SWVoiceOut *sw)
{
    int live, dead;

    if (!sw) {
        return 0;
    }

    live = sw->total_hw_samples_mixed;

    if (audio_bug(__func__, live < 0 || live > sw->hw->samples)) {
        dolog ("live=%d sw->hw->samples=%d\n", live, sw->hw->samples);
        return 0;
    }

    dead = sw->hw->samples - live;

#ifdef DEBUG_OUT
    dolog ("%s: get_free live %d dead %d ret %" PRId64 "\n",
           SW_NAME (sw),
           live, dead, (((int64_t) dead << 32) / sw->ratio) << sw->info.shift);
#endif

    return (((int64_t) dead << 32) / sw->ratio) << sw->info.shift;
}

static void audio_capture_mix_and_clear (HWVoiceOut *hw, int rpos, int samples)
{
    int n;

    if (hw->enabled) {
        SWVoiceCap *sc;

        for (sc = hw->cap_head.lh_first; sc; sc = sc->entries.le_next) {
            SWVoiceOut *sw = &sc->sw;
            int rpos2 = rpos;

            n = samples;
            while (n) {
                int till_end_of_hw = hw->samples - rpos2;
                int to_write = audio_MIN (till_end_of_hw, n);
                int bytes = to_write << hw->info.shift;
                int written;

                sw->buf = hw->mix_buf + rpos2;
                written = audio_pcm_sw_write (sw, NULL, bytes);
                if (written - bytes) {
                    dolog ("Could not mix %d bytes into a capture "
                           "buffer, mixed %d\n",
                           bytes, written);
                    break;
                }
                n -= to_write;
                rpos2 = (rpos2 + to_write) % hw->samples;
            }
        }
    }

    n = audio_MIN (samples, hw->samples - rpos);
    mixeng_clear (hw->mix_buf + rpos, n);
    mixeng_clear (hw->mix_buf, samples - n);
}

static void audio_run_out (AudioState *s)
{
    HWVoiceOut *hw = NULL;
    SWVoiceOut *sw;

    while ((hw = audio_pcm_hw_find_any_enabled_out (hw))) {
        int played;
        int live, free, nb_live, cleanup_required, prev_rpos;

        live = audio_pcm_hw_get_live_out (hw, &nb_live);
        if (!nb_live) {
            live = 0;
        }

        if (audio_bug(__func__, live < 0 || live > hw->samples)) {
            dolog ("live=%d hw->samples=%d\n", live, hw->samples);
            continue;
        }

        if (hw->pending_disable && !nb_live) {
            SWVoiceCap *sc;
#ifdef DEBUG_OUT
            dolog ("Disabling voice\n");
#endif
            hw->enabled = 0;
            hw->pending_disable = 0;
            hw->pcm_ops->ctl_out (hw, VOICE_DISABLE);
            for (sc = hw->cap_head.lh_first; sc; sc = sc->entries.le_next) {
                sc->sw.active = 0;
                audio_recalc_and_notify_capture (sc->cap);
            }
            continue;
        }

        if (!live) {
            for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
                if (sw->active) {
                    free = audio_get_free (sw);
                    if (free > 0) {
                        sw->callback.fn (sw->callback.opaque, free);
                    }
                }
            }
            continue;
        }

        prev_rpos = hw->rpos;
        played = hw->pcm_ops->run_out (hw, live);
        replay_audio_out(&played);
        if (audio_bug(__func__, hw->rpos >= hw->samples)) {
            dolog ("hw->rpos=%d hw->samples=%d played=%d\n",
                   hw->rpos, hw->samples, played);
            hw->rpos = 0;
        }

#ifdef DEBUG_OUT
        dolog ("played=%d\n", played);
#endif

        if (played) {
            hw->ts_helper += played;
            audio_capture_mix_and_clear (hw, prev_rpos, played);
        }

        cleanup_required = 0;
        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            if (!sw->active && sw->empty) {
                continue;
            }

            if (audio_bug(__func__, played > sw->total_hw_samples_mixed)) {
                dolog ("played=%d sw->total_hw_samples_mixed=%d\n",
                       played, sw->total_hw_samples_mixed);
                played = sw->total_hw_samples_mixed;
            }

            sw->total_hw_samples_mixed -= played;

            if (!sw->total_hw_samples_mixed) {
                sw->empty = 1;
                cleanup_required |= !sw->active && !sw->callback.fn;
            }

            if (sw->active) {
                free = audio_get_free (sw);
                if (free > 0) {
                    sw->callback.fn (sw->callback.opaque, free);
                }
            }
        }

        if (cleanup_required) {
            SWVoiceOut *sw1;

            sw = hw->sw_head.lh_first;
            while (sw) {
                sw1 = sw->entries.le_next;
                if (!sw->active && !sw->callback.fn) {
                    audio_close_out (sw);
                }
                sw = sw1;
            }
        }
    }
}

static void audio_run_in (AudioState *s)
{
    HWVoiceIn *hw = NULL;

    while ((hw = audio_pcm_hw_find_any_enabled_in (hw))) {
        SWVoiceIn *sw;
        int captured = 0, min;

        if (replay_mode != REPLAY_MODE_PLAY) {
            captured = hw->pcm_ops->run_in(hw);
        }
        replay_audio_in(&captured, hw->conv_buf, &hw->wpos, hw->samples);

        min = audio_pcm_hw_find_min_in (hw);
        hw->total_samples_captured += captured - min;
        hw->ts_helper += captured;

        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            sw->total_hw_samples_acquired -= min;

            if (sw->active) {
                int avail;

                avail = audio_get_avail (sw);
                if (avail > 0) {
                    sw->callback.fn (sw->callback.opaque, avail);
                }
            }
        }
    }
}

static void audio_run_capture (AudioState *s)
{
    CaptureVoiceOut *cap;

    for (cap = s->cap_head.lh_first; cap; cap = cap->entries.le_next) {
        int live, rpos, captured;
        HWVoiceOut *hw = &cap->hw;
        SWVoiceOut *sw;

        captured = live = audio_pcm_hw_get_live_out (hw, NULL);
        rpos = hw->rpos;
        while (live) {
            int left = hw->samples - rpos;
            int to_capture = audio_MIN (live, left);
            struct st_sample *src;
            struct capture_callback *cb;

            src = hw->mix_buf + rpos;
            hw->clip (cap->buf, src, to_capture);
            mixeng_clear (src, to_capture);

            for (cb = cap->cb_head.lh_first; cb; cb = cb->entries.le_next) {
                cb->ops.capture (cb->opaque, cap->buf,
                                 to_capture << hw->info.shift);
            }
            rpos = (rpos + to_capture) % hw->samples;
            live -= to_capture;
        }
        hw->rpos = rpos;

        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            if (!sw->active && sw->empty) {
                continue;
            }

            if (audio_bug(__func__, captured > sw->total_hw_samples_mixed)) {
                dolog ("captured=%d sw->total_hw_samples_mixed=%d\n",
                       captured, sw->total_hw_samples_mixed);
                captured = sw->total_hw_samples_mixed;
            }

            sw->total_hw_samples_mixed -= captured;
            sw->empty = sw->total_hw_samples_mixed == 0;
        }
    }
}

void audio_run (const char *msg)
{
    AudioState *s = &glob_audio_state;

    audio_run_out (s);
    audio_run_in (s);
    audio_run_capture (s);
#ifdef DEBUG_POLL
    {
        static double prevtime;
        double currtime;
        struct timeval tv;

        if (gettimeofday (&tv, NULL)) {
            perror ("audio_run: gettimeofday");
            return;
        }

        currtime = tv.tv_sec + tv.tv_usec * 1e-6;
        dolog ("Elapsed since last %s: %f\n", msg, currtime - prevtime);
        prevtime = currtime;
    }
#endif
}

static int audio_driver_init(AudioState *s, struct audio_driver *drv,
                             bool msg, Audiodev *dev)
{
    s->drv_opaque = drv->init(dev);

    if (s->drv_opaque) {
        audio_init_nb_voices_out (drv);
        audio_init_nb_voices_in (drv);
        s->drv = drv;
        return 0;
    }
    else {
        if (msg) {
            dolog("Could not init `%s' audio driver\n", drv->name);
        }
        return -1;
    }
}

static void audio_vm_change_state_handler (void *opaque, int running,
                                           RunState state)
{
    AudioState *s = opaque;
    HWVoiceOut *hwo = NULL;
    HWVoiceIn *hwi = NULL;
    int op = running ? VOICE_ENABLE : VOICE_DISABLE;

    s->vm_running = running;
    while ((hwo = audio_pcm_hw_find_any_enabled_out (hwo))) {
        hwo->pcm_ops->ctl_out(hwo, op);
    }

    while ((hwi = audio_pcm_hw_find_any_enabled_in (hwi))) {
        hwi->pcm_ops->ctl_in(hwi, op);
    }
    audio_reset_timer (s);
}

static bool is_cleaning_up;

bool audio_is_cleaning_up(void)
{
    return is_cleaning_up;
}

void audio_cleanup(void)
{
    AudioState *s = &glob_audio_state;
    HWVoiceOut *hwo, *hwon;
    HWVoiceIn *hwi, *hwin;

    is_cleaning_up = true;
    QLIST_FOREACH_SAFE(hwo, &glob_audio_state.hw_head_out, entries, hwon) {
        SWVoiceCap *sc;

        if (hwo->enabled) {
            hwo->pcm_ops->ctl_out (hwo, VOICE_DISABLE);
        }
        hwo->pcm_ops->fini_out (hwo);

        for (sc = hwo->cap_head.lh_first; sc; sc = sc->entries.le_next) {
            CaptureVoiceOut *cap = sc->cap;
            struct capture_callback *cb;

            for (cb = cap->cb_head.lh_first; cb; cb = cb->entries.le_next) {
                cb->ops.destroy (cb->opaque);
            }
        }
        QLIST_REMOVE(hwo, entries);
    }

    QLIST_FOREACH_SAFE(hwi, &glob_audio_state.hw_head_in, entries, hwin) {
        if (hwi->enabled) {
            hwi->pcm_ops->ctl_in (hwi, VOICE_DISABLE);
        }
        hwi->pcm_ops->fini_in (hwi);
        QLIST_REMOVE(hwi, entries);
    }

    if (s->drv) {
        s->drv->fini (s->drv_opaque);
        s->drv = NULL;
    }

    if (s->dev) {
        qapi_free_Audiodev(s->dev);
        s->dev = NULL;
    }
}

static const VMStateDescription vmstate_audio = {
    .name = "audio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void audio_validate_opts(Audiodev *dev, Error **errp);

static AudiodevListEntry *audiodev_find(
    AudiodevListHead *head, const char *drvname)
{
    AudiodevListEntry *e;
    QSIMPLEQ_FOREACH(e, head, next) {
        if (strcmp(AudiodevDriver_str(e->dev->driver), drvname) == 0) {
            return e;
        }
    }

    return NULL;
}

static int audio_init(Audiodev *dev)
{
    size_t i;
    int done = 0;
    const char *drvname = NULL;
    VMChangeStateEntry *e;
    AudioState *s = &glob_audio_state;
    struct audio_driver *driver;
    /* silence gcc warning about uninitialized variable */
    AudiodevListHead head = QSIMPLEQ_HEAD_INITIALIZER(head);

    if (s->drv) {
        if (dev) {
            dolog("Cannot create more than one audio backend, sorry\n");
            qapi_free_Audiodev(dev);
        }
        return -1;
    }

    if (dev) {
        /* -audiodev option */
        drvname = AudiodevDriver_str(dev->driver);
    } else {
        /* legacy implicit initialization */
        head = audio_handle_legacy_opts();
        /*
         * In case of legacy initialization, all Audiodevs in the list will have
         * the same configuration (except the driver), so it does't matter which
         * one we chose.  We need an Audiodev to set up AudioState before we can
         * init a driver.  Also note that dev at this point is still in the
         * list.
         */
        dev = QSIMPLEQ_FIRST(&head)->dev;
        audio_validate_opts(dev, &error_abort);
    }
    s->dev = dev;

    QLIST_INIT (&s->hw_head_out);
    QLIST_INIT (&s->hw_head_in);
    QLIST_INIT (&s->cap_head);
    atexit(audio_cleanup);

    s->ts = timer_new_ns(QEMU_CLOCK_VIRTUAL, audio_timer, s);

    s->nb_hw_voices_out = audio_get_pdo_out(dev)->voices;
    s->nb_hw_voices_in = audio_get_pdo_in(dev)->voices;

    if (s->nb_hw_voices_out <= 0) {
        dolog ("Bogus number of playback voices %d, setting to 1\n",
               s->nb_hw_voices_out);
        s->nb_hw_voices_out = 1;
    }

    if (s->nb_hw_voices_in <= 0) {
        dolog ("Bogus number of capture voices %d, setting to 0\n",
               s->nb_hw_voices_in);
        s->nb_hw_voices_in = 0;
    }

    if (drvname) {
        driver = audio_driver_lookup(drvname);
        if (driver) {
            done = !audio_driver_init(s, driver, true, dev);
        } else {
            dolog ("Unknown audio driver `%s'\n", drvname);
        }
    } else {
        for (i = 0; audio_prio_list[i]; i++) {
            AudiodevListEntry *e = audiodev_find(&head, audio_prio_list[i]);
            driver = audio_driver_lookup(audio_prio_list[i]);

            if (e && driver) {
                s->dev = dev = e->dev;
                audio_validate_opts(dev, &error_abort);
                done = !audio_driver_init(s, driver, false, dev);
                if (done) {
                    e->dev = NULL;
                    break;
                }
            }
        }
    }
    audio_free_audiodev_list(&head);

    if (!done) {
        driver = audio_driver_lookup("none");
        done = !audio_driver_init(s, driver, false, dev);
        assert(done);
        dolog("warning: Using timer based audio emulation\n");
    }

    if (dev->timer_period <= 0) {
        s->period_ticks = 1;
    } else {
        s->period_ticks = dev->timer_period * SCALE_US;
    }

    e = qemu_add_vm_change_state_handler (audio_vm_change_state_handler, s);
    if (!e) {
        dolog ("warning: Could not register change state handler\n"
               "(Audio can continue looping even after stopping the VM)\n");
    }

    QLIST_INIT (&s->card_head);
    vmstate_register (NULL, 0, &vmstate_audio, s);
    return 0;
}

void audio_free_audiodev_list(AudiodevListHead *head)
{
    AudiodevListEntry *e;
    while ((e = QSIMPLEQ_FIRST(head))) {
        QSIMPLEQ_REMOVE_HEAD(head, next);
        qapi_free_Audiodev(e->dev);
        g_free(e);
    }
}

void AUD_register_card (const char *name, QEMUSoundCard *card)
{
    audio_init(NULL);
    card->name = g_strdup (name);
    memset (&card->entries, 0, sizeof (card->entries));
    QLIST_INSERT_HEAD (&glob_audio_state.card_head, card, entries);
}

void AUD_remove_card (QEMUSoundCard *card)
{
    QLIST_REMOVE (card, entries);
    g_free (card->name);
}


CaptureVoiceOut *AUD_add_capture (
    struct audsettings *as,
    struct audio_capture_ops *ops,
    void *cb_opaque
    )
{
    AudioState *s = &glob_audio_state;
    CaptureVoiceOut *cap;
    struct capture_callback *cb;

    if (audio_validate_settings (as)) {
        dolog ("Invalid settings were passed when trying to add capture\n");
        audio_print_settings (as);
        return NULL;
    }

    cb = g_malloc0(sizeof(*cb));
    cb->ops = *ops;
    cb->opaque = cb_opaque;

    cap = audio_pcm_capture_find_specific (as);
    if (cap) {
        QLIST_INSERT_HEAD (&cap->cb_head, cb, entries);
        return cap;
    }
    else {
        HWVoiceOut *hw;
        CaptureVoiceOut *cap;

        cap = g_malloc0(sizeof(*cap));

        hw = &cap->hw;
        QLIST_INIT (&hw->sw_head);
        QLIST_INIT (&cap->cb_head);

        /* XXX find a more elegant way */
        hw->samples = 4096 * 4;
        hw->mix_buf = g_new0(struct st_sample, hw->samples);

        audio_pcm_init_info (&hw->info, as);

        cap->buf = g_malloc0_n(hw->samples, 1 << hw->info.shift);

        hw->clip = mixeng_clip
            [hw->info.nchannels == 2]
            [hw->info.sign]
            [hw->info.swap_endianness]
            [audio_bits_to_index (hw->info.bits)];

        QLIST_INSERT_HEAD (&s->cap_head, cap, entries);
        QLIST_INSERT_HEAD (&cap->cb_head, cb, entries);

        QLIST_FOREACH(hw, &glob_audio_state.hw_head_out, entries) {
            audio_attach_capture (hw);
        }
        return cap;
    }
}

void AUD_del_capture (CaptureVoiceOut *cap, void *cb_opaque)
{
    struct capture_callback *cb;

    for (cb = cap->cb_head.lh_first; cb; cb = cb->entries.le_next) {
        if (cb->opaque == cb_opaque) {
            cb->ops.destroy (cb_opaque);
            QLIST_REMOVE (cb, entries);
            g_free (cb);

            if (!cap->cb_head.lh_first) {
                SWVoiceOut *sw = cap->hw.sw_head.lh_first, *sw1;

                while (sw) {
                    SWVoiceCap *sc = (SWVoiceCap *) sw;
#ifdef DEBUG_CAPTURE
                    dolog ("freeing %s\n", sw->name);
#endif

                    sw1 = sw->entries.le_next;
                    if (sw->rate) {
                        st_rate_stop (sw->rate);
                        sw->rate = NULL;
                    }
                    QLIST_REMOVE (sw, entries);
                    QLIST_REMOVE (sc, entries);
                    g_free (sc);
                    sw = sw1;
                }
                QLIST_REMOVE (cap, entries);
                g_free (cap->hw.mix_buf);
                g_free (cap->buf);
                g_free (cap);
            }
            return;
        }
    }
}

void AUD_set_volume_out (SWVoiceOut *sw, int mute, uint8_t lvol, uint8_t rvol)
{
    if (sw) {
        HWVoiceOut *hw = sw->hw;

        sw->vol.mute = mute;
        sw->vol.l = nominal_volume.l * lvol / 255;
        sw->vol.r = nominal_volume.r * rvol / 255;

        if (hw->pcm_ops->ctl_out) {
            hw->pcm_ops->ctl_out (hw, VOICE_VOLUME, sw);
        }
    }
}

void AUD_set_volume_in (SWVoiceIn *sw, int mute, uint8_t lvol, uint8_t rvol)
{
    if (sw) {
        HWVoiceIn *hw = sw->hw;

        sw->vol.mute = mute;
        sw->vol.l = nominal_volume.l * lvol / 255;
        sw->vol.r = nominal_volume.r * rvol / 255;

        if (hw->pcm_ops->ctl_in) {
            hw->pcm_ops->ctl_in (hw, VOICE_VOLUME, sw);
        }
    }
}

void audio_create_pdos(Audiodev *dev)
{
    switch (dev->driver) {
#define CASE(DRIVER, driver, pdo_name)                              \
    case AUDIODEV_DRIVER_##DRIVER:                                  \
        if (!dev->u.driver.has_in) {                                \
            dev->u.driver.in = g_malloc0(                           \
                sizeof(Audiodev##pdo_name##PerDirectionOptions));   \
            dev->u.driver.has_in = true;                            \
        }                                                           \
        if (!dev->u.driver.has_out) {                               \
            dev->u.driver.out = g_malloc0(                          \
                sizeof(AudiodevAlsaPerDirectionOptions));           \
            dev->u.driver.has_out = true;                           \
        }                                                           \
        break

        CASE(NONE, none, );
        CASE(ALSA, alsa, Alsa);
        CASE(COREAUDIO, coreaudio, Coreaudio);
        CASE(DSOUND, dsound, );
        CASE(OSS, oss, Oss);
        CASE(PA, pa, Pa);
        CASE(SDL, sdl, );
        CASE(SPICE, spice, );
        CASE(WAV, wav, );

    case AUDIODEV_DRIVER__MAX:
        abort();
    };
}

static void audio_validate_per_direction_opts(
    AudiodevPerDirectionOptions *pdo, Error **errp)
{
    if (!pdo->has_fixed_settings) {
        pdo->has_fixed_settings = true;
        pdo->fixed_settings = true;
    }
    if (!pdo->fixed_settings &&
        (pdo->has_frequency || pdo->has_channels || pdo->has_format)) {
        error_setg(errp,
                   "You can't use frequency, channels or format with fixed-settings=off");
        return;
    }

    if (!pdo->has_frequency) {
        pdo->has_frequency = true;
        pdo->frequency = 44100;
    }
    if (!pdo->has_channels) {
        pdo->has_channels = true;
        pdo->channels = 2;
    }
    if (!pdo->has_voices) {
        pdo->has_voices = true;
        pdo->voices = 1;
    }
    if (!pdo->has_format) {
        pdo->has_format = true;
        pdo->format = AUDIO_FORMAT_S16;
    }
}

static void audio_validate_opts(Audiodev *dev, Error **errp)
{
    Error *err = NULL;

    audio_create_pdos(dev);

    audio_validate_per_direction_opts(audio_get_pdo_in(dev), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    audio_validate_per_direction_opts(audio_get_pdo_out(dev), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!dev->has_timer_period) {
        dev->has_timer_period = true;
        dev->timer_period = 10000; /* 100Hz -> 10ms */
    }
}

void audio_parse_option(const char *opt)
{
    AudiodevListEntry *e;
    Audiodev *dev = NULL;

    Visitor *v = qobject_input_visitor_new_str(opt, "driver", &error_fatal);
    visit_type_Audiodev(v, NULL, &dev, &error_fatal);
    visit_free(v);

    audio_validate_opts(dev, &error_fatal);

    e = g_malloc0(sizeof(AudiodevListEntry));
    e->dev = dev;
    QSIMPLEQ_INSERT_TAIL(&audiodevs, e, next);
}

void audio_init_audiodevs(void)
{
    AudiodevListEntry *e;

    QSIMPLEQ_FOREACH(e, &audiodevs, next) {
        audio_init(e->dev);
    }
}

audsettings audiodev_to_audsettings(AudiodevPerDirectionOptions *pdo)
{
    return (audsettings) {
        .freq = pdo->frequency,
        .nchannels = pdo->channels,
        .fmt = pdo->format,
        .endianness = AUDIO_HOST_ENDIANNESS,
    };
}

int audioformat_bytes_per_sample(AudioFormat fmt)
{
    switch (fmt) {
    case AUDIO_FORMAT_U8:
    case AUDIO_FORMAT_S8:
        return 1;

    case AUDIO_FORMAT_U16:
    case AUDIO_FORMAT_S16:
        return 2;

    case AUDIO_FORMAT_U32:
    case AUDIO_FORMAT_S32:
        return 4;

    case AUDIO_FORMAT__MAX:
        ;
    }
    abort();
}


/* frames = freq * usec / 1e6 */
int audio_buffer_frames(AudiodevPerDirectionOptions *pdo,
                        audsettings *as, int def_usecs)
{
    uint64_t usecs = pdo->has_buffer_length ? pdo->buffer_length : def_usecs;
    return (as->freq * usecs + 500000) / 1000000;
}

/* samples = channels * frames = channels * freq * usec / 1e6 */
int audio_buffer_samples(AudiodevPerDirectionOptions *pdo,
                         audsettings *as, int def_usecs)
{
    return as->nchannels * audio_buffer_frames(pdo, as, def_usecs);
}

/*
 * bytes = bytes_per_sample * samples =
 *     bytes_per_sample * channels * freq * usec / 1e6
 */
int audio_buffer_bytes(AudiodevPerDirectionOptions *pdo,
                       audsettings *as, int def_usecs)
{
    return audio_buffer_samples(pdo, as, def_usecs) *
        audioformat_bytes_per_sample(as->fmt);
}
