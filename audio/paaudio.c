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
    int divisor;
    char *server;
    char *sink;
    char *source;
} conf = {
    1024,
    2,
    NULL,
    NULL,
    NULL
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
    int threshold;

    threshold = conf.divisor ? hw->samples / conf.divisor : 0;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return NULL;
    }

    for (;;) {
        int decr, to_mix, rpos;

        for (;;) {
            if (pa->done) {
                goto exit;
            }

            if (pa->live > threshold) {
                break;
            }

            if (audio_pt_wait (&pa->pt, AUDIO_FUNC)) {
                goto exit;
            }
        }

        decr = to_mix = pa->live;
        rpos = hw->rpos;

        if (audio_pt_unlock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        while (to_mix) {
            int error;
            int chunk = audio_MIN (to_mix, hw->samples - rpos);
            st_sample_t *src = hw->mix_buf + rpos;

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

static int qpa_run_out (HWVoiceOut *hw)
{
    int live, decr;
    PAVoiceOut *pa = (PAVoiceOut *) hw;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return 0;
    }

    live = audio_pcm_hw_get_live_out (hw);
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
    int threshold;

    threshold = conf.divisor ? hw->samples / conf.divisor : 0;

    if (audio_pt_lock (&pa->pt, AUDIO_FUNC)) {
        return NULL;
    }

    for (;;) {
        int incr, to_grab, wpos;

        for (;;) {
            if (pa->done) {
                goto exit;
            }

            if (pa->dead > threshold) {
                break;
            }

            if (audio_pt_wait (&pa->pt, AUDIO_FUNC)) {
                goto exit;
            }
        }

        incr = to_grab = pa->dead;
        wpos = hw->wpos;

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

            hw->conv (hw->conv_buf + wpos, buf, chunk, &nominal_volume);
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

static int qpa_init_out (HWVoiceOut *hw, audsettings_t *as)
{
    int error;
    static pa_sample_spec ss;
    audsettings_t obt_as = *as;
    PAVoiceOut *pa = (PAVoiceOut *) hw;

    ss.format = audfmt_to_pa (as->fmt, as->endianness);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.endianness);

    pa->s = pa_simple_new (
        conf.server,
        "qemu",
        PA_STREAM_PLAYBACK,
        conf.sink,
        "pcm.playback",
        &ss,
        NULL,                   /* channel map */
        NULL,                   /* buffering attributes */
        &error
        );
    if (!pa->s) {
        qpa_logerr (error, "pa_simple_new for playback failed\n");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = conf.samples;
    pa->pcm_buf = audio_calloc (AUDIO_FUNC, hw->samples, 1 << hw->info.shift);
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
    free (pa->pcm_buf);
    pa->pcm_buf = NULL;
 fail2:
    pa_simple_free (pa->s);
    pa->s = NULL;
 fail1:
    return -1;
}

static int qpa_init_in (HWVoiceIn *hw, audsettings_t *as)
{
    int error;
    static pa_sample_spec ss;
    audsettings_t obt_as = *as;
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
    free (pa->pcm_buf);
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
    {"SAMPLES", AUD_OPT_INT, &conf.samples,
     "buffer size in samples", NULL, 0},

    {"DIVISOR", AUD_OPT_INT, &conf.divisor,
     "threshold divisor", NULL, 0},

    {"SERVER", AUD_OPT_STR, &conf.server,
     "server address", NULL, 0},

    {"SINK", AUD_OPT_STR, &conf.sink,
     "sink device name", NULL, 0},

    {"SOURCE", AUD_OPT_STR, &conf.source,
     "source device name", NULL, 0},

    {NULL, 0, NULL, NULL, NULL, 0}
};

static const struct audio_pcm_ops qpa_pcm_ops = {
    qpa_init_out,
    qpa_fini_out,
    qpa_run_out,
    qpa_write,
    qpa_ctl_out,
    qpa_init_in,
    qpa_fini_in,
    qpa_run_in,
    qpa_read,
    qpa_ctl_in
};

struct audio_driver pa_audio_driver = {
    INIT_FIELD (name           = ) "pa",
    INIT_FIELD (descr          = ) "http://www.pulseaudio.org/",
    INIT_FIELD (options        = ) qpa_options,
    INIT_FIELD (init           = ) qpa_audio_init,
    INIT_FIELD (fini           = ) qpa_audio_fini,
    INIT_FIELD (pcm_ops        = ) &qpa_pcm_ops,
    INIT_FIELD (can_be_default = ) 0,
    INIT_FIELD (max_voices_out = ) INT_MAX,
    INIT_FIELD (max_voices_in  = ) INT_MAX,
    INIT_FIELD (voice_size_out = ) sizeof (PAVoiceOut),
    INIT_FIELD (voice_size_in  = ) sizeof (PAVoiceIn)
};
