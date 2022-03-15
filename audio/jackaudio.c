/*
 * QEMU JACK Audio Connection Kit Client
 *
 * Copyright (c) 2020 Geoffrey McRae (gnif)
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
#include "qemu/module.h"
#include "qemu/atomic.h"
#include "qemu/main-loop.h"
#include "audio.h"

#define AUDIO_CAP "jack"
#include "audio_int.h"

#include <jack/jack.h>
#include <jack/thread.h>

struct QJack;

typedef enum QJackState {
    QJACK_STATE_DISCONNECTED,
    QJACK_STATE_RUNNING,
    QJACK_STATE_SHUTDOWN
}
QJackState;

typedef struct QJackBuffer {
    int          channels;
    int          frames;
    uint32_t     used;
    int          rptr, wptr;
    float      **data;
}
QJackBuffer;

typedef struct QJackClient {
    AudiodevJackPerDirectionOptions *opt;

    bool out;
    bool enabled;
    bool connect_ports;
    int  packets;

    QJackState      state;
    jack_client_t  *client;
    jack_nframes_t  freq;
    QEMUBH         *shutdown_bh;

    struct QJack   *j;
    int             nchannels;
    int             buffersize;
    jack_port_t   **port;
    QJackBuffer     fifo;
}
QJackClient;

typedef struct QJackOut {
    HWVoiceOut  hw;
    QJackClient c;
}
QJackOut;

typedef struct QJackIn {
    HWVoiceIn   hw;
    QJackClient c;
}
QJackIn;

static int qjack_client_init(QJackClient *c);
static void qjack_client_connect_ports(QJackClient *c);
static void qjack_client_fini(QJackClient *c);
static QemuMutex qjack_shutdown_lock;

static void qjack_buffer_create(QJackBuffer *buffer, int channels, int frames)
{
    buffer->channels = channels;
    buffer->frames   = frames;
    buffer->used     = 0;
    buffer->rptr     = 0;
    buffer->wptr     = 0;
    buffer->data     = g_new(float *, channels);
    for (int i = 0; i < channels; ++i) {
        buffer->data[i] = g_new(float, frames);
    }
}

static void qjack_buffer_clear(QJackBuffer *buffer)
{
    assert(buffer->data);
    qatomic_store_release(&buffer->used, 0);
    buffer->rptr = 0;
    buffer->wptr = 0;
}

static void qjack_buffer_free(QJackBuffer *buffer)
{
    if (!buffer->data) {
        return;
    }

    for (int i = 0; i < buffer->channels; ++i) {
        g_free(buffer->data[i]);
    }

    g_free(buffer->data);
    buffer->data = NULL;
}

/* write PCM interleaved */
static int qjack_buffer_write(QJackBuffer *buffer, float *data, int size)
{
    assert(buffer->data);
    const int samples = size / sizeof(float);
    int frames        = samples / buffer->channels;
    const int avail   = buffer->frames - qatomic_load_acquire(&buffer->used);

    if (frames > avail) {
        frames = avail;
    }

    int copy = frames;
    int wptr = buffer->wptr;

    while (copy) {

        for (int c = 0; c < buffer->channels; ++c) {
            buffer->data[c][wptr] = *data++;
        }

        if (++wptr == buffer->frames) {
            wptr = 0;
        }

        --copy;
    }

    buffer->wptr = wptr;

    qatomic_add(&buffer->used, frames);
    return frames * buffer->channels * sizeof(float);
};

/* write PCM linear */
static int qjack_buffer_write_l(QJackBuffer *buffer, float **dest, int frames)
{
    assert(buffer->data);
    const int avail   = buffer->frames - qatomic_load_acquire(&buffer->used);
    int wptr = buffer->wptr;

    if (frames > avail) {
        frames = avail;
    }

    int right = buffer->frames - wptr;
    if (right > frames) {
        right = frames;
    }

    const int left = frames - right;
    for (int c = 0; c < buffer->channels; ++c) {
        memcpy(buffer->data[c] + wptr, dest[c]        , right * sizeof(float));
        memcpy(buffer->data[c]       , dest[c] + right, left  * sizeof(float));
    }

    wptr += frames;
    if (wptr >= buffer->frames) {
        wptr -= buffer->frames;
    }
    buffer->wptr = wptr;

    qatomic_add(&buffer->used, frames);
    return frames;
}

