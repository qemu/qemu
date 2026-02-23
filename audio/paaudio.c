/* public domain */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/audio.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "audio_int.h"

#include <pulse/pulseaudio.h>

#define TYPE_AUDIO_PA "audio-pa"
OBJECT_DECLARE_SIMPLE_TYPE(AudioPa, AUDIO_PA)

static AudioBackendClass *audio_pa_parent_class;


typedef struct PAConnection {
    char *server;
    int refcount;
    QTAILQ_ENTRY(PAConnection) list;

    pa_threaded_mainloop *mainloop;
    pa_context *context;
} PAConnection;

struct AudioPa {
    AudioMixengBackend parent_obj;

    PAConnection *conn;
};

static QTAILQ_HEAD(PAConnectionHead, PAConnection) pa_conns =
    QTAILQ_HEAD_INITIALIZER(pa_conns);

typedef struct {
    HWVoiceOut hw;
    pa_stream *stream;
    AudioPa *g;
} PAVoiceOut;

typedef struct {
    HWVoiceIn hw;
    pa_stream *stream;
    const void *read_data;
    size_t read_length;
    AudioPa *g;
} PAVoiceIn;

static void qpa_conn_fini(PAConnection *c);

static void G_GNUC_PRINTF(2, 3) qpa_logerr(int err, const char *fmt, ...)
{
    va_list ap;

    error_printf("pulseaudio: ");
    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);

    error_printf(" Reason: %s\n", pa_strerror(err));
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

#define CHECK_SUCCESS_GOTO(c, expression, label, msg)           \
    do {                                                        \
        if (!(expression)) {                                    \
            qpa_logerr(pa_context_errno((c)->context), msg);    \
            goto label;                                         \
        }                                                       \
    } while (0)

#define CHECK_DEAD_GOTO(c, stream, label, msg)                          \
    do {                                                                \
        if (!(c)->context || !PA_CONTEXT_IS_GOOD (pa_context_get_state((c)->context)) || \
            !(stream) || !PA_STREAM_IS_GOOD (pa_stream_get_state ((stream)))) { \
            if (((c)->context && pa_context_get_state ((c)->context) == PA_CONTEXT_FAILED) || \
                ((stream) && pa_stream_get_state ((stream)) == PA_STREAM_FAILED)) { \
                qpa_logerr(pa_context_errno((c)->context), msg);        \
            } else {                                                    \
                qpa_logerr(PA_ERR_BADSTATE, msg);                       \
            }                                                           \
            goto label;                                                 \
        }                                                               \
    } while (0)

static void *qpa_get_buffer_in(HWVoiceIn *hw, size_t *size)
{
    PAVoiceIn *p = (PAVoiceIn *) hw;
    PAConnection *c = p->g->conn;
    int r;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock_and_fail,
                    "pa_threaded_mainloop_lock failed");

    if (!p->read_length) {
        r = pa_stream_peek(p->stream, &p->read_data, &p->read_length);
        CHECK_SUCCESS_GOTO(c, r == 0, unlock_and_fail,
                           "pa_stream_peek failed");
    }

    *size = MIN(p->read_length, *size);

    pa_threaded_mainloop_unlock(c->mainloop);
    return (void *) p->read_data;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
    *size = 0;
    return NULL;
}

static void qpa_put_buffer_in(HWVoiceIn *hw, void *buf, size_t size)
{
    PAVoiceIn *p = (PAVoiceIn *) hw;
    PAConnection *c = p->g->conn;
    int r;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock,
                    "pa_threaded_mainloop_lock failed");

    assert(buf == p->read_data && size <= p->read_length);

    p->read_data += size;
    p->read_length -= size;

    if (size && !p->read_length) {
        r = pa_stream_drop(p->stream);
        CHECK_SUCCESS_GOTO(c, r == 0, unlock, "pa_stream_drop failed");
    }

unlock:
    pa_threaded_mainloop_unlock(c->mainloop);
}

