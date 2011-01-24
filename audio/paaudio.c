/* public domain */
#include "qemu-common.h"
#include "audio.h"

#include <pulse/simple.h>
#include <pulse/error.h>

#define AUDIO_CAP "pulseaudio"
#include "audio_int.h"
#include "audio_pt_int.h"

typedef struct {
    HWVoiceOut hw;
    int done;
    int live;
    int decr;
    int rpos;
    pa_simple *s;
    void *pcm_buf;
    struct audio_pt pt;
} PAVoiceOut;

typedef struct {
    HWVoiceIn hw;
    int done;
    int dead;
    int incr;
    int wpos;
    pa_simple *s;
    void *pcm_buf;
    struct audio_pt pt;
} PAVoiceIn;

static struct {
    int samples;
    char *server;
    char *sink;
    char *source;
} conf = {
    .samples = 4096,
};

static void GCC_FMT_ATTR (2, 3) qpa_logerr (int err, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", pa_strerror (err));
}

static void *qpa_thread_out (void *arg)
{
    PAVoiceOut *pa = arg;
    HWVoiceOut *hw = &pa->hw;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return NULL;
    }

    for (;;) {
        int decr, to_mix, rpos;

        for (;;) {
            if (pa->done) {
                goto exit;
            }

            if (pa->live > 0) {
                break;
            }

            if (audio_pt_wait (&pa->pt, AUDIO_FUNC)) {
                goto exit;
            }
        }

        decr = to_mix = audio_MIN (pa->live, conf.samples >> 2);
        rpos = pa->rpos;

        if (audio_pt_unlock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        while (to_mix) {
            int error;
            int chunk = audio_MIN (to_mix, hw->samples - rpos);
            struct st_sample *src = hw->mix_buf + rpos;

            hw->clip (pa->pcm_buf, src, chunk);

            if (pa_simple_write (pa->s, pa->pcm_buf,
                                 chunk << hw->info.shift, &error) < 0) {
                qpa_logerr (error, "pa_simple_write failed\n");
                return NULL;
            }

            rpos = (rpos + chunk) % hw->samples;
            to_mix -= chunk;
        }

        if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        pa->rpos = rpos;
        pa->live -= decr;
        pa->decr += decr;
    }

 exit:
    audio_pt_unlock (&pa->pt, AUDIO_FUNC);
    return NULL;
}

static int qpa_run_out (HWVoiceOut *hw, int live)
{
    int decr;
    PAVoiceOut *pa = (PAVoiceOut *) hw;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return 0;
    }

    decr = audio_MIN (live, pa->decr);
    pa->decr -= decr;
    pa->live = live - decr;
    hw->rpos = pa->rpos;
    if (pa->live > 0) {
        audio_pt_unlock_and_signal (&pa->pt, AUDIO_FUNC);
    }
    else {
        audio_pt_unlock (&pa->pt, AUDIO_FUNC);
    }
    return decr;
}

static int qpa_write (SWVoiceOut *sw, void *buf, int len)
{
    return audio_pcm_sw_write (sw, buf, len);
}

/* capture */
static void *qpa_thread_in (void *arg)
{
    PAVoiceIn *pa = arg;
    HWVoiceIn *hw = &pa->hw;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return NULL;
    }

    for (;;) {
        int incr, to_grab, wpos;

        for (;;) {
            if (pa->done) {
                goto exit;
            }

            if (pa->dead > 0) {
                break;
            }

            if (audio_pt_wait (&pa->pt, AUDIO_FUNC)) {
                goto exit;
            }
        }

        incr = to_grab = audio_MIN (pa->dead, conf.samples >> 2);
        wpos = pa->wpos;

        if (audio_pt_unlock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        while (to_grab) {
            int error;
            int chunk = audio_MIN (to_grab, hw->samples - wpos);
            void *buf = advance (pa->pcm_buf, wpos);

            if (pa_simple_read (pa->s, buf,
                                chunk << hw->info.shift, &error) < 0) {
                qpa_logerr (error, "pa_simple_read failed\n");
                return NULL;
            }

            hw->conv (hw->conv_buf + wpos, buf, chunk);
            wpos = (wpos + chunk) % hw->samples;
            to_grab -= chunk;
        }

        if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        pa->wpos = wpos;
        pa->dead -= incr;
        pa->incr += incr;
    }

 exit:
    audio_pt_unlock (&pa->pt, AUDIO_FUNC);
    return NULL;
}

