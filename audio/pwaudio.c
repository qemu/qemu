/*
 * QEMU PipeWire audio driver
 *
 * Copyright (c) 2023 Red Hat Inc.
 *
 * Author: Dorinda Bassey       <dbassey@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "audio.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include <spa/param/audio/format-utils.h>
#include <spa/utils/ringbuffer.h>
#include <spa/utils/result.h>
#include <spa/param/props.h>

#include <pipewire/pipewire.h>
#include "trace.h"

#define AUDIO_CAP "pipewire"
#define RINGBUFFER_SIZE    (1u << 22)
#define RINGBUFFER_MASK    (RINGBUFFER_SIZE - 1)

#include "audio_int.h"

typedef struct pwvolume {
    uint32_t channels;
    float values[SPA_AUDIO_MAX_CHANNELS];
} pwvolume;

typedef struct pwaudio {
    Audiodev *dev;
    struct pw_thread_loop *thread_loop;
    struct pw_context *context;

    struct pw_core *core;
    struct spa_hook core_listener;
    int last_seq, pending_seq, error;
} pwaudio;

typedef struct PWVoice {
    pwaudio *g;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_audio_info_raw info;
    uint32_t highwater_mark;
    uint32_t frame_size, req;
    struct spa_ringbuffer ring;
    uint8_t buffer[RINGBUFFER_SIZE];

    pwvolume volume;
    bool muted;
} PWVoice;

typedef struct PWVoiceOut {
    HWVoiceOut hw;
    PWVoice v;
} PWVoiceOut;

typedef struct PWVoiceIn {
    HWVoiceIn hw;
    PWVoice v;
} PWVoiceIn;

#define PW_VOICE_IN(v) ((PWVoiceIn *)v)
#define PW_VOICE_OUT(v) ((PWVoiceOut *)v)

static void
stream_destroy(void *data)
{
    PWVoice *v = (PWVoice *) data;
    spa_hook_remove(&v->stream_listener);
    v->stream = NULL;
}

/* output data processing function to read stuffs from the buffer */
static void
playback_on_process(void *data)
{
    PWVoice *v = data;
    void *p;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    uint32_t req, index, n_bytes;
    int32_t avail;

    assert(v->stream);

    /* obtain a buffer to read from */
    b = pw_stream_dequeue_buffer(v->stream);
    if (b == NULL) {
        error_report("out of buffers: %s", strerror(errno));
        return;
    }

    buf = b->buffer;
    p = buf->datas[0].data;
    if (p == NULL) {
        return;
    }
    /* calculate the total no of bytes to read data from buffer */
    req = b->requested * v->frame_size;
    if (req == 0) {
        req = v->req;
    }
    n_bytes = SPA_MIN(req, buf->datas[0].maxsize);

    /* get no of available bytes to read data from buffer */
    avail = spa_ringbuffer_get_read_index(&v->ring, &index);

    if (avail <= 0) {
        PWVoiceOut *vo = container_of(data, PWVoiceOut, v);
        audio_pcm_info_clear_buf(&vo->hw.info, p, n_bytes / v->frame_size);
    } else {
        if ((uint32_t) avail < n_bytes) {
            /*
             * PipeWire immediately calls this callback again if we provide
             * less than n_bytes. Then audio_pcm_info_clear_buf() fills the
             * rest of the buffer with silence.
             */
            n_bytes = avail;
        }

        spa_ringbuffer_read_data(&v->ring,
                                    v->buffer, RINGBUFFER_SIZE,
                                    index & RINGBUFFER_MASK, p, n_bytes);

        index += n_bytes;
        spa_ringbuffer_read_update(&v->ring, index);

    }
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = v->frame_size;
    buf->datas[0].chunk->size = n_bytes;

    /* queue the buffer for playback */
    pw_stream_queue_buffer(v->stream, b);
}