/* read PCM interleaved */
static int qjack_buffer_read(QJackBuffer *buffer, float *dest, int size)
{
    assert(buffer->data);
    const int samples = size / sizeof(float);
    int frames        = samples / buffer->channels;
    const int avail   = qatomic_load_acquire(&buffer->used);

    if (frames > avail) {
        frames = avail;
    }

    int copy = frames;
    int rptr = buffer->rptr;

    while (copy) {

        for (int c = 0; c < buffer->channels; ++c) {
            *dest++ = buffer->data[c][rptr];
        }

        if (++rptr == buffer->frames) {
            rptr = 0;
        }

        --copy;
    }

    buffer->rptr = rptr;

    qatomic_sub(&buffer->used, frames);
    return frames * buffer->channels * sizeof(float);
}

/* read PCM linear */
static int qjack_buffer_read_l(QJackBuffer *buffer, float **dest, int frames)
{
    assert(buffer->data);
    int copy       = frames;
    const int used = qatomic_load_acquire(&buffer->used);
    int rptr       = buffer->rptr;

    if (copy > used) {
        copy = used;
    }

    int right = buffer->frames - rptr;
    if (right > copy) {
        right = copy;
    }

    const int left = copy - right;
    for (int c = 0; c < buffer->channels; ++c) {
        memcpy(dest[c]        , buffer->data[c] + rptr, right * sizeof(float));
        memcpy(dest[c] + right, buffer->data[c]       , left  * sizeof(float));
    }

    rptr += copy;
    if (rptr >= buffer->frames) {
        rptr -= buffer->frames;
    }
    buffer->rptr = rptr;

    qatomic_sub(&buffer->used, copy);
    return copy;
}

static int qjack_process(jack_nframes_t nframes, void *arg)
{
    QJackClient *c = (QJackClient *)arg;

    if (c->state != QJACK_STATE_RUNNING) {
        return 0;
    }

    /* get the buffers for the ports */
    float *buffers[c->nchannels];
    for (int i = 0; i < c->nchannels; ++i) {
        buffers[i] = jack_port_get_buffer(c->port[i], nframes);
    }

    if (c->out) {
        if (likely(c->enabled)) {
            qjack_buffer_read_l(&c->fifo, buffers, nframes);
        } else {
            for (int i = 0; i < c->nchannels; ++i) {
                memset(buffers[i], 0, nframes * sizeof(float));
            }
        }
    } else {
        if (likely(c->enabled)) {
            qjack_buffer_write_l(&c->fifo, buffers, nframes);
        }
    }

    return 0;
}

static void qjack_port_registration(jack_port_id_t port, int reg, void *arg)
{
    if (reg) {
        QJackClient *c = (QJackClient *)arg;
        c->connect_ports = true;
    }
}

static int qjack_xrun(void *arg)
{
    QJackClient *c = (QJackClient *)arg;
    if (c->state != QJACK_STATE_RUNNING) {
        return 0;
    }

    qjack_buffer_clear(&c->fifo);
    return 0;
}

static void qjack_shutdown_bh(void *opaque)
{
    QJackClient *c = (QJackClient *)opaque;
    qjack_client_fini(c);
}

static void qjack_shutdown(void *arg)
{
    QJackClient *c = (QJackClient *)arg;
    c->state = QJACK_STATE_SHUTDOWN;
    qemu_bh_schedule(c->shutdown_bh);
}

static void qjack_client_recover(QJackClient *c)
{
    if (c->state != QJACK_STATE_DISCONNECTED) {
        return;
    }

    /* packets is used simply to throttle this */
    if (c->packets % 100 == 0) {

        /* if enabled then attempt to recover */
        if (c->enabled) {
            dolog("attempting to reconnect to server\n");
            qjack_client_init(c);
        }
    }
}

static size_t qjack_write(HWVoiceOut *hw, void *buf, size_t len)
{
    QJackOut *jo = (QJackOut *)hw;
    ++jo->c.packets;

    if (jo->c.state != QJACK_STATE_RUNNING) {
        qjack_client_recover(&jo->c);
        return len;
    }

    qjack_client_connect_ports(&jo->c);
    return qjack_buffer_write(&jo->c.fifo, buf, len);
}

static size_t qjack_read(HWVoiceIn *hw, void *buf, size_t len)
{
    QJackIn *ji = (QJackIn *)hw;
    ++ji->c.packets;

    if (ji->c.state != QJACK_STATE_RUNNING) {
        qjack_client_recover(&ji->c);
        return len;
    }

    qjack_client_connect_ports(&ji->c);
    return qjack_buffer_read(&ji->c.fifo, buf, len);
}

