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
#include "audio.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qapi/clone-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-audio.h"
#include "qapi/qapi-commands-audio.h"
#include "qapi/qmp/qdict.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/help_option.h"
#include "sysemu/sysemu.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "ui/qemu-spice.h"
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
    NULL
};

static QLIST_HEAD(, audio_driver) audio_drivers;
static AudiodevListHead audiodevs =
    QSIMPLEQ_HEAD_INITIALIZER(audiodevs);
static AudiodevListHead default_audiodevs =
    QSIMPLEQ_HEAD_INITIALIZER(default_audiodevs);


void audio_driver_register(audio_driver *drv)
{
    QLIST_INSERT_HEAD(&audio_drivers, drv, next);
}

static audio_driver *audio_driver_lookup(const char *name)
{
    struct audio_driver *d;
    Error *local_err = NULL;
    int rv;

    QLIST_FOREACH(d, &audio_drivers, next) {
        if (strcmp(name, d->name) == 0) {
            return d;
        }
    }
    rv = audio_module_load(name, &local_err);
    if (rv > 0) {
        QLIST_FOREACH(d, &audio_drivers, next) {
            if (strcmp(name, d->name) == 0) {
                return d;
            }
        }
    } else if (rv < 0) {
        error_report_err(local_err);
    }
    return NULL;
}

static QTAILQ_HEAD(AudioStateHead, AudioState) audio_states =
    QTAILQ_HEAD_INITIALIZER(audio_states);
static AudioState *default_audio_state;

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
    }

    return cond;
}

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
    case AUDIO_FORMAT_F32:
        AUD_log (NULL, "F32");
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

    invalid = as->nchannels < 1;
    invalid |= as->endianness != 0 && as->endianness != 1;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
    case AUDIO_FORMAT_U8:
    case AUDIO_FORMAT_S16:
    case AUDIO_FORMAT_U16:
    case AUDIO_FORMAT_S32:
    case AUDIO_FORMAT_U32:
    case AUDIO_FORMAT_F32:
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
    int bits = 8;
    bool is_signed = false, is_float = false;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
        is_signed = true;
        /* fall through */
    case AUDIO_FORMAT_U8:
        break;

    case AUDIO_FORMAT_S16:
        is_signed = true;
        /* fall through */
    case AUDIO_FORMAT_U16:
        bits = 16;
        break;

    case AUDIO_FORMAT_F32:
        is_float = true;
        /* fall through */
    case AUDIO_FORMAT_S32:
        is_signed = true;
        /* fall through */
    case AUDIO_FORMAT_U32:
        bits = 32;
        break;

    default:
        abort();
    }
    return info->freq == as->freq
        && info->nchannels == as->nchannels
        && info->is_signed == is_signed
        && info->is_float == is_float
        && info->bits == bits
        && info->swap_endianness == (as->endianness != AUDIO_HOST_ENDIANNESS);
}

void audio_pcm_init_info (struct audio_pcm_info *info, struct audsettings *as)
{
    int bits = 8, mul;
    bool is_signed = false, is_float = false;

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
        is_signed = true;
        /* fall through */
    case AUDIO_FORMAT_U8:
        mul = 1;
        break;

    case AUDIO_FORMAT_S16:
        is_signed = true;
        /* fall through */
    case AUDIO_FORMAT_U16:
        bits = 16;
        mul = 2;
        break;

    case AUDIO_FORMAT_F32:
        is_float = true;
        /* fall through */
    case AUDIO_FORMAT_S32:
        is_signed = true;
        /* fall through */
    case AUDIO_FORMAT_U32:
        bits = 32;
        mul = 4;
        break;

    default:
        abort();
    }

    info->freq = as->freq;
    info->bits = bits;
    info->is_signed = is_signed;
    info->is_float = is_float;
    info->nchannels = as->nchannels;
    info->bytes_per_frame = as->nchannels * mul;
    info->bytes_per_second = info->freq * info->bytes_per_frame;
    info->swap_endianness = (as->endianness != AUDIO_HOST_ENDIANNESS);
}