static int qpa_run_in (HWVoiceIn *hw)
{
    int live, incr, dead;
    PAVoiceIn *pa = (PAVoiceIn *) hw;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return 0;
    }

    live = audio_pcm_hw_get_live_in (hw);
    dead = hw->samples - live;
    incr = audio_MIN (dead, pa->incr);
    pa->incr -= incr;
    pa->dead = dead - incr;
    hw->wpos = pa->wpos;
    if (pa->dead > 0) {
        audio_pt_unlock_and_signal (&pa->pt, AUDIO_FUNC);
    }
    else {
        audio_pt_unlock (&pa->pt, AUDIO_FUNC);
    }
    return incr;
}

static int qpa_read (SWVoiceIn *sw, void *buf, int len)
{
    return audio_pcm_sw_read (sw, buf, len);
}

static pa_sample_format_t audfmt_to_pa (audfmt_e afmt, int endianness)
{
    int format;

    switch (afmt) {
    case AUD_FMT_S8:
    case AUD_FMT_U8:
        format = PA_SAMPLE_U8;
        break;
    case AUD_FMT_S16:
    case AUD_FMT_U16:
        format = endianness ? PA_SAMPLE_S16BE : PA_SAMPLE_S16LE;
        break;
    case AUD_FMT_S32:
    case AUD_FMT_U32:
        format = endianness ? PA_SAMPLE_S32BE : PA_SAMPLE_S32LE;
        break;
    default:
        dolog ("Internal logic error: Bad audio format %d\n", afmt);
        format = PA_SAMPLE_U8;
        break;
    }
    return format;
}

static audfmt_e pa_to_audfmt (pa_sample_format_t fmt, int *endianness)
{
    switch (fmt) {
    case PA_SAMPLE_U8:
        return AUD_FMT_U8;
    case PA_SAMPLE_S16BE:
        *endianness = 1;
        return AUD_FMT_S16;
    case PA_SAMPLE_S16LE:
        *endianness = 0;
        return AUD_FMT_S16;
    case PA_SAMPLE_S32BE:
        *endianness = 1;
        return AUD_FMT_S32;
    case PA_SAMPLE_S32LE:
        *endianness = 0;
        return AUD_FMT_S32;
    default:
        dolog ("Internal logic error: Bad pa_sample_format %d\n", fmt);
        return AUD_FMT_U8;
    }
}

static int qpa_init_out (HWVoiceOut *hw, struct audsettings *as)
{
    int error;
    static pa_sample_spec ss;
    static pa_buffer_attr ba;
    struct audsettings obt_as = *as;
    PAVoiceOut *pa = (PAVoiceOut *) hw;

    ss.format = audfmt_to_pa (as->fmt, as->endianness);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    /*
     * qemu audio tick runs at 250 Hz (by default), so processing
     * data chunks worth 4 ms of sound should be a good fit.
     */
    ba.tlength = pa_usec_to_bytes (4 * 1000, &ss);
    ba.minreq = pa_usec_to_bytes (2 * 1000, &ss);
    ba.maxlength = -1;
    ba.prebuf = -1;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.endianness);

    pa->s = pa_simple_new (
        conf.server,
        "qemu",
        PA_STREAM_PLAYBACK,
        conf.sink,
        "pcm.playback",
        &ss,
        NULL,                   /* channel map */
        &ba,                    /* buffering attributes */
        &error
        );
    if (!pa->s) {
        qpa_logerr (error, "pa_simple_new for playback failed\n");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = conf.samples;
    pa->pcm_buf = audio_calloc (AUDIO_FUNC, hw->samples, 1 << hw->info.shift);
    pa->rpos = hw->rpos;
    if (!pa->pcm_buf) {
        dolog ("Could not allocate buffer (%d bytes)\n",
               hw->samples << hw->info.shift);
        goto fail2;
    }

    if (audio_pt_init (&pa->pt, qpa_thread_out, hw, AUDIO_CAP, AUDIO_FUNC)) {
        goto fail3;
    }

    return 0;

 fail3:
    qemu_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
 fail2:
    pa_simple_free (pa->s);
    pa->s = NULL;
 fail1:
    return -1;
}