/* output data processing function to generate stuffs in the buffer */
static void
capture_on_process(void *data)
{
    PWVoice *v = (PWVoice *) data;
    void *p;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    int32_t filled;
    uint32_t index, offs, n_bytes;

    assert(v->stream);

    /* obtain a buffer */
    b = pw_stream_dequeue_buffer(v->stream);
    if (b == NULL) {
        error_report("out of buffers: %s", strerror(errno));
        return;
    }

    /* Write data into buffer */
    buf = b->buffer;
    p = buf->datas[0].data;
    if (p == NULL) {
        return;
    }
    offs = SPA_MIN(buf->datas[0].chunk->offset, buf->datas[0].maxsize);
    n_bytes = SPA_MIN(buf->datas[0].chunk->size, buf->datas[0].maxsize - offs);

    filled = spa_ringbuffer_get_write_index(&v->ring, &index);


    if (filled < 0) {
        error_report("%p: underrun write:%u filled:%d", p, index, filled);
    } else {
        if ((uint32_t) filled + n_bytes > RINGBUFFER_SIZE) {
            error_report("%p: overrun write:%u filled:%d + size:%u > max:%u",
            p, index, filled, n_bytes, RINGBUFFER_SIZE);
        }
    }
    spa_ringbuffer_write_data(&v->ring,
                                v->buffer, RINGBUFFER_SIZE,
                                index & RINGBUFFER_MASK,
                                SPA_PTROFF(p, offs, void), n_bytes);
    index += n_bytes;
    spa_ringbuffer_write_update(&v->ring, index);

    /* queue the buffer for playback */
    pw_stream_queue_buffer(v->stream, b);
}

static void
on_stream_state_changed(void *data, enum pw_stream_state old,
                        enum pw_stream_state state, const char *error)
{
    PWVoice *v = (PWVoice *) data;

    trace_pw_state_changed(pw_stream_get_node_id(v->stream),
                           pw_stream_state_as_string(state));
}

static const struct pw_stream_events capture_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .destroy = stream_destroy,
    .state_changed = on_stream_state_changed,
    .process = capture_on_process
};

static const struct pw_stream_events playback_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .destroy = stream_destroy,
    .state_changed = on_stream_state_changed,
    .process = playback_on_process
};

static size_t
qpw_read(HWVoiceIn *hw, void *data, size_t len)
{
    PWVoiceIn *pw = (PWVoiceIn *) hw;
    PWVoice *v = &pw->v;
    pwaudio *c = v->g;
    const char *error = NULL;
    size_t l;
    int32_t avail;
    uint32_t index;

    pw_thread_loop_lock(c->thread_loop);
    if (pw_stream_get_state(v->stream, &error) != PW_STREAM_STATE_STREAMING) {
        /* wait for stream to become ready */
        l = 0;
        goto done_unlock;
    }
    /* get no of available bytes to read data from buffer */
    avail = spa_ringbuffer_get_read_index(&v->ring, &index);

    trace_pw_read(avail, index, len);

    if (avail < (int32_t) len) {
        len = avail;
    }

    spa_ringbuffer_read_data(&v->ring,
                             v->buffer, RINGBUFFER_SIZE,
                             index & RINGBUFFER_MASK, data, len);
    index += len;
    spa_ringbuffer_read_update(&v->ring, index);
    l = len;

done_unlock:
    pw_thread_loop_unlock(c->thread_loop);
    return l;
}

static size_t qpw_buffer_get_free(HWVoiceOut *hw)
{
    PWVoiceOut *pw = (PWVoiceOut *)hw;
    PWVoice *v = &pw->v;
    pwaudio *c = v->g;
    const char *error = NULL;
    int32_t filled, avail;
    uint32_t index;

    pw_thread_loop_lock(c->thread_loop);
    if (pw_stream_get_state(v->stream, &error) != PW_STREAM_STATE_STREAMING) {
        /* wait for stream to become ready */
        avail = 0;
        goto done_unlock;
    }

    filled = spa_ringbuffer_get_write_index(&v->ring, &index);
    avail = v->highwater_mark - filled;

done_unlock:
    pw_thread_loop_unlock(c->thread_loop);
    return avail;
}