void audio_pcm_info_clear_buf (struct audio_pcm_info *info, void *buf, int len)
{
    if (!len) {
        return;
    }

    if (info->is_signed || info->is_float) {
        memset(buf, 0x00, len * info->bytes_per_frame);
    } else {
        switch (info->bits) {
        case 8:
            memset(buf, 0x80, len * info->bytes_per_frame);
            break;

        case 16:
            {
                int i;
                uint16_t *p = buf;
                short s = INT16_MAX;

                if (info->swap_endianness) {
                    s = bswap16 (s);
                }

                for (i = 0; i < len * info->nchannels; i++) {
                    p[i] = s;
                }
            }
            break;

        case 32:
            {
                int i;
                uint32_t *p = buf;
                int32_t s = INT32_MAX;

                if (info->swap_endianness) {
                    s = bswap32 (s);
                }

                for (i = 0; i < len * info->nchannels; i++) {
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
static CaptureVoiceOut *audio_pcm_capture_find_specific(AudioState *s,
                                                        struct audsettings *as)
{
    CaptureVoiceOut *cap;

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
    AudioState *s = hw->s;
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
        sw->vol = nominal_volume;
        sw->rate = st_rate_start (sw->info.freq, hw_cap->info.freq);
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
static size_t audio_pcm_hw_find_min_in (HWVoiceIn *hw)
{
    SWVoiceIn *sw;
    size_t m = hw->total_samples_captured;

    for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
        if (sw->active) {
            m = MIN (m, sw->total_hw_samples_acquired);
        }
    }
    return m;
}

static size_t audio_pcm_hw_get_live_in(HWVoiceIn *hw)
{
    size_t live = hw->total_samples_captured - audio_pcm_hw_find_min_in (hw);
    if (audio_bug(__func__, live > hw->conv_buf.size)) {
        dolog("live=%zu hw->conv_buf.size=%zu\n", live, hw->conv_buf.size);
        return 0;
    }
    return live;
}

static size_t audio_pcm_hw_conv_in(HWVoiceIn *hw, void *pcm_buf, size_t samples)
{
    size_t conv = 0;
    STSampleBuffer *conv_buf = &hw->conv_buf;

    while (samples) {
        uint8_t *src = advance(pcm_buf, conv * hw->info.bytes_per_frame);
        size_t proc = MIN(samples, conv_buf->size - conv_buf->pos);

        hw->conv(conv_buf->buffer + conv_buf->pos, src, proc);
        conv_buf->pos = (conv_buf->pos + proc) % conv_buf->size;
        samples -= proc;
        conv += proc;
    }

    return conv;
}

/*
 * Soft voice (capture)
 */
static void audio_pcm_sw_resample_in(SWVoiceIn *sw,
    size_t frames_in_max, size_t frames_out_max,
    size_t *total_in, size_t *total_out)
{
    HWVoiceIn *hw = sw->hw;
    struct st_sample *src, *dst;
    size_t live, rpos, frames_in, frames_out;

    live = hw->total_samples_captured - sw->total_hw_samples_acquired;
    rpos = audio_ring_posb(hw->conv_buf.pos, live, hw->conv_buf.size);

    /* resample conv_buf from rpos to end of buffer */
    src = hw->conv_buf.buffer + rpos;
    frames_in = MIN(frames_in_max, hw->conv_buf.size - rpos);
    dst = sw->resample_buf.buffer;
    frames_out = frames_out_max;
    st_rate_flow(sw->rate, src, dst, &frames_in, &frames_out);
    rpos += frames_in;
    *total_in = frames_in;
    *total_out = frames_out;

    /* resample conv_buf from start of buffer if there are input frames left */
    if (frames_in_max - frames_in && rpos == hw->conv_buf.size) {
        src = hw->conv_buf.buffer;
        frames_in = frames_in_max - frames_in;
        dst += frames_out;
        frames_out = frames_out_max - frames_out;
        st_rate_flow(sw->rate, src, dst, &frames_in, &frames_out);
        *total_in += frames_in;
        *total_out += frames_out;
    }
}

static size_t audio_pcm_sw_read(SWVoiceIn *sw, void *buf, size_t buf_len)
{
    HWVoiceIn *hw = sw->hw;
    size_t live, frames_out_max, total_in, total_out;

    live = hw->total_samples_captured - sw->total_hw_samples_acquired;
    if (!live) {
        return 0;
    }
    if (audio_bug(__func__, live > hw->conv_buf.size)) {
        dolog("live_in=%zu hw->conv_buf.size=%zu\n", live, hw->conv_buf.size);
        return 0;
    }

    frames_out_max = MIN(buf_len / sw->info.bytes_per_frame,
                         sw->resample_buf.size);

    audio_pcm_sw_resample_in(sw, live, frames_out_max, &total_in, &total_out);

    if (!hw->pcm_ops->volume_in) {
        mixeng_volume(sw->resample_buf.buffer, total_out, &sw->vol);
    }
    sw->clip(buf, sw->resample_buf.buffer, total_out);

    sw->total_hw_samples_acquired += total_in;
    return total_out * sw->info.bytes_per_frame;
}

/*
 * Hard voice (playback)
 */
static size_t audio_pcm_hw_find_min_out (HWVoiceOut *hw, int *nb_livep)
{
    SWVoiceOut *sw;
    size_t m = SIZE_MAX;
    int nb_live = 0;

    for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
        if (sw->active || !sw->empty) {
            m = MIN (m, sw->total_hw_samples_mixed);
            nb_live += 1;
        }
    }

    *nb_livep = nb_live;
    return m;
}

static size_t audio_pcm_hw_get_live_out (HWVoiceOut *hw, int *nb_live)
{
    size_t smin;
    int nb_live1;

    smin = audio_pcm_hw_find_min_out (hw, &nb_live1);
    if (nb_live) {
        *nb_live = nb_live1;
    }

    if (nb_live1) {
        size_t live = smin;

        if (audio_bug(__func__, live > hw->mix_buf.size)) {
            dolog("live=%zu hw->mix_buf.size=%zu\n", live, hw->mix_buf.size);
            return 0;
        }
        return live;
    }
    return 0;
}

static size_t audio_pcm_hw_get_free(HWVoiceOut *hw)
{
    return (hw->pcm_ops->buffer_get_free ? hw->pcm_ops->buffer_get_free(hw) :
            INT_MAX) / hw->info.bytes_per_frame;
}

static void audio_pcm_hw_clip_out(HWVoiceOut *hw, void *pcm_buf, size_t len)
{
    size_t clipped = 0;
    size_t pos = hw->mix_buf.pos;

    while (len) {
        st_sample *src = hw->mix_buf.buffer + pos;
        uint8_t *dst = advance(pcm_buf, clipped * hw->info.bytes_per_frame);
        size_t samples_till_end_of_buf = hw->mix_buf.size - pos;
        size_t samples_to_clip = MIN(len, samples_till_end_of_buf);

        hw->clip(dst, src, samples_to_clip);

        pos = (pos + samples_to_clip) % hw->mix_buf.size;
        len -= samples_to_clip;
        clipped += samples_to_clip;
    }
}

/*
 * Soft voice (playback)
 */
static void audio_pcm_sw_resample_out(SWVoiceOut *sw,
    size_t frames_in_max, size_t frames_out_max,
    size_t *total_in, size_t *total_out)
{
    HWVoiceOut *hw = sw->hw;
    struct st_sample *src, *dst;
    size_t live, wpos, frames_in, frames_out;

    live = sw->total_hw_samples_mixed;
    wpos = (hw->mix_buf.pos + live) % hw->mix_buf.size;

    /* write to mix_buf from wpos to end of buffer */
    src = sw->resample_buf.buffer;
    frames_in = frames_in_max;
    dst = hw->mix_buf.buffer + wpos;
    frames_out = MIN(frames_out_max, hw->mix_buf.size - wpos);
    st_rate_flow_mix(sw->rate, src, dst, &frames_in, &frames_out);
    wpos += frames_out;
    *total_in = frames_in;
    *total_out = frames_out;

    /* write to mix_buf from start of buffer if there are input frames left */
    if (frames_in_max - frames_in > 0 && wpos == hw->mix_buf.size) {
        src += frames_in;
        frames_in = frames_in_max - frames_in;
        dst = hw->mix_buf.buffer;
        frames_out = frames_out_max - frames_out;
        st_rate_flow_mix(sw->rate, src, dst, &frames_in, &frames_out);
        *total_in += frames_in;
        *total_out += frames_out;
    }
}

static size_t audio_pcm_sw_write(SWVoiceOut *sw, void *buf, size_t buf_len)
{
    HWVoiceOut *hw = sw->hw;
    size_t live, dead, hw_free, sw_max, fe_max;
    size_t frames_in_max, frames_out_max, total_in, total_out;

    live = sw->total_hw_samples_mixed;
    if (audio_bug(__func__, live > hw->mix_buf.size)) {
        dolog("live=%zu hw->mix_buf.size=%zu\n", live, hw->mix_buf.size);
        return 0;
    }

    if (live == hw->mix_buf.size) {
#ifdef DEBUG_OUT
        dolog ("%s is full %zu\n", sw->name, live);
#endif
        return 0;
    }

    dead = hw->mix_buf.size - live;
    hw_free = audio_pcm_hw_get_free(hw);
    hw_free = hw_free > live ? hw_free - live : 0;
    frames_out_max = MIN(dead, hw_free);
    sw_max = st_rate_frames_in(sw->rate, frames_out_max);
    fe_max = MIN(buf_len / sw->info.bytes_per_frame + sw->resample_buf.pos,
                 sw->resample_buf.size);
    frames_in_max = MIN(sw_max, fe_max);

    if (!frames_in_max) {
        return 0;
    }

    if (frames_in_max > sw->resample_buf.pos) {
        sw->conv(sw->resample_buf.buffer + sw->resample_buf.pos,
                 buf, frames_in_max - sw->resample_buf.pos);
        if (!sw->hw->pcm_ops->volume_out) {
            mixeng_volume(sw->resample_buf.buffer + sw->resample_buf.pos,
                          frames_in_max - sw->resample_buf.pos, &sw->vol);
        }
    }

    audio_pcm_sw_resample_out(sw, frames_in_max, frames_out_max,
                              &total_in, &total_out);

    sw->total_hw_samples_mixed += total_out;
    sw->empty = sw->total_hw_samples_mixed == 0;

    /*
     * Upsampling may leave one audio frame in the resample buffer. Decrement
     * total_in by one if there was a leftover frame from the previous resample
     * pass in the resample buffer. Increment total_in by one if the current
     * resample pass left one frame in the resample buffer.
     */
    if (frames_in_max - total_in == 1) {
        /* copy one leftover audio frame to the beginning of the buffer */
        *sw->resample_buf.buffer = *(sw->resample_buf.buffer + total_in);
        total_in += 1 - sw->resample_buf.pos;
        sw->resample_buf.pos = 1;
    } else if (total_in >= sw->resample_buf.pos) {
        total_in -= sw->resample_buf.pos;
        sw->resample_buf.pos = 0;
    }

#ifdef DEBUG_OUT
    dolog (
        "%s: write size %zu written %zu total mixed %zu\n",
        SW_NAME(sw),
        buf_len / sw->info.bytes_per_frame,
        total_in,
        sw->total_hw_samples_mixed
        );
#endif

    return total_in * sw->info.bytes_per_frame;
}

#ifdef DEBUG_AUDIO
static void audio_pcm_print_info (const char *cap, struct audio_pcm_info *info)
{
    dolog("%s: bits %d, sign %d, float %d, freq %d, nchan %d\n",
          cap, info->bits, info->is_signed, info->is_float, info->freq,
          info->nchannels);
}
#endif

#define DAC
#include "audio_template.h"
#undef DAC
#include "audio_template.h"

/*
 * Timer
 */
static int audio_is_timer_needed(AudioState *s)
{
    HWVoiceIn *hwi = NULL;
    HWVoiceOut *hwo = NULL;

    while ((hwo = audio_pcm_hw_find_any_enabled_out(s, hwo))) {
        if (!hwo->poll_mode) {
            return 1;
        }
    }
    while ((hwi = audio_pcm_hw_find_any_enabled_in(s, hwi))) {
        if (!hwi->poll_mode) {
            return 1;
        }
    }
    return 0;
}

static void audio_reset_timer (AudioState *s)
{
    if (audio_is_timer_needed(s)) {
        timer_mod_anticipate_ns(s->ts,
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->period_ticks);
        if (!s->timer_running) {
            s->timer_running = true;
            s->timer_last = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            trace_audio_timer_start(s->period_ticks / SCALE_MS);
        }
    } else {
        timer_del(s->ts);
        if (s->timer_running) {
            s->timer_running = false;
            trace_audio_timer_stop();
        }
    }
}

static void audio_timer (void *opaque)
{
    int64_t now, diff;
    AudioState *s = opaque;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    diff = now - s->timer_last;
    if (diff > s->period_ticks * 3 / 2) {
        trace_audio_timer_delayed(diff / SCALE_MS);
    }
    s->timer_last = now;

    audio_run(s, "timer");
    audio_reset_timer(s);
}

/*
 * Public API
 */
size_t AUD_write(SWVoiceOut *sw, void *buf, size_t size)
{
    HWVoiceOut *hw;

    if (!sw) {
        /* XXX: Consider options */
        return size;
    }
    hw = sw->hw;

    if (!hw->enabled) {
        dolog ("Writing to disabled voice %s\n", SW_NAME (sw));
        return 0;
    }

    if (audio_get_pdo_out(hw->s->dev)->mixing_engine) {
        return audio_pcm_sw_write(sw, buf, size);
    } else {
        return hw->pcm_ops->write(hw, buf, size);
    }
}

size_t AUD_read(SWVoiceIn *sw, void *buf, size_t size)
{
    HWVoiceIn *hw;

    if (!sw) {
        /* XXX: Consider options */
        return size;
    }
    hw = sw->hw;

    if (!hw->enabled) {
        dolog ("Reading from disabled voice %s\n", SW_NAME (sw));
        return 0;
    }

    if (audio_get_pdo_in(hw->s->dev)->mixing_engine) {
        return audio_pcm_sw_read(sw, buf, size);
    } else {
        return hw->pcm_ops->read(hw, buf, size);
    }
}

int AUD_get_buffer_size_out(SWVoiceOut *sw)
{
    return sw->hw->samples * sw->hw->info.bytes_per_frame;
}

void AUD_set_active_out (SWVoiceOut *sw, int on)
{
    HWVoiceOut *hw;

    if (!sw) {
        return;
    }

    hw = sw->hw;
    if (sw->active != on) {
        AudioState *s = sw->s;
        SWVoiceOut *temp_sw;
        SWVoiceCap *sc;

        if (on) {
            hw->pending_disable = 0;
            if (!hw->enabled) {
                hw->enabled = 1;
                if (s->vm_running) {
                    if (hw->pcm_ops->enable_out) {
                        hw->pcm_ops->enable_out(hw, true);
                    }
                    audio_reset_timer (s);
                }
            }
        } else {
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
        AudioState *s = sw->s;
        SWVoiceIn *temp_sw;

        if (on) {
            if (!hw->enabled) {
                hw->enabled = 1;
                if (s->vm_running) {
                    if (hw->pcm_ops->enable_in) {
                        hw->pcm_ops->enable_in(hw, true);
                    }
                    audio_reset_timer (s);
                }
            }
            sw->total_hw_samples_acquired = hw->total_samples_captured;
        } else {
            if (hw->enabled) {
                int nb_active = 0;

                for (temp_sw = hw->sw_head.lh_first; temp_sw;
                     temp_sw = temp_sw->entries.le_next) {
                    nb_active += temp_sw->active != 0;
                }

                if (nb_active == 1) {
                    hw->enabled = 0;
                    if (hw->pcm_ops->enable_in) {
                        hw->pcm_ops->enable_in(hw, false);
                    }
                }
            }
        }
        sw->active = on;
    }
}

static size_t audio_get_avail (SWVoiceIn *sw)
{
    size_t live;

    if (!sw) {
        return 0;
    }

    live = sw->hw->total_samples_captured - sw->total_hw_samples_acquired;
    if (audio_bug(__func__, live > sw->hw->conv_buf.size)) {
        dolog("live=%zu sw->hw->conv_buf.size=%zu\n", live,
              sw->hw->conv_buf.size);
        return 0;
    }

    ldebug (
        "%s: get_avail live %zu frontend frames %u\n",
        SW_NAME (sw),
        live, st_rate_frames_out(sw->rate, live)
        );

    return live;
}

static size_t audio_get_free(SWVoiceOut *sw)
{
    size_t live, dead;

    if (!sw) {
        return 0;
    }

    live = sw->total_hw_samples_mixed;

    if (audio_bug(__func__, live > sw->hw->mix_buf.size)) {
        dolog("live=%zu sw->hw->mix_buf.size=%zu\n", live,
              sw->hw->mix_buf.size);
        return 0;
    }

    dead = sw->hw->mix_buf.size - live;

#ifdef DEBUG_OUT
    dolog("%s: get_free live %zu dead %zu frontend frames %u\n",
          SW_NAME(sw), live, dead, st_rate_frames_in(sw->rate, dead));
#endif

    return dead;
}

static void audio_capture_mix_and_clear(HWVoiceOut *hw, size_t rpos,
                                        size_t samples)
{
    size_t n;

    if (hw->enabled) {
        SWVoiceCap *sc;

        for (sc = hw->cap_head.lh_first; sc; sc = sc->entries.le_next) {
            SWVoiceOut *sw = &sc->sw;
            size_t rpos2 = rpos;

            n = samples;
            while (n) {
                size_t till_end_of_hw = hw->mix_buf.size - rpos2;
                size_t to_read = MIN(till_end_of_hw, n);
                size_t live, frames_in, frames_out;

                sw->resample_buf.buffer = hw->mix_buf.buffer + rpos2;
                sw->resample_buf.size = to_read;
                live = sw->total_hw_samples_mixed;

                audio_pcm_sw_resample_out(sw,
                                          to_read, sw->hw->mix_buf.size - live,
                                          &frames_in, &frames_out);

                sw->total_hw_samples_mixed += frames_out;
                sw->empty = sw->total_hw_samples_mixed == 0;

                if (to_read - frames_in) {
                    dolog("Could not mix %zu frames into a capture "
                          "buffer, mixed %zu\n",
                          to_read, frames_in);
                    break;
                }
                n -= to_read;
                rpos2 = (rpos2 + to_read) % hw->mix_buf.size;
            }
        }
    }

    n = MIN(samples, hw->mix_buf.size - rpos);
    mixeng_clear(hw->mix_buf.buffer + rpos, n);
    mixeng_clear(hw->mix_buf.buffer, samples - n);
}

static size_t audio_pcm_hw_run_out(HWVoiceOut *hw, size_t live)
{
    size_t clipped = 0;

    while (live) {
        size_t size = live * hw->info.bytes_per_frame;
        size_t decr, proc;
        void *buf = hw->pcm_ops->get_buffer_out(hw, &size);

        if (size == 0) {
            break;
        }

        decr = MIN(size / hw->info.bytes_per_frame, live);
        if (buf) {
            audio_pcm_hw_clip_out(hw, buf, decr);
        }
        proc = hw->pcm_ops->put_buffer_out(hw, buf,
                                           decr * hw->info.bytes_per_frame) /
            hw->info.bytes_per_frame;

        live -= proc;
        clipped += proc;
        hw->mix_buf.pos = (hw->mix_buf.pos + proc) % hw->mix_buf.size;

        if (proc == 0 || proc < decr) {
            break;
        }
    }

    if (hw->pcm_ops->run_buffer_out) {
        hw->pcm_ops->run_buffer_out(hw);
    }

    return clipped;
}

static void audio_run_out (AudioState *s)
{
    HWVoiceOut *hw = NULL;
    SWVoiceOut *sw;

    while ((hw = audio_pcm_hw_find_any_enabled_out(s, hw))) {
        size_t played, live, prev_rpos;
        size_t hw_free = audio_pcm_hw_get_free(hw);
        int nb_live;

        if (!audio_get_pdo_out(s->dev)->mixing_engine) {
            /* there is exactly 1 sw for each hw with no mixeng */
            sw = hw->sw_head.lh_first;

            if (hw->pending_disable) {
                hw->enabled = 0;
                hw->pending_disable = 0;
                if (hw->pcm_ops->enable_out) {
                    hw->pcm_ops->enable_out(hw, false);
                }
            }

            if (sw->active) {
                sw->callback.fn(sw->callback.opaque,
                                hw_free * sw->info.bytes_per_frame);
            }

            if (hw->pcm_ops->run_buffer_out) {
                hw->pcm_ops->run_buffer_out(hw);
            }

            continue;
        }

        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            if (sw->active) {
                size_t sw_free = audio_get_free(sw);
                size_t free;

                if (hw_free > sw->total_hw_samples_mixed) {
                    free = st_rate_frames_in(sw->rate,
                        MIN(sw_free, hw_free - sw->total_hw_samples_mixed));
                } else {
                    free = 0;
                }
                if (free > sw->resample_buf.pos) {
                    free = MIN(free, sw->resample_buf.size)
                           - sw->resample_buf.pos;
                    sw->callback.fn(sw->callback.opaque,
                                    free * sw->info.bytes_per_frame);
                }
            }
        }

        live = audio_pcm_hw_get_live_out (hw, &nb_live);
        if (!nb_live) {
            live = 0;
        }

        if (audio_bug(__func__, live > hw->mix_buf.size)) {
            dolog("live=%zu hw->mix_buf.size=%zu\n", live, hw->mix_buf.size);
            continue;
        }

        if (hw->pending_disable && !nb_live) {
            SWVoiceCap *sc;
#ifdef DEBUG_OUT
            dolog ("Disabling voice\n");
#endif
            hw->enabled = 0;
            hw->pending_disable = 0;
            if (hw->pcm_ops->enable_out) {
                hw->pcm_ops->enable_out(hw, false);
            }
            for (sc = hw->cap_head.lh_first; sc; sc = sc->entries.le_next) {
                sc->sw.active = 0;
                audio_recalc_and_notify_capture (sc->cap);
            }
            continue;
        }

        if (!live) {
            if (hw->pcm_ops->run_buffer_out) {
                hw->pcm_ops->run_buffer_out(hw);
            }
            continue;
        }

        prev_rpos = hw->mix_buf.pos;
        played = audio_pcm_hw_run_out(hw, live);
        replay_audio_out(&played);
        if (audio_bug(__func__, hw->mix_buf.pos >= hw->mix_buf.size)) {
            dolog("hw->mix_buf.pos=%zu hw->mix_buf.size=%zu played=%zu\n",
                  hw->mix_buf.pos, hw->mix_buf.size, played);
            hw->mix_buf.pos = 0;
        }

#ifdef DEBUG_OUT
        dolog("played=%zu\n", played);
#endif

        if (played) {
            hw->ts_helper += played;
            audio_capture_mix_and_clear (hw, prev_rpos, played);
        }

        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            if (!sw->active && sw->empty) {
                continue;
            }

            if (audio_bug(__func__, played > sw->total_hw_samples_mixed)) {
                dolog("played=%zu sw->total_hw_samples_mixed=%zu\n",
                      played, sw->total_hw_samples_mixed);
                played = sw->total_hw_samples_mixed;
            }

            sw->total_hw_samples_mixed -= played;

            if (!sw->total_hw_samples_mixed) {
                sw->empty = 1;
            }
        }
    }
}

static size_t audio_pcm_hw_run_in(HWVoiceIn *hw, size_t samples)
{
    size_t conv = 0;

    if (hw->pcm_ops->run_buffer_in) {
        hw->pcm_ops->run_buffer_in(hw);
    }

    while (samples) {
        size_t proc;
        size_t size = samples * hw->info.bytes_per_frame;
        void *buf = hw->pcm_ops->get_buffer_in(hw, &size);

        assert(size % hw->info.bytes_per_frame == 0);
        if (size == 0) {
            break;
        }

        proc = audio_pcm_hw_conv_in(hw, buf, size / hw->info.bytes_per_frame);

        samples -= proc;
        conv += proc;
        hw->pcm_ops->put_buffer_in(hw, buf, proc * hw->info.bytes_per_frame);
    }

    return conv;
}

static void audio_run_in (AudioState *s)
{
    HWVoiceIn *hw = NULL;

    if (!audio_get_pdo_in(s->dev)->mixing_engine) {
        while ((hw = audio_pcm_hw_find_any_enabled_in(s, hw))) {
            /* there is exactly 1 sw for each hw with no mixeng */
            SWVoiceIn *sw = hw->sw_head.lh_first;
            if (sw->active) {
                sw->callback.fn(sw->callback.opaque, INT_MAX);
            }
        }
        return;
    }

    while ((hw = audio_pcm_hw_find_any_enabled_in(s, hw))) {
        SWVoiceIn *sw;
        size_t captured = 0, min;

        if (replay_mode != REPLAY_MODE_PLAY) {
            captured = audio_pcm_hw_run_in(
                hw, hw->conv_buf.size - audio_pcm_hw_get_live_in(hw));
        }
        replay_audio_in(&captured, hw->conv_buf.buffer, &hw->conv_buf.pos,
                        hw->conv_buf.size);

        min = audio_pcm_hw_find_min_in (hw);
        hw->total_samples_captured += captured - min;
        hw->ts_helper += captured;

        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            sw->total_hw_samples_acquired -= min;

            if (sw->active) {
                size_t sw_avail = audio_get_avail(sw);
                size_t avail;

                avail = st_rate_frames_out(sw->rate, sw_avail);
                if (avail > 0) {
                    avail = MIN(avail, sw->resample_buf.size);
                    sw->callback.fn(sw->callback.opaque,
                                    avail * sw->info.bytes_per_frame);
                }
            }
        }
    }
}

static void audio_run_capture (AudioState *s)
{
    CaptureVoiceOut *cap;

    for (cap = s->cap_head.lh_first; cap; cap = cap->entries.le_next) {
        size_t live, rpos, captured;
        HWVoiceOut *hw = &cap->hw;
        SWVoiceOut *sw;

        captured = live = audio_pcm_hw_get_live_out (hw, NULL);
        rpos = hw->mix_buf.pos;
        while (live) {
            size_t left = hw->mix_buf.size - rpos;
            size_t to_capture = MIN(live, left);
            struct st_sample *src;
            struct capture_callback *cb;

            src = hw->mix_buf.buffer + rpos;
            hw->clip (cap->buf, src, to_capture);
            mixeng_clear (src, to_capture);

            for (cb = cap->cb_head.lh_first; cb; cb = cb->entries.le_next) {
                cb->ops.capture (cb->opaque, cap->buf,
                                 to_capture * hw->info.bytes_per_frame);
            }
            rpos = (rpos + to_capture) % hw->mix_buf.size;
            live -= to_capture;
        }
        hw->mix_buf.pos = rpos;

        for (sw = hw->sw_head.lh_first; sw; sw = sw->entries.le_next) {
            if (!sw->active && sw->empty) {
                continue;
            }

            if (audio_bug(__func__, captured > sw->total_hw_samples_mixed)) {
                dolog("captured=%zu sw->total_hw_samples_mixed=%zu\n",
                      captured, sw->total_hw_samples_mixed);
                captured = sw->total_hw_samples_mixed;
            }

            sw->total_hw_samples_mixed -= captured;
            sw->empty = sw->total_hw_samples_mixed == 0;
        }
    }
}

void audio_run(AudioState *s, const char *msg)
{
    audio_run_out(s);
    audio_run_in(s);
    audio_run_capture(s);

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

void audio_generic_run_buffer_in(HWVoiceIn *hw)
{
    if (unlikely(!hw->buf_emul)) {
        hw->size_emul = hw->samples * hw->info.bytes_per_frame;
        hw->buf_emul = g_malloc(hw->size_emul);
        hw->pos_emul = hw->pending_emul = 0;
    }

    while (hw->pending_emul < hw->size_emul) {
        size_t read_len = MIN(hw->size_emul - hw->pos_emul,
                              hw->size_emul - hw->pending_emul);
        size_t read = hw->pcm_ops->read(hw, hw->buf_emul + hw->pos_emul,
                                        read_len);
        hw->pending_emul += read;
        hw->pos_emul = (hw->pos_emul + read) % hw->size_emul;
        if (read < read_len) {
            break;
        }
    }
}

void *audio_generic_get_buffer_in(HWVoiceIn *hw, size_t *size)
{
    size_t start;

    start = audio_ring_posb(hw->pos_emul, hw->pending_emul, hw->size_emul);
    assert(start < hw->size_emul);

    *size = MIN(*size, hw->pending_emul);
    *size = MIN(*size, hw->size_emul - start);
    return hw->buf_emul + start;
}

void audio_generic_put_buffer_in(HWVoiceIn *hw, void *buf, size_t size)
{
    assert(size <= hw->pending_emul);
    hw->pending_emul -= size;
}

size_t audio_generic_buffer_get_free(HWVoiceOut *hw)
{
    if (hw->buf_emul) {
        return hw->size_emul - hw->pending_emul;
    } else {
        return hw->samples * hw->info.bytes_per_frame;
    }
}

void audio_generic_run_buffer_out(HWVoiceOut *hw)
{
    while (hw->pending_emul) {
        size_t write_len, written, start;

        start = audio_ring_posb(hw->pos_emul, hw->pending_emul, hw->size_emul);
        assert(start < hw->size_emul);

        write_len = MIN(hw->pending_emul, hw->size_emul - start);

        written = hw->pcm_ops->write(hw, hw->buf_emul + start, write_len);
        hw->pending_emul -= written;

        if (written < write_len) {
            break;
        }
    }
}

void *audio_generic_get_buffer_out(HWVoiceOut *hw, size_t *size)
{
    if (unlikely(!hw->buf_emul)) {
        hw->size_emul = hw->samples * hw->info.bytes_per_frame;
        hw->buf_emul = g_malloc(hw->size_emul);
        hw->pos_emul = hw->pending_emul = 0;
    }

    *size = MIN(hw->size_emul - hw->pending_emul,
                hw->size_emul - hw->pos_emul);
    return hw->buf_emul + hw->pos_emul;
}

size_t audio_generic_put_buffer_out(HWVoiceOut *hw, void *buf, size_t size)
{
    assert(buf == hw->buf_emul + hw->pos_emul &&
           size + hw->pending_emul <= hw->size_emul);

    hw->pending_emul += size;
    hw->pos_emul = (hw->pos_emul + size) % hw->size_emul;

    return size;
}

size_t audio_generic_write(HWVoiceOut *hw, void *buf, size_t size)
{
    size_t total = 0;

    if (hw->pcm_ops->buffer_get_free) {
        size_t free = hw->pcm_ops->buffer_get_free(hw);

        size = MIN(size, free);
    }

    while (total < size) {
        size_t dst_size = size - total;
        size_t copy_size, proc;
        void *dst = hw->pcm_ops->get_buffer_out(hw, &dst_size);

        if (dst_size == 0) {
            break;
        }

        copy_size = MIN(size - total, dst_size);
        if (dst) {
            memcpy(dst, (char *)buf + total, copy_size);
        }
        proc = hw->pcm_ops->put_buffer_out(hw, dst, copy_size);
        total += proc;

        if (proc == 0 || proc < copy_size) {
            break;
        }
    }

    return total;
}

size_t audio_generic_read(HWVoiceIn *hw, void *buf, size_t size)
{
    size_t total = 0;

    if (hw->pcm_ops->run_buffer_in) {
        hw->pcm_ops->run_buffer_in(hw);
    }

    while (total < size) {
        size_t src_size = size - total;
        void *src = hw->pcm_ops->get_buffer_in(hw, &src_size);

        if (src_size == 0) {
            break;
        }

        memcpy((char *)buf + total, src, src_size);
        hw->pcm_ops->put_buffer_in(hw, src, src_size);
        total += src_size;
    }

    return total;
}

static int audio_driver_init(AudioState *s, struct audio_driver *drv,
                             Audiodev *dev, Error **errp)
{
    Error *local_err = NULL;

    s->drv_opaque = drv->init(dev, &local_err);

    if (s->drv_opaque) {
        if (!drv->pcm_ops->get_buffer_in) {
            drv->pcm_ops->get_buffer_in = audio_generic_get_buffer_in;
            drv->pcm_ops->put_buffer_in = audio_generic_put_buffer_in;
        }
        if (!drv->pcm_ops->get_buffer_out) {
            drv->pcm_ops->get_buffer_out = audio_generic_get_buffer_out;
            drv->pcm_ops->put_buffer_out = audio_generic_put_buffer_out;
        }

        audio_init_nb_voices_out(s, drv, 1);
        audio_init_nb_voices_in(s, drv, 0);
        s->drv = drv;
        return 0;
    } else {
        if (local_err) {
            error_propagate(errp, local_err);
        } else {
            error_setg(errp, "Could not init `%s' audio driver", drv->name);
        }
        return -1;
    }
}

static void audio_vm_change_state_handler (void *opaque, bool running,
                                           RunState state)
{
    AudioState *s = opaque;
    HWVoiceOut *hwo = NULL;
    HWVoiceIn *hwi = NULL;

    s->vm_running = running;
    while ((hwo = audio_pcm_hw_find_any_enabled_out(s, hwo))) {
        if (hwo->pcm_ops->enable_out) {
            hwo->pcm_ops->enable_out(hwo, running);
        }
    }

    while ((hwi = audio_pcm_hw_find_any_enabled_in(s, hwi))) {
        if (hwi->pcm_ops->enable_in) {
            hwi->pcm_ops->enable_in(hwi, running);
        }
    }
    audio_reset_timer (s);
}

static void free_audio_state(AudioState *s)
{
    HWVoiceOut *hwo, *hwon;
    HWVoiceIn *hwi, *hwin;

    QLIST_FOREACH_SAFE(hwo, &s->hw_head_out, entries, hwon) {
        SWVoiceCap *sc;

        if (hwo->enabled && hwo->pcm_ops->enable_out) {
            hwo->pcm_ops->enable_out(hwo, false);
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

    QLIST_FOREACH_SAFE(hwi, &s->hw_head_in, entries, hwin) {
        if (hwi->enabled && hwi->pcm_ops->enable_in) {
            hwi->pcm_ops->enable_in(hwi, false);
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

    if (s->ts) {
        timer_free(s->ts);
        s->ts = NULL;
    }

    g_free(s);
}

void audio_cleanup(void)
{
    default_audio_state = NULL;
    while (!QTAILQ_EMPTY(&audio_states)) {
        AudioState *s = QTAILQ_FIRST(&audio_states);
        QTAILQ_REMOVE(&audio_states, s, list);
        free_audio_state(s);
    }
}

static bool vmstate_audio_needed(void *opaque)
{
    /*
     * Never needed, this vmstate only exists in case
     * an old qemu sends it to us.
     */
    return false;
}

static const VMStateDescription vmstate_audio = {
    .name = "audio",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_audio_needed,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

void audio_create_default_audiodevs(void)
{
    for (int i = 0; audio_prio_list[i]; i++) {
        if (audio_driver_lookup(audio_prio_list[i])) {
            QDict *dict = qdict_new();
            Audiodev *dev = NULL;
            Visitor *v;

            qdict_put_str(dict, "driver", audio_prio_list[i]);
            qdict_put_str(dict, "id", "#default");

            v = qobject_input_visitor_new_keyval(QOBJECT(dict));
            qobject_unref(dict);
            visit_type_Audiodev(v, NULL, &dev, &error_fatal);
            visit_free(v);

            audio_define_default(dev, &error_abort);
        }
    }
}

/*
 * if we have dev, this function was called because of an -audiodev argument =>
 *   initialize a new state with it
 * if dev == NULL => legacy implicit initialization, return the already created
 *   state or create a new one
 */
static AudioState *audio_init(Audiodev *dev, Error **errp)
{
    static bool atexit_registered;
    int done = 0;
    const char *drvname;
    VMChangeStateEntry *vmse;
    AudioState *s;
    struct audio_driver *driver;

    s = g_new0(AudioState, 1);

    QLIST_INIT (&s->hw_head_out);
    QLIST_INIT (&s->hw_head_in);
    QLIST_INIT (&s->cap_head);
    if (!atexit_registered) {
        atexit(audio_cleanup);
        atexit_registered = true;
    }

    s->ts = timer_new_ns(QEMU_CLOCK_VIRTUAL, audio_timer, s);

    if (dev) {
        /* -audiodev option */
        s->dev = dev;
        drvname = AudiodevDriver_str(dev->driver);
        driver = audio_driver_lookup(drvname);
        if (driver) {
            done = !audio_driver_init(s, driver, dev, errp);
        } else {
            error_setg(errp, "Unknown audio driver `%s'\n", drvname);
        }
        if (!done) {
            goto out;
        }
    } else {
        assert(!default_audio_state);
        for (;;) {
            AudiodevListEntry *e = QSIMPLEQ_FIRST(&default_audiodevs);
            if (!e) {
                error_setg(errp, "no default audio driver available");
                goto out;
            }
            s->dev = dev = e->dev;
            QSIMPLEQ_REMOVE_HEAD(&default_audiodevs, next);
            g_free(e);
            drvname = AudiodevDriver_str(dev->driver);
            driver = audio_driver_lookup(drvname);
            if (!audio_driver_init(s, driver, dev, NULL)) {
                break;
            }
            qapi_free_Audiodev(dev);
            s->dev = NULL;
        }
    }

    if (dev->timer_period <= 0) {
        s->period_ticks = 1;
    } else {
        s->period_ticks = dev->timer_period * (int64_t)SCALE_US;
    }

    vmse = qemu_add_vm_change_state_handler (audio_vm_change_state_handler, s);
    if (!vmse) {
        dolog ("warning: Could not register change state handler\n"
               "(Audio can continue looping even after stopping the VM)\n");
    }

    QTAILQ_INSERT_TAIL(&audio_states, s, list);
    QLIST_INIT (&s->card_head);
    vmstate_register_any(NULL, &vmstate_audio, s);
    return s;

out:
    free_audio_state(s);
    return NULL;
}

AudioState *audio_get_default_audio_state(Error **errp)
{
    if (!default_audio_state) {
        default_audio_state = audio_init(NULL, errp);
        if (!default_audio_state) {
            if (!QSIMPLEQ_EMPTY(&audiodevs)) {
                error_append_hint(errp, "Perhaps you wanted to use -audio or set audiodev=%s?\n",
                                  QSIMPLEQ_FIRST(&audiodevs)->dev->id);
            }
        }
    }

    return default_audio_state;
}

bool AUD_register_card (const char *name, QEMUSoundCard *card, Error **errp)
{
    if (!card->state) {
        card->state = audio_get_default_audio_state(errp);
        if (!card->state) {
            return false;
        }
    }

    card->name = g_strdup (name);
    memset (&card->entries, 0, sizeof (card->entries));
    QLIST_INSERT_HEAD(&card->state->card_head, card, entries);

    return true;
}

void AUD_remove_card (QEMUSoundCard *card)
{
    QLIST_REMOVE (card, entries);
    g_free (card->name);
}

static struct audio_pcm_ops capture_pcm_ops;

CaptureVoiceOut *AUD_add_capture(
    AudioState *s,
    struct audsettings *as,
    struct audio_capture_ops *ops,
    void *cb_opaque
    )
{
    CaptureVoiceOut *cap;
    struct capture_callback *cb;

    if (!s) {
        error_report("Capturing without setting an audiodev is not supported");
        abort();
    }

    if (!audio_get_pdo_out(s->dev)->mixing_engine) {
        dolog("Can't capture with mixeng disabled\n");
        return NULL;
    }

    if (audio_validate_settings (as)) {
        dolog ("Invalid settings were passed when trying to add capture\n");
        audio_print_settings (as);
        return NULL;
    }

    cb = g_malloc0(sizeof(*cb));
    cb->ops = *ops;
    cb->opaque = cb_opaque;

    cap = audio_pcm_capture_find_specific(s, as);
    if (cap) {
        QLIST_INSERT_HEAD (&cap->cb_head, cb, entries);
    } else {
        HWVoiceOut *hw;

        cap = g_malloc0(sizeof(*cap));

        hw = &cap->hw;
        hw->s = s;
        hw->pcm_ops = &capture_pcm_ops;
        QLIST_INIT (&hw->sw_head);
        QLIST_INIT (&cap->cb_head);

        /* XXX find a more elegant way */
        hw->samples = 4096 * 4;
        audio_pcm_hw_alloc_resources_out(hw);

        audio_pcm_init_info (&hw->info, as);

        cap->buf = g_malloc0_n(hw->mix_buf.size, hw->info.bytes_per_frame);

        if (hw->info.is_float) {
            hw->clip = mixeng_clip_float[hw->info.nchannels == 2];
        } else {
            hw->clip = mixeng_clip
                [hw->info.nchannels == 2]
                [hw->info.is_signed]
                [hw->info.swap_endianness]
                [audio_bits_to_index(hw->info.bits)];
        }

        QLIST_INSERT_HEAD (&s->cap_head, cap, entries);
        QLIST_INSERT_HEAD (&cap->cb_head, cb, entries);

        QLIST_FOREACH(hw, &s->hw_head_out, entries) {
            audio_attach_capture (hw);
        }
    }

    return cap;
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
                g_free(cap->hw.mix_buf.buffer);
                g_free (cap->buf);
                g_free (cap);
            }
            return;
        }
    }
}

void AUD_set_volume_out (SWVoiceOut *sw, int mute, uint8_t lvol, uint8_t rvol)
{
    Volume vol = { .mute = mute, .channels = 2, .vol = { lvol, rvol } };
    audio_set_volume_out(sw, &vol);
}

void audio_set_volume_out(SWVoiceOut *sw, Volume *vol)
{
    if (sw) {
        HWVoiceOut *hw = sw->hw;

        sw->vol.mute = vol->mute;
        sw->vol.l = nominal_volume.l * vol->vol[0] / 255;
        sw->vol.r = nominal_volume.l * vol->vol[vol->channels > 1 ? 1 : 0] /
            255;

        if (hw->pcm_ops->volume_out) {
            hw->pcm_ops->volume_out(hw, vol);
        }
    }
}

void AUD_set_volume_in (SWVoiceIn *sw, int mute, uint8_t lvol, uint8_t rvol)
{
    Volume vol = { .mute = mute, .channels = 2, .vol = { lvol, rvol } };
    audio_set_volume_in(sw, &vol);
}

void audio_set_volume_in(SWVoiceIn *sw, Volume *vol)
{
    if (sw) {
        HWVoiceIn *hw = sw->hw;

        sw->vol.mute = vol->mute;
        sw->vol.l = nominal_volume.l * vol->vol[0] / 255;
        sw->vol.r = nominal_volume.r * vol->vol[vol->channels > 1 ? 1 : 0] /
            255;

        if (hw->pcm_ops->volume_in) {
            hw->pcm_ops->volume_in(hw, vol);
        }
    }
}

void audio_create_pdos(Audiodev *dev)
{
    switch (dev->driver) {
#define CASE(DRIVER, driver, pdo_name)                              \
    case AUDIODEV_DRIVER_##DRIVER:                                  \
        if (!dev->u.driver.in) {                                    \
            dev->u.driver.in = g_malloc0(                           \
                sizeof(Audiodev##pdo_name##PerDirectionOptions));   \
        }                                                           \
        if (!dev->u.driver.out) {                                   \
            dev->u.driver.out = g_malloc0(                          \
                sizeof(Audiodev##pdo_name##PerDirectionOptions));   \
        }                                                           \
        break

        CASE(NONE, none, );
#ifdef CONFIG_AUDIO_ALSA
        CASE(ALSA, alsa, Alsa);
#endif
#ifdef CONFIG_AUDIO_COREAUDIO
        CASE(COREAUDIO, coreaudio, Coreaudio);
#endif
#ifdef CONFIG_DBUS_DISPLAY
        CASE(DBUS, dbus, );
#endif
#ifdef CONFIG_AUDIO_DSOUND
        CASE(DSOUND, dsound, );
#endif
#ifdef CONFIG_AUDIO_JACK
        CASE(JACK, jack, Jack);
#endif
#ifdef CONFIG_AUDIO_OSS
        CASE(OSS, oss, Oss);
#endif
#ifdef CONFIG_AUDIO_PA
        CASE(PA, pa, Pa);
#endif
#ifdef CONFIG_AUDIO_PIPEWIRE
        CASE(PIPEWIRE, pipewire, Pipewire);
#endif
#ifdef CONFIG_AUDIO_SDL
        CASE(SDL, sdl, Sdl);
#endif
#ifdef CONFIG_AUDIO_SNDIO
        CASE(SNDIO, sndio, );
#endif
#ifdef CONFIG_SPICE
        CASE(SPICE, spice, );
#endif
        CASE(WAV, wav, );

    case AUDIODEV_DRIVER__MAX:
        abort();
    };
}

static void audio_validate_per_direction_opts(
    AudiodevPerDirectionOptions *pdo, Error **errp)
{
    if (!pdo->has_mixing_engine) {
        pdo->has_mixing_engine = true;
        pdo->mixing_engine = true;
    }
    if (!pdo->has_fixed_settings) {
        pdo->has_fixed_settings = true;
        pdo->fixed_settings = pdo->mixing_engine;
    }
    if (!pdo->fixed_settings &&
        (pdo->has_frequency || pdo->has_channels || pdo->has_format)) {
        error_setg(errp,
                   "You can't use frequency, channels or format with fixed-settings=off");
        return;
    }
    if (!pdo->mixing_engine && pdo->fixed_settings) {
        error_setg(errp, "You can't use fixed-settings without mixeng");
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
        pdo->voices = pdo->mixing_engine ? 1 : INT_MAX;
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

void audio_help(void)
{
    int i;

    printf("Available audio drivers:\n");

    for (i = 0; i < AUDIODEV_DRIVER__MAX; i++) {
        audio_driver *driver = audio_driver_lookup(AudiodevDriver_str(i));
        if (driver) {
            printf("%s\n", driver->name);
        }
    }
}

void audio_parse_option(const char *opt)
{
    Audiodev *dev = NULL;

    if (is_help_option(opt)) {
        audio_help();
        exit(EXIT_SUCCESS);
    }
    Visitor *v = qobject_input_visitor_new_str(opt, "driver", &error_fatal);
    visit_type_Audiodev(v, NULL, &dev, &error_fatal);
    visit_free(v);

    audio_define(dev);
}

void audio_define(Audiodev *dev)
{
    AudiodevListEntry *e;

    audio_validate_opts(dev, &error_fatal);

    e = g_new0(AudiodevListEntry, 1);
    e->dev = dev;
    QSIMPLEQ_INSERT_TAIL(&audiodevs, e, next);
}

void audio_define_default(Audiodev *dev, Error **errp)
{
    AudiodevListEntry *e;

    audio_validate_opts(dev, errp);

    e = g_new0(AudiodevListEntry, 1);
    e->dev = dev;
    QSIMPLEQ_INSERT_TAIL(&default_audiodevs, e, next);
}

void audio_init_audiodevs(void)
{
    AudiodevListEntry *e;

    QSIMPLEQ_FOREACH(e, &audiodevs, next) {
        audio_init(e->dev, &error_fatal);
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
    case AUDIO_FORMAT_F32:
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

AudioState *audio_state_by_name(const char *name, Error **errp)
{
    AudioState *s;
    QTAILQ_FOREACH(s, &audio_states, list) {
        assert(s->dev);
        if (strcmp(name, s->dev->id) == 0) {
            return s;
        }
    }
    error_setg(errp, "audiodev '%s' not found", name);
    return NULL;
}

const char *audio_get_id(QEMUSoundCard *card)
{
    if (card->state) {
        assert(card->state->dev);
        return card->state->dev->id;
    } else {
        return "";
    }
}

const char *audio_application_name(void)
{
    const char *vm_name;

    vm_name = qemu_get_vm_name();
    return vm_name ? vm_name : "qemu";
}

void audio_rate_start(RateCtl *rate)
{
    memset(rate, 0, sizeof(RateCtl));
    rate->start_ticks = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

size_t audio_rate_peek_bytes(RateCtl *rate, struct audio_pcm_info *info)
{
    int64_t now;
    int64_t ticks;
    int64_t bytes;
    int64_t frames;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ticks = now - rate->start_ticks;
    bytes = muldiv64(ticks, info->bytes_per_second, NANOSECONDS_PER_SECOND);
    frames = (bytes - rate->bytes_sent) / info->bytes_per_frame;
    if (frames < 0 || frames > 65536) {
        AUD_log(NULL, "Resetting rate control (%" PRId64 " frames)\n", frames);
        audio_rate_start(rate);
        frames = 0;
    }

    return frames * info->bytes_per_frame;
}

void audio_rate_add_bytes(RateCtl *rate, size_t bytes_used)
{
    rate->bytes_sent += bytes_used;
}

size_t audio_rate_get_bytes(RateCtl *rate, struct audio_pcm_info *info,
                            size_t bytes_avail)
{
    size_t bytes;

    bytes = audio_rate_peek_bytes(rate, info);
    bytes = MIN(bytes, bytes_avail);
    audio_rate_add_bytes(rate, bytes);

    return bytes;
}

AudiodevList *qmp_query_audiodevs(Error **errp)
{
    AudiodevList *ret = NULL;
    AudiodevListEntry *e;
    QSIMPLEQ_FOREACH(e, &audiodevs, next) {
        QAPI_LIST_PREPEND(ret, QAPI_CLONE(Audiodev, e->dev));
    }
    return ret;
}