static void qjack_client_connect_ports(QJackClient *c)
{
    if (!c->connect_ports || !c->opt->connect_ports) {
        return;
    }

    c->connect_ports = false;
    const char **ports;
    ports = jack_get_ports(c->client, c->opt->connect_ports, NULL,
        c->out ? JackPortIsInput : JackPortIsOutput);

    if (!ports) {
        return;
    }

    for (int i = 0; i < c->nchannels && ports[i]; ++i) {
        const char *p = jack_port_name(c->port[i]);
        if (jack_port_connected_to(c->port[i], ports[i])) {
            continue;
        }

        if (c->out) {
            dolog("connect %s -> %s\n", p, ports[i]);
            jack_connect(c->client, p, ports[i]);
        } else {
            dolog("connect %s -> %s\n", ports[i], p);
            jack_connect(c->client, ports[i], p);
        }
    }
}

static int qjack_client_init(QJackClient *c)
{
    jack_status_t status;
    char client_name[jack_client_name_size()];
    jack_options_t options = JackNullOption;

    if (c->state == QJACK_STATE_RUNNING) {
        return 0;
    }

    c->connect_ports = true;

    snprintf(client_name, sizeof(client_name), "%s-%s",
        c->out ? "out" : "in",
        c->opt->client_name ? c->opt->client_name : audio_application_name());

    if (c->opt->exact_name) {
        options |= JackUseExactName;
    }

    if (!c->opt->start_server) {
        options |= JackNoStartServer;
    }

    if (c->opt->server_name) {
        options |= JackServerName;
    }

    c->client = jack_client_open(client_name, options, &status,
      c->opt->server_name);

    if (c->client == NULL) {
        dolog("jack_client_open failed: status = 0x%2.0x\n", status);
        if (status & JackServerFailed) {
            dolog("unable to connect to JACK server\n");
        }
        return -1;
    }

    c->freq = jack_get_sample_rate(c->client);

    if (status & JackServerStarted) {
        dolog("JACK server started\n");
    }

    if (status & JackNameNotUnique) {
        dolog("JACK unique name assigned %s\n",
          jack_get_client_name(c->client));
    }

    jack_set_process_callback(c->client, qjack_process , c);
    jack_set_port_registration_callback(c->client, qjack_port_registration, c);
    jack_set_xrun_callback(c->client, qjack_xrun, c);
    jack_on_shutdown(c->client, qjack_shutdown, c);

    /* allocate and register the ports */
    c->port = g_new(jack_port_t *, c->nchannels);
    for (int i = 0; i < c->nchannels; ++i) {

        char port_name[16];
        snprintf(
            port_name,
            sizeof(port_name),
            c->out ? "output %d" : "input %d",
            i);

        c->port[i] = jack_port_register(
            c->client,
            port_name,
            JACK_DEFAULT_AUDIO_TYPE,
            c->out ? JackPortIsOutput : JackPortIsInput,
            0);
    }

    /* activate the session */
    jack_activate(c->client);
    c->buffersize = jack_get_buffer_size(c->client);

    /*
     * ensure the buffersize is no smaller then 512 samples, some (all?) qemu
     * virtual devices do not work correctly otherwise
     */
    if (c->buffersize < 512) {
        c->buffersize = 512;
    }

    /* create a 3 period buffer */
    qjack_buffer_create(&c->fifo, c->nchannels, c->buffersize * 3);

    qjack_client_connect_ports(c);
    c->state = QJACK_STATE_RUNNING;
    return 0;
}

static int qjack_init_out(HWVoiceOut *hw, struct audsettings *as,
    void *drv_opaque)
{
    QJackOut *jo  = (QJackOut *)hw;
    Audiodev *dev = (Audiodev *)drv_opaque;

    jo->c.out       = true;
    jo->c.enabled   = false;
    jo->c.nchannels = as->nchannels;
    jo->c.opt       = dev->u.jack.out;

    jo->c.shutdown_bh = qemu_bh_new(qjack_shutdown_bh, &jo->c);

    int ret = qjack_client_init(&jo->c);
    if (ret != 0) {
        qemu_bh_delete(jo->c.shutdown_bh);
        return ret;
    }

    /* report the buffer size to qemu */
    hw->samples = jo->c.buffersize;

    /* report the audio format we support */
    struct audsettings os = {
        .freq       = jo->c.freq,
        .nchannels  = jo->c.nchannels,
        .fmt        = AUDIO_FORMAT_F32,
        .endianness = 0
    };
    audio_pcm_init_info(&hw->info, &os);

    dolog("JACK output configured for %dHz (%d samples)\n",
        jo->c.freq, jo->c.buffersize);

    return 0;
}

