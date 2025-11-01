/*
 * QEMU ALSA audio driver
 *
 * Copyright (c) 2005 Vassili Karpov (malc)
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
#include <alsa/asoundlib.h>
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/audio.h"
#include "trace.h"

#pragma GCC diagnostic ignored "-Waddress"

#define AUDIO_CAP "alsa"
#include "audio_int.h"

#define DEBUG_ALSA 0

struct pollhlp {
    snd_pcm_t *handle;
    struct pollfd *pfds;
    int count;
    int mask;
    AudioBackend *s;
};

typedef struct ALSAVoiceOut {
    HWVoiceOut hw;
    snd_pcm_t *handle;
    struct pollhlp pollhlp;
    Audiodev *dev;
} ALSAVoiceOut;

typedef struct ALSAVoiceIn {
    HWVoiceIn hw;
    snd_pcm_t *handle;
    struct pollhlp pollhlp;
    Audiodev *dev;
} ALSAVoiceIn;

struct alsa_params_req {
    int freq;
    snd_pcm_format_t fmt;
    int nchannels;
};

struct alsa_params_obt {
    int freq;
    AudioFormat fmt;
    int endianness;
    int nchannels;
    snd_pcm_uframes_t samples;
};

static void G_GNUC_PRINTF (2, 3) alsa_logerr (int err, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", snd_strerror (err));
}

static void G_GNUC_PRINTF (3, 4) alsa_logerr2 (
    int err,
    const char *typ,
    const char *fmt,
    ...
    )
{
    va_list ap;

    AUD_log (AUDIO_CAP, "Could not initialize %s\n", typ);

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", snd_strerror (err));
}

static void alsa_fini_poll (struct pollhlp *hlp)
{
    int i;
    struct pollfd *pfds = hlp->pfds;

    if (pfds) {
        for (i = 0; i < hlp->count; ++i) {
            qemu_set_fd_handler (pfds[i].fd, NULL, NULL, NULL);
        }
        g_free (pfds);
    }
    hlp->pfds = NULL;
    hlp->count = 0;
    hlp->handle = NULL;
}

static void alsa_anal_close1 (snd_pcm_t **handlep)
{
    int err = snd_pcm_close (*handlep);
    if (err) {
        alsa_logerr (err, "Failed to close PCM handle %p\n", *handlep);
    }
    *handlep = NULL;
}

static void alsa_anal_close (snd_pcm_t **handlep, struct pollhlp *hlp)
{
    alsa_fini_poll (hlp);
    alsa_anal_close1 (handlep);
}

static int alsa_recover (snd_pcm_t *handle)
{
    int err = snd_pcm_prepare (handle);
    if (err < 0) {
        alsa_logerr (err, "Failed to prepare handle %p\n", handle);
        return -1;
    }
    return 0;
}

static int alsa_resume (snd_pcm_t *handle)
{
    int err = snd_pcm_resume (handle);
    if (err < 0) {
        alsa_logerr (err, "Failed to resume handle %p\n", handle);
        return -1;
    }
    return 0;
}

static void alsa_poll_handler (void *opaque)
{
    int err, count;
    snd_pcm_state_t state;
    struct pollhlp *hlp = opaque;
    unsigned short revents;

    count = poll (hlp->pfds, hlp->count, 0);
    if (count < 0) {
        dolog ("alsa_poll_handler: poll %s\n", strerror (errno));
        return;
    }

    if (!count) {
        return;
    }

    /* XXX: ALSA example uses initial count, not the one returned by
       poll, correct? */
    err = snd_pcm_poll_descriptors_revents (hlp->handle, hlp->pfds,
                                            hlp->count, &revents);
    if (err < 0) {
        alsa_logerr (err, "snd_pcm_poll_descriptors_revents");
        return;
    }

    if (!(revents & hlp->mask)) {
        trace_alsa_revents(revents);
        return;
    }

    state = snd_pcm_state (hlp->handle);
    switch (state) {
    case SND_PCM_STATE_SETUP:
        alsa_recover (hlp->handle);
        break;

    case SND_PCM_STATE_XRUN:
        alsa_recover (hlp->handle);
        break;

    case SND_PCM_STATE_SUSPENDED:
        alsa_resume (hlp->handle);
        break;

    case SND_PCM_STATE_PREPARED:
        audio_run(hlp->s, "alsa run (prepared)");
        break;

    case SND_PCM_STATE_RUNNING:
        audio_run(hlp->s, "alsa run (running)");
        break;

    default:
        dolog ("Unexpected state %d\n", state);
    }
}