static size_t
qpw_write(HWVoiceOut *hw, void *data, size_t len)
{
    PWVoiceOut *pw = (PWVoiceOut *) hw;
    PWVoice *v = &pw->v;
    pwaudio *c = v->g;
    const char *error = NULL;
    int32_t filled, avail;
    uint32_t index;

    pw_thread_loop_lock(c->thread_loop);
    if (pw_stream_get_state(v->stream, &error) != PW_STREAM_STATE_STREAMING) {
        /* wait for stream to become ready */
        len = 0;
        goto done_unlock;
    }
    filled = spa_ringbuffer_get_write_index(&v->ring, &index);
    avail = v->highwater_mark - filled;

    trace_pw_write(filled, avail, index, len);

    if (len > avail) {
        len = avail;
    }

    if (filled < 0) {
        error_report("%p: underrun write:%u filled:%d", pw, index, filled);
    } else {
        if ((uint32_t) filled + len > RINGBUFFER_SIZE) {
            error_report("%p: overrun write:%u filled:%d + size:%zu > max:%u",
            pw, index, filled, len, RINGBUFFER_SIZE);
        }
    }

    spa_ringbuffer_write_data(&v->ring,
                                v->buffer, RINGBUFFER_SIZE,
                                index & RINGBUFFER_MASK, data, len);
    index += len;
    spa_ringbuffer_write_update(&v->ring, index);

done_unlock:
    pw_thread_loop_unlock(c->thread_loop);
    return len;
}

static int
audfmt_to_pw(AudioFormat fmt, int endianness)
{
    int format;

    switch (fmt) {
    case AUDIO_FORMAT_S8:
        format = SPA_AUDIO_FORMAT_S8;
        break;
    case AUDIO_FORMAT_U8:
        format = SPA_AUDIO_FORMAT_U8;
        break;
    case AUDIO_FORMAT_S16:
        format = endianness ? SPA_AUDIO_FORMAT_S16_BE : SPA_AUDIO_FORMAT_S16_LE;
        break;
    case AUDIO_FORMAT_U16:
        format = endianness ? SPA_AUDIO_FORMAT_U16_BE : SPA_AUDIO_FORMAT_U16_LE;
        break;
    case AUDIO_FORMAT_S32:
        format = endianness ? SPA_AUDIO_FORMAT_S32_BE : SPA_AUDIO_FORMAT_S32_LE;
        break;
    case AUDIO_FORMAT_U32:
        format = endianness ? SPA_AUDIO_FORMAT_U32_BE : SPA_AUDIO_FORMAT_U32_LE;
        break;
    case AUDIO_FORMAT_F32:
        format = endianness ? SPA_AUDIO_FORMAT_F32_BE : SPA_AUDIO_FORMAT_F32_LE;
        break;
    default:
        dolog("Internal logic error: Bad audio format %d\n", fmt);
        format = SPA_AUDIO_FORMAT_U8;
        break;
    }
    return format;
}