static size_t qpa_read(HWVoiceIn *hw, void *data, size_t length)
{
    PAVoiceIn *p = (PAVoiceIn *) hw;
    PAConnection *c = p->g->conn;
    size_t total = 0;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock_and_fail,
                    "pa_threaded_mainloop_lock failed");
    if (pa_stream_get_state(p->stream) != PA_STREAM_READY) {
        /* wait for stream to become ready */
        goto unlock;
    }

    while (total < length) {
        size_t l;
        int r;

        if (!p->read_length) {
            r = pa_stream_peek(p->stream, &p->read_data, &p->read_length);
            CHECK_SUCCESS_GOTO(c, r == 0, unlock_and_fail,
                               "pa_stream_peek failed");
            if (!p->read_length) {
                /* buffer is empty */
                break;
            }
        }

        l = MIN(p->read_length, length - total);
        memcpy((char *)data + total, p->read_data, l);

        p->read_data += l;
        p->read_length -= l;
        total += l;

        if (!p->read_length) {
            r = pa_stream_drop(p->stream);
            CHECK_SUCCESS_GOTO(c, r == 0, unlock_and_fail,
                               "pa_stream_drop failed");
        }
    }

unlock:
    pa_threaded_mainloop_unlock(c->mainloop);
    return total;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
    return 0;
}

static size_t qpa_buffer_get_free(HWVoiceOut *hw)
{
    PAVoiceOut *p = (PAVoiceOut *)hw;
    PAConnection *c = p->g->conn;
    size_t l;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock_and_fail,
                    "pa_threaded_mainloop_lock failed");
    if (pa_stream_get_state(p->stream) != PA_STREAM_READY) {
        /* wait for stream to become ready */
        l = 0;
        goto unlock;
    }

    l = pa_stream_writable_size(p->stream);
    CHECK_SUCCESS_GOTO(c, l != (size_t) -1, unlock_and_fail,
                       "pa_stream_writable_size failed");

unlock:
    pa_threaded_mainloop_unlock(c->mainloop);
    return l;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
    return 0;
}

static void *qpa_get_buffer_out(HWVoiceOut *hw, size_t *size)
{
    PAVoiceOut *p = (PAVoiceOut *)hw;
    PAConnection *c = p->g->conn;
    void *ret;
    int r;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock_and_fail,
                    "pa_threaded_mainloop_lock failed");

    *size = -1;
    r = pa_stream_begin_write(p->stream, &ret, size);
    CHECK_SUCCESS_GOTO(c, r >= 0, unlock_and_fail,
                       "pa_stream_begin_write failed");

    pa_threaded_mainloop_unlock(c->mainloop);
    return ret;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
    *size = 0;
    return NULL;
}

static size_t qpa_put_buffer_out(HWVoiceOut *hw, void *data, size_t length)
{
    PAVoiceOut *p = (PAVoiceOut *)hw;
    PAConnection *c = p->g->conn;
    int r;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock_and_fail,
                    "pa_threaded_mainloop_lock failed");

    r = pa_stream_write(p->stream, data, length, NULL, 0LL, PA_SEEK_RELATIVE);
    CHECK_SUCCESS_GOTO(c, r >= 0, unlock_and_fail, "pa_stream_write failed");

    pa_threaded_mainloop_unlock(c->mainloop);
    return length;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
    return 0;
}