static int alsa_poll_helper (snd_pcm_t *handle, struct pollhlp *hlp, int mask)
{
    int i, count, err;
    struct pollfd *pfds;

    count = snd_pcm_poll_descriptors_count (handle);
    if (count <= 0) {
        dolog ("Could not initialize poll mode\n"
               "Invalid number of poll descriptors %d\n", count);
        return -1;
    }

    pfds = g_new0(struct pollfd, count);

    err = snd_pcm_poll_descriptors (handle, pfds, count);
    if (err < 0) {
        alsa_logerr (err, "Could not initialize poll mode\n"
                     "Could not obtain poll descriptors\n");
        g_free (pfds);
        return -1;
    }

    for (i = 0; i < count; ++i) {
        if (pfds[i].events & POLLIN) {
            qemu_set_fd_handler (pfds[i].fd, alsa_poll_handler, NULL, hlp);
        }
        if (pfds[i].events & POLLOUT) {
            trace_alsa_pollout(i, pfds[i].fd);
            qemu_set_fd_handler (pfds[i].fd, NULL, alsa_poll_handler, hlp);
        }
        trace_alsa_set_handler(pfds[i].events, i, pfds[i].fd, err);

    }
    hlp->pfds = pfds;
    hlp->count = count;
    hlp->handle = handle;
    hlp->mask = mask;
    return 0;
}

static int alsa_poll_out (HWVoiceOut *hw)
{
    ALSAVoiceOut *alsa = (ALSAVoiceOut *) hw;

    return alsa_poll_helper (alsa->handle, &alsa->pollhlp, POLLOUT);
}

static int alsa_poll_in (HWVoiceIn *hw)
{
    ALSAVoiceIn *alsa = (ALSAVoiceIn *) hw;

    return alsa_poll_helper (alsa->handle, &alsa->pollhlp, POLLIN);
}

static snd_pcm_format_t aud_to_alsafmt(AudioFormat fmt, bool big_endian)
{
    switch (fmt) {
    case AUDIO_FORMAT_S8:
        return SND_PCM_FORMAT_S8;

    case AUDIO_FORMAT_U8:
        return SND_PCM_FORMAT_U8;

    case AUDIO_FORMAT_S16:
        return big_endian ? SND_PCM_FORMAT_S16_BE : SND_PCM_FORMAT_S16_LE;

    case AUDIO_FORMAT_U16:
        return big_endian ? SND_PCM_FORMAT_U16_BE : SND_PCM_FORMAT_U16_LE;

    case AUDIO_FORMAT_S32:
        return big_endian ? SND_PCM_FORMAT_S32_BE : SND_PCM_FORMAT_S32_LE;

    case AUDIO_FORMAT_U32:
        return big_endian ? SND_PCM_FORMAT_U32_BE : SND_PCM_FORMAT_U32_LE;

    case AUDIO_FORMAT_F32:
        return big_endian ? SND_PCM_FORMAT_FLOAT_BE : SND_PCM_FORMAT_FLOAT_LE;

    default:
        dolog ("Internal logic error: Bad audio format %d\n", fmt);
#ifdef DEBUG_AUDIO
        abort ();
#endif
        return SND_PCM_FORMAT_U8;
    }
}

static int alsa_to_audfmt (snd_pcm_format_t alsafmt, AudioFormat *fmt,
                           int *endianness)
{
    switch (alsafmt) {
    case SND_PCM_FORMAT_S8:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_S8;
        break;

    case SND_PCM_FORMAT_U8:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_U8;
        break;

    case SND_PCM_FORMAT_S16_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_S16;
        break;

    case SND_PCM_FORMAT_U16_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_U16;
        break;

    case SND_PCM_FORMAT_S16_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_S16;
        break;

    case SND_PCM_FORMAT_U16_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_U16;
        break;

    case SND_PCM_FORMAT_S32_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_S32;
        break;

    case SND_PCM_FORMAT_U32_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_U32;
        break;

    case SND_PCM_FORMAT_S32_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_S32;
        break;

    case SND_PCM_FORMAT_U32_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_U32;
        break;

    case SND_PCM_FORMAT_FLOAT_LE:
        *endianness = 0;
        *fmt = AUDIO_FORMAT_F32;
        break;

    case SND_PCM_FORMAT_FLOAT_BE:
        *endianness = 1;
        *fmt = AUDIO_FORMAT_F32;
        break;

    default:
        dolog ("Unrecognized audio format %d\n", alsafmt);
        return -1;
    }

    return 0;
}