static AudioFormat
pw_to_audfmt(enum spa_audio_format fmt, int *endianness,
             uint32_t *sample_size)
{
    switch (fmt) {
    case SPA_AUDIO_FORMAT_S8:
        *sample_size = 1;
        return AUDIO_FORMAT_S8;
    case SPA_AUDIO_FORMAT_U8:
        *sample_size = 1;
        return AUDIO_FORMAT_U8;
    case SPA_AUDIO_FORMAT_S16_BE:
        *sample_size = 2;
        *endianness = 1;
        return AUDIO_FORMAT_S16;
    case SPA_AUDIO_FORMAT_S16_LE:
        *sample_size = 2;
        *endianness = 0;
        return AUDIO_FORMAT_S16;
    case SPA_AUDIO_FORMAT_U16_BE:
        *sample_size = 2;
        *endianness = 1;
        return AUDIO_FORMAT_U16;
    case SPA_AUDIO_FORMAT_U16_LE:
        *sample_size = 2;
        *endianness = 0;
        return AUDIO_FORMAT_U16;
    case SPA_AUDIO_FORMAT_S32_BE:
        *sample_size = 4;
        *endianness = 1;
        return AUDIO_FORMAT_S32;
    case SPA_AUDIO_FORMAT_S32_LE:
        *sample_size = 4;
        *endianness = 0;
        return AUDIO_FORMAT_S32;
    case SPA_AUDIO_FORMAT_U32_BE:
        *sample_size = 4;
        *endianness = 1;
        return AUDIO_FORMAT_U32;
    case SPA_AUDIO_FORMAT_U32_LE:
        *sample_size = 4;
        *endianness = 0;
        return AUDIO_FORMAT_U32;
    case SPA_AUDIO_FORMAT_F32_BE:
        *sample_size = 4;
        *endianness = 1;
        return AUDIO_FORMAT_F32;
    case SPA_AUDIO_FORMAT_F32_LE:
        *sample_size = 4;
        *endianness = 0;
        return AUDIO_FORMAT_F32;
    default:
        *sample_size = 1;
        dolog("Internal logic error: Bad spa_audio_format %d\n", fmt);
        return AUDIO_FORMAT_U8;
    }
}

static int
qpw_stream_new(pwaudio *c, PWVoice *v, const char *stream_name,
               const char *name, enum spa_direction dir)
{
    int res;
    uint32_t n_params;
    const struct spa_pod *params[2];
    uint8_t buffer[1024];
    struct spa_pod_builder b;
    uint64_t buf_samples;
    struct pw_properties *props;

    props = pw_properties_new(NULL, NULL);
    if (!props) {
        error_report("Failed to create PW properties: %s", g_strerror(errno));
        return -1;
    }

    /* 75% of the timer period for faster updates */
    buf_samples = (uint64_t)v->g->dev->timer_period * v->info.rate
                    * 3 / 4 / 1000000;
    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%" PRIu64 "/%u",
                       buf_samples, v->info.rate);

    trace_pw_period(buf_samples, v->info.rate);
    if (name) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, name);
    }
    v->stream = pw_stream_new(c->core, stream_name, props);
    if (v->stream == NULL) {
        error_report("Failed to create PW stream: %s", g_strerror(errno));
        return -1;
    }

    if (dir == SPA_DIRECTION_INPUT) {
        pw_stream_add_listener(v->stream,
                            &v->stream_listener, &capture_stream_events, v);
    } else {
        pw_stream_add_listener(v->stream,
                            &v->stream_listener, &playback_stream_events, v);
    }

    n_params = 0;
    spa_pod_builder_init(&b, buffer, sizeof(buffer));
    params[n_params++] = spa_format_audio_raw_build(&b,
                            SPA_PARAM_EnumFormat,
                            &v->info);

    /* connect the stream to a sink or source */
    res = pw_stream_connect(v->stream,
                            dir ==
                            SPA_DIRECTION_INPUT ? PW_DIRECTION_INPUT :
                            PW_DIRECTION_OUTPUT, PW_ID_ANY,
                            PW_STREAM_FLAG_AUTOCONNECT |
                            PW_STREAM_FLAG_INACTIVE |
                            PW_STREAM_FLAG_MAP_BUFFERS |
                            PW_STREAM_FLAG_RT_PROCESS, params, n_params);
    if (res < 0) {
        error_report("Failed to connect PW stream: %s", g_strerror(errno));
        pw_stream_destroy(v->stream);
        return -1;
    }

    return 0;
}