static size_t qpa_write(HWVoiceOut *hw, void *data, size_t length)
{
    PAVoiceOut *p = (PAVoiceOut *) hw;
    PAConnection *c = p->g->conn;
    size_t l;
    int r;

    pa_threaded_mainloop_lock(c->mainloop);

    CHECK_DEAD_GOTO(c, p->stream, unlock_and_fail,
                    "pa_threaded_mainloop_lock failed");
    if (pa_stream_get_state(p->stream) != PA_STREAM_READY) {
        /* wait for stream to become ready */
        l = 0;
        goto unlock;
    }

    l = pa_stream_writable_size(p->stream);

    CHECK_SUCCESS_GOTO(c, l != (size_t) -1, unlock_and_fail,
                       "pa_stream_writable_size failed");

    if (l > length) {
        l = length;
    }

    r = pa_stream_write(p->stream, data, l, NULL, 0LL, PA_SEEK_RELATIVE);
    CHECK_SUCCESS_GOTO(c, r >= 0, unlock_and_fail, "pa_stream_write failed");

unlock:
    pa_threaded_mainloop_unlock(c->mainloop);
    return l;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
    return 0;
}

static pa_sample_format_t audfmt_to_pa(AudioFormat afmt, bool big_endian)
{
    int format;

    switch (afmt) {
    case AUDIO_FORMAT_S8:
    case AUDIO_FORMAT_U8:
        format = PA_SAMPLE_U8;
        break;
    case AUDIO_FORMAT_S16:
    case AUDIO_FORMAT_U16:
        format = big_endian ? PA_SAMPLE_S16BE : PA_SAMPLE_S16LE;
        break;
    case AUDIO_FORMAT_S32:
    case AUDIO_FORMAT_U32:
        format = big_endian ? PA_SAMPLE_S32BE : PA_SAMPLE_S32LE;
        break;
    case AUDIO_FORMAT_F32:
        format = big_endian ? PA_SAMPLE_FLOAT32BE : PA_SAMPLE_FLOAT32LE;
        break;
    default:
        error_report("pulseaudio: Internal logic error: Bad audio format %d", afmt);
        format = PA_SAMPLE_U8;
        break;
    }
    return format;
}

static AudioFormat pa_to_audfmt (pa_sample_format_t fmt, bool *big_endian)
{
    switch (fmt) {
    case PA_SAMPLE_U8:
        return AUDIO_FORMAT_U8;
    case PA_SAMPLE_S16BE:
        *big_endian = true;
        return AUDIO_FORMAT_S16;
    case PA_SAMPLE_S16LE:
        *big_endian = false;
        return AUDIO_FORMAT_S16;
    case PA_SAMPLE_S32BE:
        *big_endian = true;
        return AUDIO_FORMAT_S32;
    case PA_SAMPLE_S32LE:
        *big_endian = false;
        return AUDIO_FORMAT_S32;
    case PA_SAMPLE_FLOAT32BE:
        *big_endian = true;
        return AUDIO_FORMAT_F32;
    case PA_SAMPLE_FLOAT32LE:
        *big_endian = false;
        return AUDIO_FORMAT_F32;
    default:
        error_report("pulseaudio: Internal logic error: Bad pa_sample_format %d", fmt);
        return AUDIO_FORMAT_U8;
    }
}

static void context_state_cb(pa_context *c, void *userdata)
{
    PAConnection *conn = userdata;

    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
        pa_threaded_mainloop_signal(conn->mainloop, 0);
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
    PAConnection *c = userdata;

    switch (pa_stream_get_state (s)) {

    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        pa_threaded_mainloop_signal(c->mainloop, 0);
        break;

    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
        break;
    }
}

