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
#include "hw/qdev-properties-system.h"
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

struct AudioBackendClass {
    ObjectClass parent_class;
};

typedef struct AudioBackend AudioBackend;

typedef struct QEMUAudioTimeStamp {
    uint64_t old_ts;
} QEMUAudioTimeStamp;

bool AUD_backend_check(AudioBackend **be, Error **errp);

SWVoiceOut *AUD_open_out (
    AudioBackend *be,
    SWVoiceOut *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    struct audsettings *settings
    );

void AUD_close_out (AudioBackend *be, SWVoiceOut *sw);
size_t AUD_write (SWVoiceOut *sw, void *pcm_buf, size_t size);
int  AUD_get_buffer_size_out (SWVoiceOut *sw);
void AUD_set_active_out(SWVoiceOut *sw, bool on);
bool AUD_is_active_out(SWVoiceOut *sw);

void     AUD_init_time_stamp_out (SWVoiceOut *sw, QEMUAudioTimeStamp *ts);
uint64_t AUD_get_elapsed_usec_out (SWVoiceOut *sw, QEMUAudioTimeStamp *ts);

#define AUDIO_MAX_CHANNELS 16
typedef struct Volume {
    bool mute;
    int channels;
    uint8_t vol[AUDIO_MAX_CHANNELS];
} Volume;

void AUD_set_volume_out(SWVoiceOut *sw, Volume *vol);
void AUD_set_volume_in(SWVoiceIn *sw, Volume *vol);

static inline void
AUD_set_volume_out_lr(SWVoiceOut *sw, bool mut, uint8_t lvol, uint8_t rvol) {
    AUD_set_volume_out(sw, &(Volume) {
        .mute = mut, .channels = 2, .vol = { lvol, rvol }
    });
}

static inline void
AUD_set_volume_in_lr(SWVoiceIn *sw, bool mut, uint8_t lvol, uint8_t rvol) {
    AUD_set_volume_in(sw, &(Volume) {
        .mute = mut, .channels = 2, .vol = { lvol, rvol }
    });
}

SWVoiceIn *AUD_open_in(
    AudioBackend *be,
    SWVoiceIn *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    struct audsettings *settings
    );

void AUD_close_in(AudioBackend *be, SWVoiceIn *sw);
size_t AUD_read (SWVoiceIn *sw, void *pcm_buf, size_t size);
void AUD_set_active_in(SWVoiceIn *sw, bool on);
bool AUD_is_active_in(SWVoiceIn *sw);

void     AUD_init_time_stamp_in (SWVoiceIn *sw, QEMUAudioTimeStamp *ts);
uint64_t AUD_get_elapsed_usec_in (SWVoiceIn *sw, QEMUAudioTimeStamp *ts);

void audio_cleanup(void);

typedef struct st_sample st_sample;

void audio_sample_to_uint64(const st_sample *sample, int pos,
                            uint64_t *left, uint64_t *right);
void audio_sample_from_uint64(st_sample *sample, int pos,
                            uint64_t left, uint64_t right);

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
bool audio_be_set_dbus_server(AudioBackend *be,
                              GDBusObjectManagerServer *server,
                              bool p2p,
                              Error **errp);
#endif

#define DEFINE_AUDIO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_AUDIODEV("audiodev", _s, _f)

#define TYPE_AUDIO_BACKEND "audio-backend"
OBJECT_DECLARE_TYPE(AudioBackend, AudioBackendClass, AUDIO_BACKEND)

#endif /* QEMU_AUDIO_H */
