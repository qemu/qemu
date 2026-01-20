/*
 * QEMU Audio subsystem header
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

#ifndef QEMU_AUDIO_H
#define QEMU_AUDIO_H

#include "qemu/queue.h"
#include "qapi/qapi-types-audio.h"
#include "hw/core/qdev-properties-system.h"
#ifdef CONFIG_GIO
#include "gio/gio.h"
#endif

typedef void (*audio_callback_fn) (void *opaque, int avail);

typedef struct audsettings {
    int freq;
    int nchannels;
    AudioFormat fmt;
    int endianness;
} audsettings;

typedef struct SWVoiceOut SWVoiceOut;
typedef struct SWVoiceIn SWVoiceIn;
typedef struct CaptureVoiceOut CaptureVoiceOut;

typedef enum {
    AUD_CNOTIFY_ENABLE,
    AUD_CNOTIFY_DISABLE
} audcnotification_e;

struct audio_capture_ops {
    void (*notify) (void *opaque, audcnotification_e cmd);
    void (*capture) (void *opaque, const void *buf, int size);
    void (*destroy) (void *opaque);
};

#define AUDIO_MAX_CHANNELS 16
typedef struct Volume {
    bool mute;
    int channels;
    uint8_t vol[AUDIO_MAX_CHANNELS];
} Volume;

typedef struct AudioBackend {
    Object parent_obj;
} AudioBackend;

typedef struct AudioBackendClass {
    ObjectClass parent_class;

    bool (*realize)(AudioBackend *be, Audiodev *dev, Error **errp);
    const char *(*get_id)(AudioBackend *be);
    SWVoiceOut *(*open_out)(AudioBackend *be,
                            SWVoiceOut *sw,
                            const char *name,
                            void *callback_opaque,
                            audio_callback_fn callback_fn,
                            const struct audsettings *as);
    SWVoiceIn *(*open_in)(AudioBackend *be,
                          SWVoiceIn *sw,
                          const char *name,
                          void *callback_opaque,
                          audio_callback_fn callback_fn,
                          const struct audsettings *as);
    void (*close_out)(AudioBackend *be, SWVoiceOut *sw);
    void (*close_in)(AudioBackend *be, SWVoiceIn *sw);
    bool (*is_active_out)(AudioBackend *be, SWVoiceOut *sw);
    bool (*is_active_in)(AudioBackend *be, SWVoiceIn *sw);
    void (*set_active_out)(AudioBackend *be, SWVoiceOut *sw, bool on);
    void (*set_active_in)(AudioBackend *be, SWVoiceIn *sw, bool on);
    void (*set_volume_out)(AudioBackend *be, SWVoiceOut *sw, Volume *vol);
    void (*set_volume_in)(AudioBackend *be, SWVoiceIn *sw, Volume *vol);
    size_t (*write)(AudioBackend *be, SWVoiceOut *sw, void *buf, size_t size);
    size_t (*read)(AudioBackend *be, SWVoiceIn *sw, void *buf, size_t size);
    int (*get_buffer_size_out)(AudioBackend *be, SWVoiceOut *sw);
    CaptureVoiceOut *(*add_capture)(AudioBackend *be,
                                    const struct audsettings *as,
                                    const struct audio_capture_ops *ops,
                                    void *cb_opaque);
    void (*del_capture)(AudioBackend *be, CaptureVoiceOut *cap, void *cb_opaque);

#ifdef CONFIG_GIO
    bool (*set_dbus_server)(AudioBackend *be,
                            GDBusObjectManagerServer *manager,
                            bool p2p,
                            Error **errp);
#endif
} AudioBackendClass;

bool audio_be_check(AudioBackend **be, Error **errp);

AudioBackend *audio_be_new(Audiodev *dev, Error **errp);

SWVoiceOut *audio_be_open_out(
    AudioBackend *be,
    SWVoiceOut *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    const struct audsettings *settings);

void audio_be_close_out(AudioBackend *be, SWVoiceOut *sw);
size_t audio_be_write(AudioBackend *be, SWVoiceOut *sw, void *pcm_buf, size_t size);
int  audio_be_get_buffer_size_out(AudioBackend *be, SWVoiceOut *sw);
void audio_be_set_active_out(AudioBackend *be, SWVoiceOut *sw, bool on);
bool audio_be_is_active_out(AudioBackend *be, SWVoiceOut *sw);


void audio_be_set_volume_out(AudioBackend *be, SWVoiceOut *sw, Volume *vol);
void audio_be_set_volume_in(AudioBackend *be, SWVoiceIn *sw, Volume *vol);

static inline void
audio_be_set_volume_out_lr(AudioBackend *be, SWVoiceOut *sw,
                           bool mut, uint8_t lvol, uint8_t rvol) {
    audio_be_set_volume_out(be, sw, &(Volume) {
        .mute = mut, .channels = 2, .vol = { lvol, rvol }
    });
}

static inline void
audio_be_set_volume_in_lr(AudioBackend *be, SWVoiceIn *sw,
                          bool mut, uint8_t lvol, uint8_t rvol) {
    audio_be_set_volume_in(be, sw, &(Volume) {
        .mute = mut, .channels = 2, .vol = { lvol, rvol }
    });
}

SWVoiceIn *audio_be_open_in(
    AudioBackend *be,
    SWVoiceIn *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    const struct audsettings *settings
    );

void audio_be_close_in(AudioBackend *be, SWVoiceIn *sw);
size_t audio_be_read(AudioBackend *be, SWVoiceIn *sw, void *pcm_buf, size_t size);
void audio_be_set_active_in(AudioBackend *be, SWVoiceIn *sw, bool on);
bool audio_be_is_active_in(AudioBackend *be, SWVoiceIn *sw);

void audio_cleanup(void);

typedef struct st_sample st_sample;

void audio_add_audiodev(Audiodev *audio);
void audio_add_default_audiodev(Audiodev *dev, Error **errp);
void audio_parse_option(const char *opt);
void audio_create_default_audiodevs(void);
void audio_init_audiodevs(void);
void audio_help(void);

AudioBackend *audio_be_by_name(const char *name, Error **errp);
AudioBackend *audio_get_default_audio_be(Error **errp);
const char *audio_be_get_id(AudioBackend *be);
#ifdef CONFIG_GIO
bool audio_be_can_set_dbus_server(AudioBackend *be);
bool audio_be_set_dbus_server(AudioBackend *be,
                              GDBusObjectManagerServer *server,
                              bool p2p,
                              Error **errp);
#endif

const char *audio_application_name(void);

static inline int audio_format_bits(AudioFormat fmt)
{
    switch (fmt) {
    case AUDIO_FORMAT_S8:
    case AUDIO_FORMAT_U8:
        return 8;

    case AUDIO_FORMAT_S16:
    case AUDIO_FORMAT_U16:
        return 16;

    case AUDIO_FORMAT_F32:
    case AUDIO_FORMAT_S32:
    case AUDIO_FORMAT_U32:
        return 32;

    case AUDIO_FORMAT__MAX:
        break;
    }

    g_assert_not_reached();
}

static inline bool audio_format_is_float(AudioFormat fmt)
{
    return fmt == AUDIO_FORMAT_F32;
}

static inline bool audio_format_is_signed(AudioFormat fmt)
{
    switch (fmt) {
    case AUDIO_FORMAT_S8:
    case AUDIO_FORMAT_S16:
    case AUDIO_FORMAT_S32:
    case AUDIO_FORMAT_F32:
        return true;

    case AUDIO_FORMAT_U8:
    case AUDIO_FORMAT_U16:
    case AUDIO_FORMAT_U32:
        return false;

    case AUDIO_FORMAT__MAX:
        break;
    }

    g_assert_not_reached();
}

#define DEFINE_AUDIO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_AUDIODEV("audiodev", _s, _f)

#define TYPE_AUDIO_BACKEND "audio-backend"
OBJECT_DECLARE_TYPE(AudioBackend, AudioBackendClass, AUDIO_BACKEND)

#endif /* QEMU_AUDIO_H */