static void
qpw_set_position(uint32_t channels, uint32_t position[SPA_AUDIO_MAX_CHANNELS])
{
    memcpy(position, (uint32_t[SPA_AUDIO_MAX_CHANNELS]) { SPA_AUDIO_CHANNEL_UNKNOWN, },
           sizeof(uint32_t) * SPA_AUDIO_MAX_CHANNELS);
    /*
     * TODO: This currently expects the only frontend supporting more than 2
     * channels is the usb-audio.  We will need some means to set channel
     * order when a new frontend gains multi-channel support.
     */
    switch (channels) {
    case 8:
        position[6] = SPA_AUDIO_CHANNEL_SL;
        position[7] = SPA_AUDIO_CHANNEL_SR;
        /* fallthrough */
    case 6:
        position[2] = SPA_AUDIO_CHANNEL_FC;
        position[3] = SPA_AUDIO_CHANNEL_LFE;
        position[4] = SPA_AUDIO_CHANNEL_RL;
        position[5] = SPA_AUDIO_CHANNEL_RR;
        /* fallthrough */
    case 2:
        position[0] = SPA_AUDIO_CHANNEL_FL;
        position[1] = SPA_AUDIO_CHANNEL_FR;
        break;
    case 1:
        position[0] = SPA_AUDIO_CHANNEL_MONO;
        break;
    default:
        dolog("Internal error: unsupported channel count %d\n", channels);
    }
}

static int
qpw_init_out(HWVoiceOut *hw, struct audsettings *as, void *drv_opaque)
{
    PWVoiceOut *pw = (PWVoiceOut *) hw;
    PWVoice *v = &pw->v;
    struct audsettings obt_as = *as;
    pwaudio *c = v->g = drv_opaque;
    AudiodevPipewireOptions *popts = &c->dev->u.pipewire;
    AudiodevPipewirePerDirectionOptions *ppdo = popts->out;
    int r;

    pw_thread_loop_lock(c->thread_loop);

    v->info.format = audfmt_to_pw(as->fmt, as->endianness);
    v->info.channels = as->nchannels;
    qpw_set_position(as->nchannels, v->info.position);
    v->info.rate = as->freq;

    obt_as.fmt =
        pw_to_audfmt(v->info.format, &obt_as.endianness, &v->frame_size);
    v->frame_size *= as->nchannels;

    v->req = (uint64_t)c->dev->timer_period * v->info.rate
        * 1 / 2 / 1000000 * v->frame_size;

    /* call the function that creates a new stream for playback */
    r = qpw_stream_new(c, v, ppdo->stream_name ? : c->dev->id,
                       ppdo->name, SPA_DIRECTION_OUTPUT);
    if (r < 0) {
        pw_thread_loop_unlock(c->thread_loop);
        return -1;
    }

    /* report the audio format we support */
    audio_pcm_init_info(&hw->info, &obt_as);

    /* report the buffer size to qemu */
    hw->samples = audio_buffer_frames(
        qapi_AudiodevPipewirePerDirectionOptions_base(ppdo), &obt_as, 46440);
    v->highwater_mark = MIN(RINGBUFFER_SIZE,
                            (ppdo->has_latency ? ppdo->latency : 46440)
                            * (uint64_t)v->info.rate / 1000000 * v->frame_size);

    pw_thread_loop_unlock(c->thread_loop);
    return 0;
}

static int
qpw_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    PWVoiceIn *pw = (PWVoiceIn *) hw;
    PWVoice *v = &pw->v;
    struct audsettings obt_as = *as;
    pwaudio *c = v->g = drv_opaque;
    AudiodevPipewireOptions *popts = &c->dev->u.pipewire;
    AudiodevPipewirePerDirectionOptions *ppdo = popts->in;
    int r;

    pw_thread_loop_lock(c->thread_loop);

    v->info.format = audfmt_to_pw(as->fmt, as->endianness);
    v->info.channels = as->nchannels;
    qpw_set_position(as->nchannels, v->info.position);
    v->info.rate = as->freq;

    obt_as.fmt =
        pw_to_audfmt(v->info.format, &obt_as.endianness, &v->frame_size);
    v->frame_size *= as->nchannels;

    /* call the function that creates a new stream for recording */
    r = qpw_stream_new(c, v, ppdo->stream_name ? : c->dev->id,
                       ppdo->name, SPA_DIRECTION_INPUT);
    if (r < 0) {
        pw_thread_loop_unlock(c->thread_loop);
        return -1;
    }

    /* report the audio format we support */
    audio_pcm_init_info(&hw->info, &obt_as);

    /* report the buffer size to qemu */
    hw->samples = audio_buffer_frames(
        qapi_AudiodevPipewirePerDirectionOptions_base(ppdo), &obt_as, 46440);

    pw_thread_loop_unlock(c->thread_loop);
    return 0;
}