static void alsa_dump_info (struct alsa_params_req *req,
                            struct alsa_params_obt *obt,
                            snd_pcm_format_t obtfmt,
                            AudiodevAlsaPerDirectionOptions *apdo)
{
    dolog("parameter | requested value | obtained value\n");
    dolog("format    |      %10d |     %10d\n", req->fmt, obtfmt);
    dolog("channels  |      %10d |     %10d\n",
          req->nchannels, obt->nchannels);
    dolog("frequency |      %10d |     %10d\n", req->freq, obt->freq);
    dolog("============================================\n");
    dolog("requested: buffer len %" PRId32 " period len %" PRId32 "\n",
          apdo->buffer_length, apdo->period_length);
    dolog("obtained: samples %ld\n", obt->samples);
}

static void alsa_set_threshold (snd_pcm_t *handle, snd_pcm_uframes_t threshold)
{
    int err;
    snd_pcm_sw_params_t *sw_params;

    snd_pcm_sw_params_alloca (&sw_params);

    err = snd_pcm_sw_params_current (handle, sw_params);
    if (err < 0) {
        dolog ("Could not fully initialize DAC\n");
        alsa_logerr (err, "Failed to get current software parameters\n");
        return;
    }

    err = snd_pcm_sw_params_set_start_threshold (handle, sw_params, threshold);
    if (err < 0) {
        dolog ("Could not fully initialize DAC\n");
        alsa_logerr (err, "Failed to set software threshold to %ld\n",
                     threshold);
        return;
    }

    err = snd_pcm_sw_params (handle, sw_params);
    if (err < 0) {
        dolog ("Could not fully initialize DAC\n");
        alsa_logerr (err, "Failed to set software parameters\n");
        return;
    }
}