static pa_stream *qpa_simple_new (
        PAConnection *c,
        const char *name,
        pa_stream_direction_t dir,
        const char *dev,
        const pa_sample_spec *ss,
        const pa_buffer_attr *attr,
        int *rerror)
{
    int r;
    pa_stream *stream = NULL;
    pa_stream_flags_t flags;
    pa_channel_map map;

    pa_threaded_mainloop_lock(c->mainloop);

    pa_channel_map_init(&map);
    map.channels = ss->channels;

    /*
     * TODO: This currently expects the only frontend supporting more than 2
     * channels is the usb-audio.  We will need some means to set channel
     * order when a new frontend gains multi-channel support.
     */
    switch (ss->channels) {
    case 1:
        map.map[0] = PA_CHANNEL_POSITION_MONO;
        break;

    case 2:
        map.map[0] = PA_CHANNEL_POSITION_LEFT;
        map.map[1] = PA_CHANNEL_POSITION_RIGHT;
        break;

    case 6:
        map.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
        map.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
        map.map[2] = PA_CHANNEL_POSITION_CENTER;
        map.map[3] = PA_CHANNEL_POSITION_LFE;
        map.map[4] = PA_CHANNEL_POSITION_REAR_LEFT;
        map.map[5] = PA_CHANNEL_POSITION_REAR_RIGHT;
        break;

    case 8:
        map.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
        map.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
        map.map[2] = PA_CHANNEL_POSITION_CENTER;
        map.map[3] = PA_CHANNEL_POSITION_LFE;
        map.map[4] = PA_CHANNEL_POSITION_REAR_LEFT;
        map.map[5] = PA_CHANNEL_POSITION_REAR_RIGHT;
        map.map[6] = PA_CHANNEL_POSITION_SIDE_LEFT;
        map.map[7] = PA_CHANNEL_POSITION_SIDE_RIGHT;
        break;

    default:
        error_report("pulseaudio: unsupported channel count %d", ss->channels);
        goto fail;
    }

    stream = pa_stream_new(c->context, name, ss, &map);
    if (!stream) {
        goto fail;
    }

    pa_stream_set_state_callback(stream, stream_state_cb, c);

    flags = PA_STREAM_EARLY_REQUESTS;

    if (dev) {
        /* don't move the stream if the user specified a sink/source */
        flags |= PA_STREAM_DONT_MOVE;
    }

    if (dir == PA_STREAM_PLAYBACK) {
        r = pa_stream_connect_playback(stream, dev, attr, flags, NULL, NULL);
    } else {
        r = pa_stream_connect_record(stream, dev, attr, flags);
    }

    if (r < 0) {
        goto fail;
    }

    pa_threaded_mainloop_unlock(c->mainloop);

    return stream;

fail:
    pa_threaded_mainloop_unlock(c->mainloop);

    if (stream) {
        pa_stream_unref (stream);
    }

    *rerror = pa_context_errno(c->context);

    return NULL;
}

static int qpa_init_out(HWVoiceOut *hw, struct audsettings *as)
{
    AudioMixengBackend *amb = hw->s;
    AudioPa *apa = AUDIO_PA(amb);
    int error;
    pa_sample_spec ss;
    pa_buffer_attr ba;
    struct audsettings obt_as = *as;
    PAVoiceOut *pa = (PAVoiceOut *) hw;
    AudiodevPaOptions *popts = &amb->dev->u.pa;
    AudiodevPaPerDirectionOptions *ppdo = popts->out;
    PAConnection *c = apa->conn;

    pa->g = apa;
    ss.format = audfmt_to_pa (as->fmt, as->big_endian);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    ba.tlength = pa_usec_to_bytes(ppdo->latency, &ss);
    ba.minreq = pa_usec_to_bytes(MIN(ppdo->latency >> 2,
                                     (amb->dev->timer_period >> 2) * 3), &ss);
    ba.maxlength = -1;
    ba.prebuf = -1;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.big_endian);

    pa->stream = qpa_simple_new (
        c,
        ppdo->stream_name ?: amb->dev->id,
        PA_STREAM_PLAYBACK,
        ppdo->name,
        &ss,
        &ba,                    /* buffering attributes */
        &error
        );
    if (!pa->stream) {
        qpa_logerr(error, "pa_simple_new for playback failed");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    /* hw->samples counts in frames */
    hw->samples = audio_buffer_frames(
        qapi_AudiodevPaPerDirectionOptions_base(ppdo), &obt_as, 46440);

    return 0;

 fail1:
    return -1;
}