static void
qpw_voice_fini(PWVoice *v)
{
    pwaudio *c = v->g;

    if (!v->stream) {
        return;
    }
    pw_thread_loop_lock(c->thread_loop);
    pw_stream_destroy(v->stream);
    v->stream = NULL;
    pw_thread_loop_unlock(c->thread_loop);
}

static void
qpw_fini_out(HWVoiceOut *hw)
{
    qpw_voice_fini(&PW_VOICE_OUT(hw)->v);
}

static void
qpw_fini_in(HWVoiceIn *hw)
{
    qpw_voice_fini(&PW_VOICE_IN(hw)->v);
}

static void
qpw_voice_set_enabled(PWVoice *v, bool enable)
{
    pwaudio *c = v->g;
    pw_thread_loop_lock(c->thread_loop);
    pw_stream_set_active(v->stream, enable);
    pw_thread_loop_unlock(c->thread_loop);
}

static void
qpw_enable_out(HWVoiceOut *hw, bool enable)
{
    qpw_voice_set_enabled(&PW_VOICE_OUT(hw)->v, enable);
}

static void
qpw_enable_in(HWVoiceIn *hw, bool enable)
{
    qpw_voice_set_enabled(&PW_VOICE_IN(hw)->v, enable);
}

static void
qpw_voice_set_volume(PWVoice *v, Volume *vol)
{
    pwaudio *c = v->g;
    int i, ret;

    pw_thread_loop_lock(c->thread_loop);
    v->volume.channels = vol->channels;

    for (i = 0; i < vol->channels; ++i) {
        v->volume.values[i] = (float)vol->vol[i] / 255;
    }

    ret = pw_stream_set_control(v->stream,
        SPA_PROP_channelVolumes, v->volume.channels, v->volume.values, 0);
    trace_pw_vol(ret == 0 ? "success" : "failed");

    v->muted = vol->mute;
    float val = v->muted ? 1.f : 0.f;
    ret = pw_stream_set_control(v->stream, SPA_PROP_mute, 1, &val, 0);
    pw_thread_loop_unlock(c->thread_loop);
}

static void
qpw_volume_out(HWVoiceOut *hw, Volume *vol)
{
    qpw_voice_set_volume(&PW_VOICE_OUT(hw)->v, vol);
}

static void
qpw_volume_in(HWVoiceIn *hw, Volume *vol)
{
    qpw_voice_set_volume(&PW_VOICE_IN(hw)->v, vol);
}

static int wait_resync(pwaudio *pw)
{
    int res;
    pw->pending_seq = pw_core_sync(pw->core, PW_ID_CORE, pw->pending_seq);

    while (true) {
        pw_thread_loop_wait(pw->thread_loop);

        res = pw->error;
        if (res < 0) {
            pw->error = 0;
            return res;
        }
        if (pw->pending_seq == pw->last_seq) {
            break;
        }
    }
    return 0;
}

static void
on_core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
    pwaudio *pw = data;

    error_report("error id:%u seq:%d res:%d (%s): %s",
                id, seq, res, spa_strerror(res), message);

    /* stop and exit the thread loop */
    pw_thread_loop_signal(pw->thread_loop, FALSE);
}

static void
on_core_done(void *data, uint32_t id, int seq)
{
    pwaudio *pw = data;
    assert(id == PW_ID_CORE);
    pw->last_seq = seq;
    if (pw->pending_seq == seq) {
        /* stop and exit the thread loop */
        pw_thread_loop_signal(pw->thread_loop, FALSE);
    }
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
    .error = on_core_error,
};