static int qpa_init_in (HWVoiceIn *hw, struct audsettings *as)
{
    int error;
    static pa_sample_spec ss;
    struct audsettings obt_as = *as;
    PAVoiceIn *pa = (PAVoiceIn *) hw;

    ss.format = audfmt_to_pa (as->fmt, as->endianness);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.endianness);

    pa->s = pa_simple_new (
        conf.server,
        "qemu",
        PA_STREAM_RECORD,
        conf.source,
        "pcm.capture",
        &ss,
        NULL,                   /* channel map */
        NULL,                   /* buffering attributes */
        &error
        );
    if (!pa->s) {
        qpa_logerr (error, "pa_simple_new for capture failed\n");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = conf.samples;
    pa->pcm_buf = audio_calloc (AUDIO_FUNC, hw->samples, 1 << hw->info.shift);
    pa->wpos = hw->wpos;
    if (!pa->pcm_buf) {
        dolog ("Could not allocate buffer (%d bytes)\n",
               hw->samples << hw->info.shift);
        goto fail2;
    }

    if (audio_pt_init (&pa->pt, qpa_thread_in, hw, AUDIO_CAP, AUDIO_FUNC)) {
        goto fail3;
    }

    return 0;

 fail3:
    qemu_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
 fail2:
    pa_simple_free (pa->s);
    pa->s = NULL;
 fail1:
    return -1;
}

static void qpa_fini_out (HWVoiceOut *hw)
{
    void *ret;
    PAVoiceOut *pa = (PAVoiceOut *) hw;

    audio_pt_lock (&pa->pt, AUDIO_FUNC);
    pa->done = 1;
    audio_pt_unlock_and_signal (&pa->pt, AUDIO_FUNC);
    audio_pt_join (&pa->pt, &ret, AUDIO_FUNC);

    if (pa->s) {
        pa_simple_free (pa->s);
        pa->s = NULL;
    }

    audio_pt_fini (&pa->pt, AUDIO_FUNC);
    qemu_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
}

static void qpa_fini_in (HWVoiceIn *hw)
{
    void *ret;
    PAVoiceIn *pa = (PAVoiceIn *) hw;

    audio_pt_lock (&pa->pt, AUDIO_FUNC);
    pa->done = 1;
    audio_pt_unlock_and_signal (&pa->pt, AUDIO_FUNC);
    audio_pt_join (&pa->pt, &ret, AUDIO_FUNC);

    if (pa->s) {
        pa_simple_free (pa->s);
        pa->s = NULL;
    }

    audio_pt_fini (&pa->pt, AUDIO_FUNC);
    qemu_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
}

static int qpa_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    (void) hw;
    (void) cmd;
    return 0;
}

static int qpa_ctl_in (HWVoiceIn *hw, int cmd, ...)
{
    (void) hw;
    (void) cmd;
    return 0;
}

/* common */
static void *qpa_audio_init (void)
{
    return &conf;
}

static void qpa_audio_fini (void *opaque)
{
    (void) opaque;
}

struct audio_option qpa_options[] = {
    {
        .name  = "SAMPLES",
        .tag   = AUD_OPT_INT,
        .valp  = &conf.samples,
        .descr = "buffer size in samples"
    },
    {
        .name  = "SERVER",
        .tag   = AUD_OPT_STR,
        .valp  = &conf.server,
        .descr = "server address"
    },
    {
        .name  = "SINK",
        .tag   = AUD_OPT_STR,
        .valp  = &conf.sink,
        .descr = "sink device name"
    },
    {
        .name  = "SOURCE",
        .tag   = AUD_OPT_STR,
        .valp  = &conf.source,
        .descr = "source device name"
    },
    { /* End of list */ }
};

static struct audio_pcm_ops qpa_pcm_ops = {
    .init_out = qpa_init_out,
    .fini_out = qpa_fini_out,
    .run_out  = qpa_run_out,
    .write    = qpa_write,
    .ctl_out  = qpa_ctl_out,

    .init_in  = qpa_init_in,
    .fini_in  = qpa_fini_in,
    .run_in   = qpa_run_in,
    .read     = qpa_read,
    .ctl_in   = qpa_ctl_in
};

struct audio_driver pa_audio_driver = {
    .name           = "pa",
    .descr          = "http://www.pulseaudio.org/",
    .options        = qpa_options,
    .init           = qpa_audio_init,
    .fini           = qpa_audio_fini,
    .pcm_ops        = &qpa_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof (PAVoiceOut),
    .voice_size_in  = sizeof (PAVoiceIn)
};
