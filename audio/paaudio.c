/* public domain */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "audio.h"

#include <pulse/pulseaudio.h>

#define AUDIO_CAP "pulseaudio"
#include "audio_int.h"
#include "audio_pt_int.h"

typedef struct {
    int samples;
    char *server;
    char *sink;
    char *source;
} PAConf;

typedef struct {
    PAConf conf;
    pa_threaded_mainloop *mainloop;
    pa_context *context;
} paaudio;

typedef struct {
    HWVoiceOut hw;
    int done;
    int live;
    int decr;
    int rpos;
    pa_stream *stream;
    void *pcm_buf;
    struct audio_pt pt;
    paaudio *g;
} PAVoiceOut;

typedef struct {
    HWVoiceIn hw;
    int done;
    int dead;
    int incr;
    int wpos;
    pa_stream *stream;
    void *pcm_buf;
    struct audio_pt pt;
    const void *read_data;
    size_t read_index, read_length;
    paaudio *g;
} PAVoiceIn;

static void qpa_audio_fini(void *opaque);

static void GCC_FMT_ATTR (2, 3) qpa_logerr (int err, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (AUDIO_CAP, fmt, ap);
    va_end (ap);

    AUD_log (AUDIO_CAP, "Reason: %s\n", pa_strerror (err));
}

#ifndef PA_CONTEXT_IS_GOOD
static inline int PA_CONTEXT_IS_GOOD(pa_context_state_t x)
{
    return
        x == PA_CONTEXT_CONNECTING ||
        x == PA_CONTEXT_AUTHORIZING ||
        x == PA_CONTEXT_SETTING_NAME ||
        x == PA_CONTEXT_READY;
}
#endif

#ifndef PA_STREAM_IS_GOOD
static inline int PA_STREAM_IS_GOOD(pa_stream_state_t x)
{
    return
        x == PA_STREAM_CREATING ||
        x == PA_STREAM_READY;
}
#endif

#define CHECK_SUCCESS_GOTO(c, rerror, expression, label)        \
    do {                                                        \
        if (!(expression)) {                                    \
            if (rerror) {                                       \
                *(rerror) = pa_context_errno ((c)->context);    \
            }                                                   \
            goto label;                                         \
        }                                                       \
    } while (0);

#define CHECK_DEAD_GOTO(c, stream, rerror, label)                       \
    do {                                                                \
        if (!(c)->context || !PA_CONTEXT_IS_GOOD (pa_context_get_state((c)->context)) || \
            !(stream) || !PA_STREAM_IS_GOOD (pa_stream_get_state ((stream)))) { \
            if (((c)->context && pa_context_get_state ((c)->context) == PA_CONTEXT_FAILED) || \
                ((stream) && pa_stream_get_state ((stream)) == PA_STREAM_FAILED)) { \
                if (rerror) {                                           \
                    *(rerror) = pa_context_errno ((c)->context);        \
                }                                                       \
            } else {                                                    \
                if (rerror) {                                           \
                    *(rerror) = PA_ERR_BADSTATE;                        \
                }                                                       \
            }                                                           \
            goto label;                                                 \
        }                                                               \
    } while (0);

static int qpa_simple_read (PAVoiceIn *p, void *data, size_t length, int *rerror)
{
    paaudio *g = p->g;

    pa_threaded_mainloop_lock (g->mainloop);

    CHECK_DEAD_GOTO (g, p->stream, rerror, unlock_and_fail);

    while (length > 0) {
        size_t l;

        while (!p->read_data) {
            int r;

            r = pa_stream_peek (p->stream, &p->read_data, &p->read_length);
            CHECK_SUCCESS_GOTO (g, rerror, r == 0, unlock_and_fail);

            if (!p->read_data) {
                pa_threaded_mainloop_wait (g->mainloop);
                CHECK_DEAD_GOTO (g, p->stream, rerror, unlock_and_fail);
            } else {
                p->read_index = 0;
            }
        }

        l = p->read_length < length ? p->read_length : length;
        memcpy (data, (const uint8_t *) p->read_data+p->read_index, l);

        data = (uint8_t *) data + l;
        length -= l;

        p->read_index += l;
        p->read_length -= l;

        if (!p->read_length) {
            int r;

            r = pa_stream_drop (p->stream);
            p->read_data = NULL;
            p->read_length = 0;
            p->read_index = 0;

            CHECK_SUCCESS_GOTO (g, rerror, r == 0, unlock_and_fail);
        }
    }

    pa_threaded_mainloop_unlock (g->mainloop);
    return 0;

unlock_and_fail:
    pa_threaded_mainloop_unlock (g->mainloop);
    return -1;
}