static int qpa_init_in(HWVoiceIn *hw, struct audsettings *as)
{
    AudioMixengBackend *amb = hw->s;
    AudioPa *apa = AUDIO_PA(amb);
    int error;
    pa_sample_spec ss;
    pa_buffer_attr ba;
    struct audsettings obt_as = *as;
    PAVoiceIn *pa = (PAVoiceIn *) hw;
    AudiodevPaOptions *popts = &amb->dev->u.pa;
    AudiodevPaPerDirectionOptions *ppdo = popts->in;
    PAConnection *c = apa->conn;

    pa->g = apa;
    ss.format = audfmt_to_pa (as->fmt, as->big_endian);
    ss.channels = as->nchannels;
    ss.rate = as->freq;

    ba.fragsize = pa_usec_to_bytes((amb->dev->timer_period >> 1) * 3, &ss);
    ba.maxlength = pa_usec_to_bytes(
        MAX(ppdo->latency, amb->dev->timer_period * 3), &ss);
    ba.minreq = -1;
    ba.prebuf = -1;

    obt_as.fmt = pa_to_audfmt (ss.format, &obt_as.big_endian);

    pa->stream = qpa_simple_new (
        c,
        ppdo->stream_name ?: amb->dev->id,
        PA_STREAM_RECORD,
        ppdo->name,
        &ss,
        &ba,                    /* buffering attributes */
        &error
        );
    if (!pa->stream) {
        qpa_logerr(error, "pa_simple_new for capture failed");
        goto fail1;
    }

    audio_pcm_init_info (&hw->info, &obt_as);
    /* hw->samples counts in frames */
    hw->samples = audio_buffer_frames(
        qapi_AudiodevPaPerDirectionOptions_base(ppdo), &obt_as, 46440);

    return 0;

 fail1:
    return -1;
}

static void qpa_simple_disconnect(PAConnection *c, pa_stream *stream)
{
    int err;

    /*
     * wait until actually connects. workaround pa bug #247
     * https://gitlab.freedesktop.org/pulseaudio/pulseaudio/issues/247
     */
    while (pa_stream_get_state(stream) == PA_STREAM_CREATING) {
        pa_threaded_mainloop_wait(c->mainloop);
    }

    err = pa_stream_disconnect(stream);
    if (err != 0) {
        error_report("pulseaudio: Failed to disconnect! err=%d", err);
    }
    pa_stream_unref(stream);
}

static void qpa_fini_out (HWVoiceOut *hw)
{
    PAVoiceOut *pa = (PAVoiceOut *) hw;

    if (pa->stream) {
        PAConnection *c = pa->g->conn;

        pa_threaded_mainloop_lock(c->mainloop);
        qpa_simple_disconnect(c, pa->stream);
        pa->stream = NULL;
        pa_threaded_mainloop_unlock(c->mainloop);
    }
}

static void qpa_fini_in (HWVoiceIn *hw)
{
    PAVoiceIn *pa = (PAVoiceIn *) hw;

    if (pa->stream) {
        PAConnection *c = pa->g->conn;

        pa_threaded_mainloop_lock(c->mainloop);
        if (pa->read_length) {
            int r = pa_stream_drop(pa->stream);
            if (r) {
                qpa_logerr(pa_context_errno(c->context),
                           "pa_stream_drop failed");
            }
            pa->read_length = 0;
        }
        qpa_simple_disconnect(c, pa->stream);
        pa->stream = NULL;
        pa_threaded_mainloop_unlock(c->mainloop);
    }
}