static int alsa_open(bool in, struct alsa_params_req *req,
                     struct alsa_params_obt *obt, snd_pcm_t **handlep,
                     Audiodev *dev)
{
    AudiodevAlsaOptions *aopts = &dev->u.alsa;
    AudiodevAlsaPerDirectionOptions *apdo = in ? aopts->in : aopts->out;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *hw_params;
    int err;
    unsigned int freq, nchannels;
    const char *pcm_name = apdo->dev ?: "default";
    snd_pcm_uframes_t obt_buffer_size;
    const char *typ = in ? "ADC" : "DAC";
    snd_pcm_format_t obtfmt;

    freq = req->freq;
    nchannels = req->nchannels;

    snd_pcm_hw_params_alloca (&hw_params);

    err = snd_pcm_open (
        &handle,
        pcm_name,
        in ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,
        SND_PCM_NONBLOCK
        );
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to open `%s':\n", pcm_name);
        return -1;
    }

    err = snd_pcm_hw_params_any (handle, hw_params);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to initialize hardware parameters\n");
        goto err;
    }

    err = snd_pcm_hw_params_set_access (
        handle,
        hw_params,
        SND_PCM_ACCESS_RW_INTERLEAVED
        );
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to set access type\n");
        goto err;
    }

    err = snd_pcm_hw_params_set_format (handle, hw_params, req->fmt);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to set format %d\n", req->fmt);
    }

    err = snd_pcm_hw_params_set_rate_near (handle, hw_params, &freq, 0);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to set frequency %d\n", req->freq);
        goto err;
    }

    err = snd_pcm_hw_params_set_channels_near (
        handle,
        hw_params,
        &nchannels
        );
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to set number of channels %d\n",
                      req->nchannels);
        goto err;
    }

    if (apdo->buffer_length) {
        int dir = 0;
        unsigned int btime = apdo->buffer_length;

        err = snd_pcm_hw_params_set_buffer_time_near(
            handle, hw_params, &btime, &dir);

        if (err < 0) {
            alsa_logerr2(err, typ, "Failed to set buffer time to %" PRId32 "\n",
                         apdo->buffer_length);
            goto err;
        }

        if (apdo->has_buffer_length && btime != apdo->buffer_length) {
            dolog("Requested buffer time %" PRId32
                  " was rejected, using %u\n", apdo->buffer_length, btime);
        }
    }

    if (apdo->period_length) {
        int dir = 0;
        unsigned int ptime = apdo->period_length;

        err = snd_pcm_hw_params_set_period_time_near(handle, hw_params, &ptime,
                                                     &dir);

        if (err < 0) {
            alsa_logerr2(err, typ, "Failed to set period time to %" PRId32 "\n",
                         apdo->period_length);
            goto err;
        }

        if (apdo->has_period_length && ptime != apdo->period_length) {
            dolog("Requested period time %" PRId32 " was rejected, using %d\n",
                  apdo->period_length, ptime);
        }
    }

    err = snd_pcm_hw_params (handle, hw_params);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to apply audio parameters\n");
        goto err;
    }

    err = snd_pcm_hw_params_get_buffer_size (hw_params, &obt_buffer_size);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to get buffer size\n");
        goto err;
    }

    err = snd_pcm_hw_params_get_format (hw_params, &obtfmt);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Failed to get format\n");
        goto err;
    }

    if (alsa_to_audfmt (obtfmt, &obt->fmt, &obt->endianness)) {
        dolog ("Invalid format was returned %d\n", obtfmt);
        goto err;
    }

    err = snd_pcm_prepare (handle);
    if (err < 0) {
        alsa_logerr2 (err, typ, "Could not prepare handle %p\n", handle);
        goto err;
    }

    if (!in && aopts->has_threshold && aopts->threshold) {
        struct audsettings as = { .freq = freq };
        alsa_set_threshold(
            handle,
            audio_buffer_frames(qapi_AudiodevAlsaPerDirectionOptions_base(apdo),
                                &as, aopts->threshold));
    }

    obt->nchannels = nchannels;
    obt->freq = freq;
    obt->samples = obt_buffer_size;

    *handlep = handle;

    if (DEBUG_ALSA || obtfmt != req->fmt ||
        obt->nchannels != req->nchannels || obt->freq != req->freq) {
        dolog ("Audio parameters for %s\n", typ);
        alsa_dump_info(req, obt, obtfmt, apdo);
    }

    return 0;

 err:
    alsa_anal_close1 (&handle);
    return -1;
}

static size_t alsa_buffer_get_free(HWVoiceOut *hw)
{
    ALSAVoiceOut *alsa = (ALSAVoiceOut *)hw;
    snd_pcm_sframes_t avail;
    size_t alsa_free, generic_free, generic_in_use;

    avail = snd_pcm_avail_update(alsa->handle);
    if (avail < 0) {
        if (avail == -EPIPE) {
            if (!alsa_recover(alsa->handle)) {
                avail = snd_pcm_avail_update(alsa->handle);
            }
        }
        if (avail < 0) {
            alsa_logerr(avail,
                        "Could not obtain number of available frames\n");
            avail = 0;
        }
    }

    alsa_free = avail * hw->info.bytes_per_frame;
    generic_free = audio_generic_buffer_get_free(hw);
    generic_in_use = hw->samples * hw->info.bytes_per_frame - generic_free;
    if (generic_in_use) {
        /*
         * This code can only be reached in the unlikely case that
         * snd_pcm_avail_update() returned a larger number of frames
         * than snd_pcm_writei() could write. Make sure that all
         * remaining bytes in the generic buffer can be written.
         */
        alsa_free = alsa_free > generic_in_use ? alsa_free - generic_in_use : 0;
    }

    return alsa_free;
}

static size_t alsa_write(HWVoiceOut *hw, void *buf, size_t len)
{
    ALSAVoiceOut *alsa = (ALSAVoiceOut *) hw;
    size_t pos = 0;
    size_t len_frames = len / hw->info.bytes_per_frame;

    while (len_frames) {
        char *src = advance(buf, pos);
        snd_pcm_sframes_t written;

        written = snd_pcm_writei(alsa->handle, src, len_frames);

        if (written <= 0) {
            switch (written) {
            case 0:
                trace_alsa_wrote_zero(len_frames);
                return pos;

            case -EPIPE:
                if (alsa_recover(alsa->handle)) {
                    alsa_logerr(written, "Failed to write %zu frames\n",
                                len_frames);
                    return pos;
                }
                trace_alsa_xrun_out();
                continue;

            case -ESTRPIPE:
                /*
                 * stream is suspended and waiting for an application
                 * recovery
                 */
                if (alsa_resume(alsa->handle)) {
                    alsa_logerr(written, "Failed to write %zu frames\n",
                                len_frames);
                    return pos;
                }
                trace_alsa_resume_out();
                continue;

            case -EAGAIN:
                return pos;

            default:
                alsa_logerr(written, "Failed to write %zu frames from %p\n",
                            len, src);
                return pos;
            }
        }

        pos += written * hw->info.bytes_per_frame;
        if (written < len_frames) {
            break;
        }
        len_frames -= written;
    }

    return pos;
}

static void alsa_fini_out (HWVoiceOut *hw)
{
    ALSAVoiceOut *alsa = (ALSAVoiceOut *) hw;

    ldebug ("alsa_fini\n");
    alsa_anal_close (&alsa->handle, &alsa->pollhlp);
}

static int alsa_init_out(HWVoiceOut *hw, struct audsettings *as,
                         void *drv_opaque)
{
    ALSAVoiceOut *alsa = (ALSAVoiceOut *) hw;
    struct alsa_params_req req;
    struct alsa_params_obt obt;
    snd_pcm_t *handle;
    struct audsettings obt_as;
    Audiodev *dev = drv_opaque;

    req.fmt = aud_to_alsafmt (as->fmt, as->endianness);
    req.freq = as->freq;
    req.nchannels = as->nchannels;

    if (alsa_open(0, &req, &obt, &handle, dev)) {
        return -1;
    }

    obt_as.freq = obt.freq;
    obt_as.nchannels = obt.nchannels;
    obt_as.fmt = obt.fmt;
    obt_as.endianness = obt.endianness;

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = obt.samples;

    alsa->pollhlp.s = hw->s;
    alsa->handle = handle;
    alsa->dev = dev;
    return 0;
}

#define VOICE_CTL_PAUSE 0
#define VOICE_CTL_PREPARE 1
#define VOICE_CTL_START 2

static int alsa_voice_ctl (snd_pcm_t *handle, const char *typ, int ctl)
{
    int err;

    if (ctl == VOICE_CTL_PAUSE) {
        err = snd_pcm_drop (handle);
        if (err < 0) {
            alsa_logerr (err, "Could not stop %s\n", typ);
            return -1;
        }
    } else {
        err = snd_pcm_prepare (handle);
        if (err < 0) {
            alsa_logerr (err, "Could not prepare handle for %s\n", typ);
            return -1;
        }
        if (ctl == VOICE_CTL_START) {
            err = snd_pcm_start(handle);
            if (err < 0) {
                alsa_logerr (err, "Could not start handle for %s\n", typ);
                return -1;
            }
        }
    }

    return 0;
}

static void alsa_enable_out(HWVoiceOut *hw, bool enable)
{
    ALSAVoiceOut *alsa = (ALSAVoiceOut *) hw;
    AudiodevAlsaPerDirectionOptions *apdo = alsa->dev->u.alsa.out;

    if (enable) {
        bool poll_mode = apdo->try_poll;

        ldebug("enabling voice\n");
        if (poll_mode && alsa_poll_out(hw)) {
            poll_mode = 0;
        }
        hw->poll_mode = poll_mode;
        alsa_voice_ctl(alsa->handle, "playback", VOICE_CTL_PREPARE);
    } else {
        ldebug("disabling voice\n");
        if (hw->poll_mode) {
            hw->poll_mode = 0;
            alsa_fini_poll(&alsa->pollhlp);
        }
        alsa_voice_ctl(alsa->handle, "playback", VOICE_CTL_PAUSE);
    }
}

static int alsa_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    ALSAVoiceIn *alsa = (ALSAVoiceIn *) hw;
    struct alsa_params_req req;
    struct alsa_params_obt obt;
    snd_pcm_t *handle;
    struct audsettings obt_as;
    Audiodev *dev = drv_opaque;

    req.fmt = aud_to_alsafmt (as->fmt, as->endianness);
    req.freq = as->freq;
    req.nchannels = as->nchannels;

    if (alsa_open(1, &req, &obt, &handle, dev)) {
        return -1;
    }

    obt_as.freq = obt.freq;
    obt_as.nchannels = obt.nchannels;
    obt_as.fmt = obt.fmt;
    obt_as.endianness = obt.endianness;

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = obt.samples;

    alsa->pollhlp.s = hw->s;
    alsa->handle = handle;
    alsa->dev = dev;
    return 0;
}

static void alsa_fini_in (HWVoiceIn *hw)
{
    ALSAVoiceIn *alsa = (ALSAVoiceIn *) hw;

    alsa_anal_close (&alsa->handle, &alsa->pollhlp);
}

static size_t alsa_read(HWVoiceIn *hw, void *buf, size_t len)
{
    ALSAVoiceIn *alsa = (ALSAVoiceIn *) hw;
    size_t pos = 0;

    while (len) {
        void *dst = advance(buf, pos);
        snd_pcm_sframes_t nread;

        nread = snd_pcm_readi(
            alsa->handle, dst, len / hw->info.bytes_per_frame);

        if (nread <= 0) {
            switch (nread) {
            case 0:
                trace_alsa_read_zero(len);
                return pos;

            case -EPIPE:
                if (alsa_recover(alsa->handle)) {
                    alsa_logerr(nread, "Failed to read %zu frames\n", len);
                    return pos;
                }
                trace_alsa_xrun_in();
                continue;

            case -EAGAIN:
                return pos;

            default:
                alsa_logerr(nread, "Failed to read %zu frames to %p\n",
                            len, dst);
                return pos;
            }
        }

        pos += nread * hw->info.bytes_per_frame;
        len -= nread * hw->info.bytes_per_frame;
    }

    return pos;
}

static void alsa_enable_in(HWVoiceIn *hw, bool enable)
{
    ALSAVoiceIn *alsa = (ALSAVoiceIn *) hw;
    AudiodevAlsaPerDirectionOptions *apdo = alsa->dev->u.alsa.in;

    if (enable) {
        bool poll_mode = apdo->try_poll;

        ldebug("enabling voice\n");
        if (poll_mode && alsa_poll_in(hw)) {
            poll_mode = 0;
        }
        hw->poll_mode = poll_mode;

        alsa_voice_ctl(alsa->handle, "capture", VOICE_CTL_START);
    } else {
        ldebug ("disabling voice\n");
        if (hw->poll_mode) {
            hw->poll_mode = 0;
            alsa_fini_poll(&alsa->pollhlp);
        }
        alsa_voice_ctl(alsa->handle, "capture", VOICE_CTL_PAUSE);
    }
}

static void alsa_init_per_direction(AudiodevAlsaPerDirectionOptions *apdo)
{
    if (!apdo->has_try_poll) {
        apdo->try_poll = false;
        apdo->has_try_poll = true;
    }
}

static void *alsa_audio_init(Audiodev *dev, Error **errp)
{
    AudiodevAlsaOptions *aopts;
    assert(dev->driver == AUDIODEV_DRIVER_ALSA);

    aopts = &dev->u.alsa;
    alsa_init_per_direction(aopts->in);
    alsa_init_per_direction(aopts->out);

    /* don't set has_* so alsa_open can identify it wasn't set by the user */
    if (!dev->u.alsa.out->has_period_length) {
        /* 256 frames assuming 44100Hz */
        dev->u.alsa.out->period_length = 5805;
    }
    if (!dev->u.alsa.out->has_buffer_length) {
        /* 4096 frames assuming 44100Hz */
        dev->u.alsa.out->buffer_length = 92880;
    }

    if (!dev->u.alsa.in->has_period_length) {
        /* 256 frames assuming 44100Hz */
        dev->u.alsa.in->period_length = 5805;
    }
    if (!dev->u.alsa.in->has_buffer_length) {
        /* 4096 frames assuming 44100Hz */
        dev->u.alsa.in->buffer_length = 92880;
    }

    return dev;
}

static void alsa_audio_fini (void *opaque)
{
}

static struct audio_pcm_ops alsa_pcm_ops = {
    .init_out = alsa_init_out,
    .fini_out = alsa_fini_out,
    .write    = alsa_write,
    .buffer_get_free = alsa_buffer_get_free,
    .run_buffer_out = audio_generic_run_buffer_out,
    .enable_out = alsa_enable_out,

    .init_in  = alsa_init_in,
    .fini_in  = alsa_fini_in,
    .read     = alsa_read,
    .run_buffer_in = audio_generic_run_buffer_in,
    .enable_in = alsa_enable_in,
};

static struct audio_driver alsa_audio_driver = {
    .name           = "alsa",
    .init           = alsa_audio_init,
    .fini           = alsa_audio_fini,
    .pcm_ops        = &alsa_pcm_ops,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof (ALSAVoiceOut),
    .voice_size_in  = sizeof (ALSAVoiceIn)
};

static void register_audio_alsa(void)
{
    audio_driver_register(&alsa_audio_driver);
}
type_init(register_audio_alsa);