static void *
qpw_audio_init(Audiodev *dev, Error **errp)
{
    g_autofree pwaudio *pw = g_new0(pwaudio, 1);

    assert(dev->driver == AUDIODEV_DRIVER_PIPEWIRE);
    trace_pw_audio_init();

    pw_init(NULL, NULL);

    pw->dev = dev;
    pw->thread_loop = pw_thread_loop_new("PipeWire thread loop", NULL);
    if (pw->thread_loop == NULL) {
        error_setg_errno(errp, errno, "Could not create PipeWire loop");
        goto fail;
    }

    pw->context =
        pw_context_new(pw_thread_loop_get_loop(pw->thread_loop), NULL, 0);
    if (pw->context == NULL) {
        error_setg_errno(errp, errno, "Could not create PipeWire context");
        goto fail;
    }

    if (pw_thread_loop_start(pw->thread_loop) < 0) {
        error_setg_errno(errp, errno, "Could not start PipeWire loop");
        goto fail;
    }

    pw_thread_loop_lock(pw->thread_loop);

    pw->core = pw_context_connect(pw->context, NULL, 0);
    if (pw->core == NULL) {
        pw_thread_loop_unlock(pw->thread_loop);
        error_setg_errno(errp, errno, "Failed to connect to PipeWire instance");
        goto fail;
    }

    if (pw_core_add_listener(pw->core, &pw->core_listener,
                             &core_events, pw) < 0) {
        pw_thread_loop_unlock(pw->thread_loop);
        error_setg(errp, "Failed to add PipeWire listener");
        goto fail;
    }
    if (wait_resync(pw) < 0) {
        pw_thread_loop_unlock(pw->thread_loop);
    }

    pw_thread_loop_unlock(pw->thread_loop);

    return g_steal_pointer(&pw);

fail:
    if (pw->thread_loop) {
        pw_thread_loop_stop(pw->thread_loop);
    }
    g_clear_pointer(&pw->context, pw_context_destroy);
    g_clear_pointer(&pw->thread_loop, pw_thread_loop_destroy);
    return NULL;
}

static void
qpw_audio_fini(void *opaque)
{
    pwaudio *pw = opaque;

    if (pw->thread_loop) {
        pw_thread_loop_stop(pw->thread_loop);
    }

    if (pw->core) {
        spa_hook_remove(&pw->core_listener);
        spa_zero(pw->core_listener);
        pw_core_disconnect(pw->core);
    }

    if (pw->context) {
        pw_context_destroy(pw->context);
    }
    pw_thread_loop_destroy(pw->thread_loop);

    g_free(pw);
}

static struct audio_pcm_ops qpw_pcm_ops = {
    .init_out = qpw_init_out,
    .fini_out = qpw_fini_out,
    .write = qpw_write,
    .buffer_get_free = qpw_buffer_get_free,
    .run_buffer_out = audio_generic_run_buffer_out,
    .enable_out = qpw_enable_out,
    .volume_out = qpw_volume_out,
    .volume_in = qpw_volume_in,

    .init_in = qpw_init_in,
    .fini_in = qpw_fini_in,
    .read = qpw_read,
    .run_buffer_in = audio_generic_run_buffer_in,
    .enable_in = qpw_enable_in
};

static struct audio_driver pw_audio_driver = {
    .name = "pipewire",
    .descr = "http://www.pipewire.org/",
    .init = qpw_audio_init,
    .fini = qpw_audio_fini,
    .pcm_ops = &qpw_pcm_ops,
    .max_voices_out = INT_MAX,
    .max_voices_in = INT_MAX,
    .voice_size_out = sizeof(PWVoiceOut),
    .voice_size_in = sizeof(PWVoiceIn),
};

static void
register_audio_pw(void)
{
    audio_driver_register(&pw_audio_driver);
}

type_init(register_audio_pw);