static void qpa_volume_out(HWVoiceOut *hw, Volume *vol)
{
    PAVoiceOut *pa = (PAVoiceOut *) hw;
    pa_operation *op;
    pa_cvolume v;
    PAConnection *c = pa->g->conn;
    int i;

#ifdef PA_CHECK_VERSION    /* macro is present in 0.9.16+ */
    pa_cvolume_init (&v);  /* function is present in 0.9.13+ */
#endif

    v.channels = vol->channels;
    for (i = 0; i < vol->channels; ++i) {
        v.values[i] = ((PA_VOLUME_NORM - PA_VOLUME_MUTED) * vol->vol[i]) / 255;
    }

    pa_threaded_mainloop_lock(c->mainloop);

    op = pa_context_set_sink_input_volume(c->context,
                                          pa_stream_get_index(pa->stream),
                                          &v, NULL, NULL);
    if (!op) {
        qpa_logerr(pa_context_errno(c->context),
                   "set_sink_input_volume() failed");
    } else {
        pa_operation_unref(op);
    }

    op = pa_context_set_sink_input_mute(c->context,
                                        pa_stream_get_index(pa->stream),
                                        vol->mute, NULL, NULL);
    if (!op) {
        qpa_logerr(pa_context_errno(c->context),
                   "set_sink_input_mute() failed");
    } else {
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(c->mainloop);
}

static void qpa_volume_in(HWVoiceIn *hw, Volume *vol)
{
    PAVoiceIn *pa = (PAVoiceIn *) hw;
    pa_operation *op;
    pa_cvolume v;
    PAConnection *c = pa->g->conn;
    int i;

#ifdef PA_CHECK_VERSION
    pa_cvolume_init (&v);
#endif

    v.channels = vol->channels;
    for (i = 0; i < vol->channels; ++i) {
        v.values[i] = ((PA_VOLUME_NORM - PA_VOLUME_MUTED) * vol->vol[i]) / 255;
    }

    pa_threaded_mainloop_lock(c->mainloop);

    op = pa_context_set_source_output_volume(c->context,
        pa_stream_get_index(pa->stream),
        &v, NULL, NULL);
    if (!op) {
        qpa_logerr(pa_context_errno(c->context),
                   "set_source_output_volume() failed");
    } else {
        pa_operation_unref(op);
    }

    op = pa_context_set_source_output_mute(c->context,
        pa_stream_get_index(pa->stream),
        vol->mute, NULL, NULL);
    if (!op) {
        qpa_logerr(pa_context_errno(c->context),
                   "set_source_output_mute() failed");
    } else {
        pa_operation_unref(op);
    }

    pa_threaded_mainloop_unlock(c->mainloop);
}

static void qpa_validate_per_direction_opts(Audiodev *dev,
                                            AudiodevPaPerDirectionOptions *pdo)
{
    if (!pdo->has_latency) {
        pdo->has_latency = true;
        pdo->latency = 46440;
    }
}

/* common */
static void *qpa_conn_init(const char *server)
{
    PAConnection *c = g_new0(PAConnection, 1);
    QTAILQ_INSERT_TAIL(&pa_conns, c, list);

    c->mainloop = pa_threaded_mainloop_new();
    if (!c->mainloop) {
        goto fail;
    }

    c->context = pa_context_new(pa_threaded_mainloop_get_api(c->mainloop),
                                audio_application_name());
    if (!c->context) {
        goto fail;
    }

    pa_context_set_state_callback(c->context, context_state_cb, c);

    if (pa_context_connect(c->context, server, 0, NULL) < 0) {
        qpa_logerr(pa_context_errno(c->context),
                   "pa_context_connect() failed");
        goto fail;
    }

    pa_threaded_mainloop_lock(c->mainloop);

    if (pa_threaded_mainloop_start(c->mainloop) < 0) {
        goto unlock_and_fail;
    }

    for (;;) {
        pa_context_state_t state;

        state = pa_context_get_state(c->context);

        if (state == PA_CONTEXT_READY) {
            break;
        }

        if (!PA_CONTEXT_IS_GOOD(state)) {
            qpa_logerr(pa_context_errno(c->context),
                       "Wrong context state");
            goto unlock_and_fail;
        }

        /* Wait until the context is ready */
        pa_threaded_mainloop_wait(c->mainloop);
    }

    pa_threaded_mainloop_unlock(c->mainloop);
    return c;

unlock_and_fail:
    pa_threaded_mainloop_unlock(c->mainloop);
fail:
    qpa_conn_fini(c);
    return NULL;
}

static bool
audio_pa_realize(AudioBackend *abe, Audiodev *dev, Error **errp)
{
    AudioPa *apa = AUDIO_PA(abe);
    AudiodevPaOptions *popts = &dev->u.pa;
    const char *server;
    PAConnection *c;

    assert(dev->driver == AUDIODEV_DRIVER_PA);

    if (!audio_pa_parent_class->realize(abe, dev, errp)) {
        return false;
    }

    if (!popts->server) {
        char pidfile[64];
        char *runtime;
        struct stat st;

        runtime = getenv("XDG_RUNTIME_DIR");
        if (!runtime) {
            error_setg(errp, "XDG_RUNTIME_DIR not set");
            return false;
        }
        snprintf(pidfile, sizeof(pidfile), "%s/pulse/pid", runtime);
        if (stat(pidfile, &st) != 0) {
            error_setg_errno(errp, errno, "could not stat pidfile %s", pidfile);
            return false;
        }
    }

    qpa_validate_per_direction_opts(dev, popts->in);
    qpa_validate_per_direction_opts(dev, popts->out);

    server = popts->server;
    QTAILQ_FOREACH(c, &pa_conns, list) {
        if (server == NULL || c->server == NULL ?
            server == c->server :
            strcmp(server, c->server) == 0) {
            apa->conn = c;
            break;
        }
    }
    if (!apa->conn) {
        apa->conn = qpa_conn_init(server);
    }
    if (!apa->conn) {
        error_setg(errp, "could not connect to PulseAudio server");
        return false;
    }

    ++apa->conn->refcount;

    return true;
}

static void qpa_conn_fini(PAConnection *c)
{
    if (c->mainloop) {
        pa_threaded_mainloop_stop(c->mainloop);
    }

    if (c->context) {
        pa_context_disconnect(c->context);
        pa_context_unref(c->context);
    }

    if (c->mainloop) {
        pa_threaded_mainloop_free(c->mainloop);
    }

    QTAILQ_REMOVE(&pa_conns, c, list);
    g_free(c);
}

static void audio_pa_finalize(Object *obj)
{
    AudioPa *apa = AUDIO_PA(obj);
    PAConnection *c = apa->conn;

    if (c && --c->refcount == 0) {
        qpa_conn_fini(c);
    }
}

static void audio_pa_class_init(ObjectClass *klass, const void *data)
{
    AudioBackendClass *b = AUDIO_BACKEND_CLASS(klass);
    AudioMixengBackendClass *k = AUDIO_MIXENG_BACKEND_CLASS(klass);

    audio_pa_parent_class = AUDIO_BACKEND_CLASS(object_class_get_parent(klass));

    b->realize = audio_pa_realize;
    k->max_voices_out = INT_MAX;
    k->max_voices_in = INT_MAX;
    k->voice_size_out = sizeof(PAVoiceOut);
    k->voice_size_in = sizeof(PAVoiceIn);

    k->init_out = qpa_init_out;
    k->fini_out = qpa_fini_out;
    k->write = qpa_write;
    k->buffer_get_free = qpa_buffer_get_free;
    k->get_buffer_out = qpa_get_buffer_out;
    k->put_buffer_out = qpa_put_buffer_out;
    k->volume_out = qpa_volume_out;

    k->init_in = qpa_init_in;
    k->fini_in = qpa_fini_in;
    k->read = qpa_read;
    k->get_buffer_in = qpa_get_buffer_in;
    k->put_buffer_in = qpa_put_buffer_in;
    k->volume_in = qpa_volume_in;
}

static const TypeInfo audio_types[] = {
    {
        .name = TYPE_AUDIO_PA,
        .parent = TYPE_AUDIO_MIXENG_BACKEND,
        .instance_size = sizeof(AudioPa),
        .class_init = audio_pa_class_init,
        .instance_finalize = audio_pa_finalize,
    },
};

DEFINE_TYPES(audio_types)
module_obj(TYPE_AUDIO_PA);