static int qjack_init_in(HWVoiceIn *hw, struct audsettings *as,
    void *drv_opaque)
{
    QJackIn  *ji  = (QJackIn *)hw;
    Audiodev *dev = (Audiodev *)drv_opaque;

    ji->c.out       = false;
    ji->c.enabled   = false;
    ji->c.nchannels = as->nchannels;
    ji->c.opt       = dev->u.jack.in;

    ji->c.shutdown_bh = qemu_bh_new(qjack_shutdown_bh, &ji->c);

    int ret = qjack_client_init(&ji->c);
    if (ret != 0) {
        qemu_bh_delete(ji->c.shutdown_bh);
        return ret;
    }

    /* report the buffer size to qemu */
    hw->samples = ji->c.buffersize;

    /* report the audio format we support */
    struct audsettings is = {
        .freq       = ji->c.freq,
        .nchannels  = ji->c.nchannels,
        .fmt        = AUDIO_FORMAT_F32,
        .endianness = 0
    };
    audio_pcm_init_info(&hw->info, &is);

    dolog("JACK input configured for %dHz (%d samples)\n",
        ji->c.freq, ji->c.buffersize);

    return 0;
}

static void qjack_client_fini_locked(QJackClient *c)
{
    switch (c->state) {
    case QJACK_STATE_RUNNING:
        jack_deactivate(c->client);
        /* fallthrough */

    case QJACK_STATE_SHUTDOWN:
        jack_client_close(c->client);
        c->client = NULL;

        qjack_buffer_free(&c->fifo);
        g_free(c->port);

        c->state = QJACK_STATE_DISCONNECTED;
        /* fallthrough */

    case QJACK_STATE_DISCONNECTED:
        break;
    }
}

static void qjack_client_fini(QJackClient *c)
{
    qemu_mutex_lock(&qjack_shutdown_lock);
    qjack_client_fini_locked(c);
    qemu_mutex_unlock(&qjack_shutdown_lock);
}

static void qjack_fini_out(HWVoiceOut *hw)
{
    QJackOut *jo = (QJackOut *)hw;
    qjack_client_fini(&jo->c);

    qemu_bh_delete(jo->c.shutdown_bh);
}

static void qjack_fini_in(HWVoiceIn *hw)
{
    QJackIn *ji = (QJackIn *)hw;
    qjack_client_fini(&ji->c);

    qemu_bh_delete(ji->c.shutdown_bh);
}

static void qjack_enable_out(HWVoiceOut *hw, bool enable)
{
    QJackOut *jo = (QJackOut *)hw;
    jo->c.enabled = enable;
}

static void qjack_enable_in(HWVoiceIn *hw, bool enable)
{
    QJackIn *ji = (QJackIn *)hw;
    ji->c.enabled = enable;
}

#if !defined(WIN32) && defined(CONFIG_PTHREAD_SETNAME_NP_W_TID)
static int qjack_thread_creator(jack_native_thread_t *thread,
    const pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
    int ret = pthread_create(thread, attr, function, arg);
    if (ret != 0) {
        return ret;
    }

    /* set the name of the thread */
    pthread_setname_np(*thread, "jack-client");

    return ret;
}
#endif

static void *qjack_init(Audiodev *dev)
{
    assert(dev->driver == AUDIODEV_DRIVER_JACK);
    return dev;
}

static void qjack_fini(void *opaque)
{
}

static struct audio_pcm_ops jack_pcm_ops = {
    .init_out       = qjack_init_out,
    .fini_out       = qjack_fini_out,
    .write          = qjack_write,
    .buffer_get_free = audio_generic_buffer_get_free,
    .run_buffer_out = audio_generic_run_buffer_out,
    .enable_out     = qjack_enable_out,

    .init_in        = qjack_init_in,
    .fini_in        = qjack_fini_in,
    .read           = qjack_read,
    .run_buffer_in  = audio_generic_run_buffer_in,
    .enable_in      = qjack_enable_in
};

static struct audio_driver jack_driver = {
    .name           = "jack",
    .descr          = "JACK Audio Connection Kit Client",
    .init           = qjack_init,
    .fini           = qjack_fini,
    .pcm_ops        = &jack_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof(QJackOut),
    .voice_size_in  = sizeof(QJackIn)
};

static void qjack_error(const char *msg)
{
    dolog("E: %s\n", msg);
}

static void qjack_info(const char *msg)
{
    dolog("I: %s\n", msg);
}

static void register_audio_jack(void)
{
    qemu_mutex_init(&qjack_shutdown_lock);
    audio_driver_register(&jack_driver);
#if !defined(WIN32) && defined(CONFIG_PTHREAD_SETNAME_NP_W_TID)
    jack_set_thread_creator(qjack_thread_creator);
#endif
    jack_set_error_function(qjack_error);
    jack_set_info_function(qjack_info);
}
type_init(register_audio_jack);