static int qpa_simple_write (PAVoiceOut *p, const void *data, size_t length, int *rerror)
{
    paaudio *g = p->g;

    pa_threaded_mainloop_lock (g->mainloop);

    CHECK_DEAD_GOTO (g, p->stream, rerror, unlock_and_fail);

    while (length > 0) {
        size_t l;
        int r;

        while (!(l = pa_stream_writable_size (p->stream))) {
            pa_threaded_mainloop_wait (g->mainloop);
            CHECK_DEAD_GOTO (g, p->stream, rerror, unlock_and_fail);
        }

        CHECK_SUCCESS_GOTO (g, rerror, l != (size_t) -1, unlock_and_fail);

        if (l > length) {
            l = length;
        }

        r = pa_stream_write (p->stream, data, l, NULL, 0LL, PA_SEEK_RELATIVE);
        CHECK_SUCCESS_GOTO (g, rerror, r >= 0, unlock_and_fail);

        data = (const uint8_t *) data + l;
        length -= l;
    }

    pa_threaded_mainloop_unlock (g->mainloop);
    return 0;

unlock_and_fail:
    pa_threaded_mainloop_unlock (g->mainloop);
    return -1;
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

        decr = to_mix = audio_MIN (pa->live, pa->g->conf.samples >> 2);
        rpos = pa->rpos;

        if (audio_pt_unlock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        while (to_mix) {
            int error;
            int chunk = audio_MIN (to_mix, hw->samples - rpos);
            struct st_sample *src = hw->mix_buf + rpos;

            hw->clip (pa->pcm_buf, src, chunk);

            if (qpa_simple_write (pa, pa->pcm_buf,
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

        incr = to_grab = audio_MIN (pa->dead, pa->g->conf.samples >> 2);
        wpos = pa->wpos;

        if (audio_pt_unlock (&pa->pt, AUDIO_FUNC)) {
            return NULL;
        }

        while (to_grab) {
            int error;
            int chunk = audio_MIN (to_grab, hw->samples - wpos);
            void *buf = advance (pa->pcm_buf, wpos);

            if (qpa_simple_read (pa, buf,
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

static void context_state_cb (pa_context *c, void *userdata)
{
    paaudio *g = userdata;

    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
        pa_threaded_mainloop_signal (g->mainloop, 0);
        break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        break;
    }
}

static void stream_state_cb (pa_stream *s, void * userdata)
{
    paaudio *g = userdata;

    switch (pa_stream_get_state (s)) {

    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        pa_threaded_mainloop_signal (g->mainloop, 0);
        break;

    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
        break;
    }
}

static void stream_request_cb (pa_stream *s, size_t length, void *userdata)
{
    paaudio *g = userdata;

    pa_threaded_mainloop_signal (g->mainloop, 0);
}

static pa_stream *qpa_simple_new (
        paaudio *g,
        const char *name,
        pa_stream_direction_t dir,
        const char *dev,
        const pa_sample_spec *ss,
        const pa_channel_map *map,
        const pa_buffer_attr *attr,
        int *rerror)
{
    int r;
    pa_stream *stream;

    pa_threaded_mainloop_lock (g->mainloop);

    stream = pa_stream_new (g->context, name, ss, map);
    if (!stream) {
        goto fail;
    }

    pa_stream_set_state_callback (stream, stream_state_cb, g);
    pa_stream_set_read_callback (stream, stream_request_cb, g);
    pa_stream_set_write_callback (stream, stream_request_cb, g);

    if (dir == PA_STREAM_PLAYBACK) {
        r = pa_stream_connect_playback (stream, dev, attr,
                                        PA_STREAM_INTERPOLATE_TIMING
#ifdef PA_STREAM_ADJUST_LATENCY
                                        |PA_STREAM_ADJUST_LATENCY
#endif
                                        |PA_STREAM_AUTO_TIMING_UPDATE, NULL, NULL);
    } else {
        r = pa_stream_connect_record (stream, dev, attr,
                                      PA_STREAM_INTERPOLATE_TIMING
#ifdef PA_STREAM_ADJUST_LATENCY
                                      |PA_STREAM_ADJUST_LATENCY
#endif
                                      |PA_STREAM_AUTO_TIMING_UPDATE);
    }

    if (r < 0) {
      goto fail;
    }

    pa_threaded_mainloop_unlock (g->mainloop);

    return stream;

fail:
    pa_threaded_mainloop_unlock (g->mainloop);

    if (stream) {
        pa_stream_unref (stream);
    }

    *rerror = pa_context_errno (g->context);

    return NULL;
}

static int qpa_init_out(HWVoiceOut *hw, struct audsettings *as,
                        void *drv_opaque)
{
    int error;
    pa_sample_spec ss;
    pa_buffer_attr ba;
    struct audsettings obt_as = *as;
    PAVoiceOut *pa = (PAVoiceOut *) hw;
    paaudio *g = pa->g = drv_opaque;

    ss.format = audfmt_to_pa (as->fmt, as->endianness);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    /*
     * qemu audio tick runs at 100 Hz (by default), so processing
     * data chunks worth 10 ms of sound should be a good fit.
     */
    ba.tlength = pa_usec_to_bytes (10 * 1000, &ss);
    ba.minreq = pa_usec_to_bytes (5 * 1000, &ss);
    ba.maxlength = -1;
    ba.prebuf = -1;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.endianness);

    pa->stream = qpa_simple_new (
        g,
        "qemu",
        PA_STREAM_PLAYBACK,
        g->conf.sink,
        &ss,
        NULL,                   /* channel map */
        &ba,                    /* buffering attributes */
        &error
        );
    if (!pa->stream) {
        qpa_logerr (error, "pa_simple_new for playback failed\n");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = g->conf.samples;
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
    g_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
 fail2:
    if (pa->stream) {
        pa_stream_unref (pa->stream);
        pa->stream = NULL;
    }
 fail1:
    return -1;
}

static int qpa_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    int error;
    pa_sample_spec ss;
    struct audsettings obt_as = *as;
    PAVoiceIn *pa = (PAVoiceIn *) hw;
    paaudio *g = pa->g = drv_opaque;

    ss.format = audfmt_to_pa (as->fmt, as->endianness);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.endianness);

    pa->stream = qpa_simple_new (
        g,
        "qemu",
        PA_STREAM_RECORD,
        g->conf.source,
        &ss,
        NULL,                   /* channel map */
        NULL,                   /* buffering attributes */
        &error
        );
    if (!pa->stream) {
        qpa_logerr (error, "pa_simple_new for capture failed\n");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    hw->samples = g->conf.samples;
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
    g_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
 fail2:
    if (pa->stream) {
        pa_stream_unref (pa->stream);
        pa->stream = NULL;
    }
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

    if (pa->stream) {
        pa_stream_unref (pa->stream);
        pa->stream = NULL;
    }

    audio_pt_fini (&pa->pt, AUDIO_FUNC);
    g_free (pa->pcm_buf);
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

    if (pa->stream) {
        pa_stream_unref (pa->stream);
        pa->stream = NULL;
    }

    audio_pt_fini (&pa->pt, AUDIO_FUNC);
    g_free (pa->pcm_buf);
    pa->pcm_buf = NULL;
}

static int qpa_ctl_out (HWVoiceOut *hw, int cmd, ...)
{
    PAVoiceOut *pa = (PAVoiceOut *) hw;
    pa_operation *op;
    pa_cvolume v;
    paaudio *g = pa->g;

#ifdef PA_CHECK_VERSION    /* macro is present in 0.9.16+ */
    pa_cvolume_init (&v);  /* function is present in 0.9.13+ */
#endif

    switch (cmd) {
    case VOICE_VOLUME:
        {
            SWVoiceOut *sw;
            va_list ap;

            va_start (ap, cmd);
            sw = va_arg (ap, SWVoiceOut *);
            va_end (ap);

            v.channels = 2;
            v.values[0] = ((PA_VOLUME_NORM - PA_VOLUME_MUTED) * sw->vol.l) / UINT32_MAX;
            v.values[1] = ((PA_VOLUME_NORM - PA_VOLUME_MUTED) * sw->vol.r) / UINT32_MAX;

            pa_threaded_mainloop_lock (g->mainloop);

            op = pa_context_set_sink_input_volume (g->context,
                pa_stream_get_index (pa->stream),
                &v, NULL, NULL);
            if (!op)
                qpa_logerr (pa_context_errno (g->context),
                            "set_sink_input_volume() failed\n");
            else
                pa_operation_unref (op);

            op = pa_context_set_sink_input_mute (g->context,
                pa_stream_get_index (pa->stream),
               sw->vol.mute, NULL, NULL);
            if (!op) {
                qpa_logerr (pa_context_errno (g->context),
                            "set_sink_input_mute() failed\n");
            } else {
                pa_operation_unref (op);
            }

            pa_threaded_mainloop_unlock (g->mainloop);
        }
    }
    return 0;
}

static int qpa_ctl_in (HWVoiceIn *hw, int cmd, ...)
{
    PAVoiceIn *pa = (PAVoiceIn *) hw;
    pa_operation *op;
    pa_cvolume v;
    paaudio *g = pa->g;

#ifdef PA_CHECK_VERSION
    pa_cvolume_init (&v);
#endif

    switch (cmd) {
    case VOICE_VOLUME:
        {
            SWVoiceIn *sw;
            va_list ap;

            va_start (ap, cmd);
            sw = va_arg (ap, SWVoiceIn *);
            va_end (ap);

            v.channels = 2;
            v.values[0] = ((PA_VOLUME_NORM - PA_VOLUME_MUTED) * sw->vol.l) / UINT32_MAX;
            v.values[1] = ((PA_VOLUME_NORM - PA_VOLUME_MUTED) * sw->vol.r) / UINT32_MAX;

            pa_threaded_mainloop_lock (g->mainloop);

            /* FIXME: use the upcoming "set_source_output_{volume,mute}" */
            op = pa_context_set_source_volume_by_index (g->context,
                pa_stream_get_device_index (pa->stream),
                &v, NULL, NULL);
            if (!op) {
                qpa_logerr (pa_context_errno (g->context),
                            "set_source_volume() failed\n");
            } else {
                pa_operation_unref(op);
            }

            op = pa_context_set_source_mute_by_index (g->context,
                pa_stream_get_index (pa->stream),
                sw->vol.mute, NULL, NULL);
            if (!op) {
                qpa_logerr (pa_context_errno (g->context),
                            "set_source_mute() failed\n");
            } else {
                pa_operation_unref (op);
            }

            pa_threaded_mainloop_unlock (g->mainloop);
        }
    }
    return 0;
}

/* common */
static PAConf glob_conf = {
    .samples = 4096,
};

static void *qpa_audio_init (void)
{
    paaudio *g = g_malloc(sizeof(paaudio));
    g->conf = glob_conf;
    g->mainloop = NULL;
    g->context = NULL;

    g->mainloop = pa_threaded_mainloop_new ();
    if (!g->mainloop) {
        goto fail;
    }

    g->context = pa_context_new (pa_threaded_mainloop_get_api (g->mainloop),
                                 g->conf.server);
    if (!g->context) {
        goto fail;
    }

    pa_context_set_state_callback (g->context, context_state_cb, g);

    if (pa_context_connect (g->context, g->conf.server, 0, NULL) < 0) {
        qpa_logerr (pa_context_errno (g->context),
                    "pa_context_connect() failed\n");
        goto fail;
    }

    pa_threaded_mainloop_lock (g->mainloop);

    if (pa_threaded_mainloop_start (g->mainloop) < 0) {
        goto unlock_and_fail;
    }

    for (;;) {
        pa_context_state_t state;

        state = pa_context_get_state (g->context);

        if (state == PA_CONTEXT_READY) {
            break;
        }

        if (!PA_CONTEXT_IS_GOOD (state)) {
            qpa_logerr (pa_context_errno (g->context),
                        "Wrong context state\n");
            goto unlock_and_fail;
        }

        /* Wait until the context is ready */
        pa_threaded_mainloop_wait (g->mainloop);
    }

    pa_threaded_mainloop_unlock (g->mainloop);

    return g;

unlock_and_fail:
    pa_threaded_mainloop_unlock (g->mainloop);
fail:
    AUD_log (AUDIO_CAP, "Failed to initialize PA context");
    qpa_audio_fini(g);
    return NULL;
}

static void qpa_audio_fini (void *opaque)
{
    paaudio *g = opaque;

    if (g->mainloop) {
        pa_threaded_mainloop_stop (g->mainloop);
    }

    if (g->context) {
        pa_context_disconnect (g->context);
        pa_context_unref (g->context);
    }

    if (g->mainloop) {
        pa_threaded_mainloop_free (g->mainloop);
    }

    g_free(g);
}

struct audio_option qpa_options[] = {
    {
        .name  = "SAMPLES",
        .tag   = AUD_OPT_INT,
        .valp  = &glob_conf.samples,
        .descr = "buffer size in samples"
    },
    {
        .name  = "SERVER",
        .tag   = AUD_OPT_STR,
        .valp  = &glob_conf.server,
        .descr = "server address"
    },
    {
        .name  = "SINK",
        .tag   = AUD_OPT_STR,
        .valp  = &glob_conf.sink,
        .descr = "sink device name"
    },
    {
        .name  = "SOURCE",
        .tag   = AUD_OPT_STR,
        .valp  = &glob_conf.source,
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
    .voice_size_in  = sizeof (PAVoiceIn),
    .ctl_caps       = VOICE_VOLUME_CAP
};
