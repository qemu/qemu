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
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

typedef void (*audio_callback_fn) (void *opaque, int avail);

#if HOST_BIG_ENDIAN
#define AUDIO_HOST_ENDIANNESS 1
#else
#define AUDIO_HOST_ENDIANNESS 0
#endif

typedef struct audsettings {
    int freq;
    int nchannels;
    AudioFormat fmt;
    int endianness;
} audsettings;

audsettings audiodev_to_audsettings(AudiodevPerDirectionOptions *pdo);
int audioformat_bytes_per_sample(AudioFormat fmt);
int audio_buffer_frames(AudiodevPerDirectionOptions *pdo,
                        audsettings *as, int def_usecs);
int audio_buffer_samples(AudiodevPerDirectionOptions *pdo,
                         audsettings *as, int def_usecs);
int audio_buffer_bytes(AudiodevPerDirectionOptions *pdo,
                       audsettings *as, int def_usecs);

typedef enum {
    AUD_CNOTIFY_ENABLE,
    AUD_CNOTIFY_DISABLE
} audcnotification_e;

struct audio_capture_ops {
    void (*notify) (void *opaque, audcnotification_e cmd);
    void (*capture) (void *opaque, const void *buf, int size);
    void (*destroy) (void *opaque);
};

struct capture_ops {
    void (*info) (void *opaque);
    void (*destroy) (void *opaque);
};

typedef struct CaptureState {
    void *opaque;
    struct capture_ops ops;
    QLIST_ENTRY (CaptureState) entries;
} CaptureState;

typedef struct SWVoiceOut SWVoiceOut;
typedef struct CaptureVoiceOut CaptureVoiceOut;
typedef struct SWVoiceIn SWVoiceIn;

typedef struct AudioState AudioState;
typedef struct QEMUSoundCard {
    char *name;
    AudioState *state;
    QLIST_ENTRY (QEMUSoundCard) entries;
} QEMUSoundCard;

typedef struct QEMUAudioTimeStamp {
    uint64_t old_ts;
} QEMUAudioTimeStamp;

void AUD_vlog (const char *cap, const char *fmt, va_list ap) G_GNUC_PRINTF(2, 0);
void AUD_log (const char *cap, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

void AUD_register_card (const char *name, QEMUSoundCard *card);
void AUD_remove_card (QEMUSoundCard *card);
CaptureVoiceOut *AUD_add_capture(
    AudioState *s,
    struct audsettings *as,
    struct audio_capture_ops *ops,
    void *opaque
    );
void AUD_del_capture (CaptureVoiceOut *cap, void *cb_opaque);

SWVoiceOut *AUD_open_out (
    QEMUSoundCard *card,
    SWVoiceOut *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    struct audsettings *settings
    );

void AUD_close_out (QEMUSoundCard *card, SWVoiceOut *sw);
size_t AUD_write (SWVoiceOut *sw, void *pcm_buf, size_t size);
int  AUD_get_buffer_size_out (SWVoiceOut *sw);
void AUD_set_active_out (SWVoiceOut *sw, int on);
int  AUD_is_active_out (SWVoiceOut *sw);

void     AUD_init_time_stamp_out (SWVoiceOut *sw, QEMUAudioTimeStamp *ts);
uint64_t AUD_get_elapsed_usec_out (SWVoiceOut *sw, QEMUAudioTimeStamp *ts);

void AUD_set_volume_out (SWVoiceOut *sw, int mute, uint8_t lvol, uint8_t rvol);
void AUD_set_volume_in (SWVoiceIn *sw, int mute, uint8_t lvol, uint8_t rvol);

#define AUDIO_MAX_CHANNELS 16
typedef struct Volume {
    bool mute;
    int channels;
    uint8_t vol[AUDIO_MAX_CHANNELS];
} Volume;

void audio_set_volume_out(SWVoiceOut *sw, Volume *vol);
void audio_set_volume_in(SWVoiceIn *sw, Volume *vol);

SWVoiceIn *AUD_open_in (
    QEMUSoundCard *card,
    SWVoiceIn *sw,
    const char *name,
    void *callback_opaque,
    audio_callback_fn callback_fn,
    struct audsettings *settings
    );

void AUD_close_in (QEMUSoundCard *card, SWVoiceIn *sw);
size_t AUD_read (SWVoiceIn *sw, void *pcm_buf, size_t size);
void AUD_set_active_in (SWVoiceIn *sw, int on);
int  AUD_is_active_in (SWVoiceIn *sw);

void     AUD_init_time_stamp_in (SWVoiceIn *sw, QEMUAudioTimeStamp *ts);
uint64_t AUD_get_elapsed_usec_in (SWVoiceIn *sw, QEMUAudioTimeStamp *ts);

static inline void *advance (void *p, int incr)
{
    uint8_t *d = p;
    return (d + incr);
}

int wav_start_capture(AudioState *state, CaptureState *s, const char *path,
                      int freq, int bits, int nchannels);

void audio_cleanup(void);

void audio_sample_to_uint64(const void *samples, int pos,
                            uint64_t *left, uint64_t *right);
void audio_sample_from_uint64(void *samples, int pos,
                            uint64_t left, uint64_t right);

void audio_define(Audiodev *audio);
void audio_parse_option(const char *opt);
bool audio_init_audiodevs(void);
void audio_help(void);
void audio_legacy_help(void);

AudioState *audio_state_by_name(const char *name);
const char *audio_get_id(QEMUSoundCard *card);

#define DEFINE_AUDIO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_AUDIODEV("audiodev", _s, _f)

#endif /* QEMU_AUDIO_H */
