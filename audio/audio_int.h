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

#ifndef QEMU_AUDIO_INT_H
#define QEMU_AUDIO_INT_H

#ifdef CONFIG_AUDIO_COREAUDIO
#define FLOAT_MIXENG
/* #define RECIPROCAL */
#endif
#include "mixeng.h"

#ifdef CONFIG_GIO
#include <gio/gio.h>
#endif

struct audio_pcm_ops;

struct audio_callback {
    void *opaque;
    audio_callback_fn fn;
};

struct audio_pcm_info {
    int bits;
    bool is_signed;
    bool is_float;
    int freq;
    int nchannels;
    int bytes_per_frame;
    int bytes_per_second;
    int swap_endianness;
};

typedef struct AudioState AudioState;
typedef struct SWVoiceCap SWVoiceCap;

typedef struct STSampleBuffer {
    size_t pos, size;
    st_sample samples[];
} STSampleBuffer;

typedef struct HWVoiceOut {
    AudioState *s;
    int enabled;
    int poll_mode;
    int pending_disable;
    struct audio_pcm_info info;

    f_sample *clip;
    uint64_t ts_helper;

    STSampleBuffer *mix_buf;
    void *buf_emul;
    size_t pos_emul, pending_emul, size_emul;

    size_t samples;
    QLIST_HEAD (sw_out_listhead, SWVoiceOut) sw_head;
    QLIST_HEAD (sw_cap_listhead, SWVoiceCap) cap_head;
    struct audio_pcm_ops *pcm_ops;
    QLIST_ENTRY (HWVoiceOut) entries;
} HWVoiceOut;

typedef struct HWVoiceIn {
    AudioState *s;
    int enabled;
    int poll_mode;
    struct audio_pcm_info info;

    t_sample *conv;

    size_t total_samples_captured;
    uint64_t ts_helper;

    STSampleBuffer *conv_buf;
    void *buf_emul;
    size_t pos_emul, pending_emul, size_emul;

    size_t samples;
    QLIST_HEAD (sw_in_listhead, SWVoiceIn) sw_head;
    struct audio_pcm_ops *pcm_ops;
    QLIST_ENTRY (HWVoiceIn) entries;
} HWVoiceIn;

struct SWVoiceOut {
    QEMUSoundCard *card;
    AudioState *s;
    struct audio_pcm_info info;
    t_sample *conv;
    int64_t ratio;
    struct st_sample *buf;
    void *rate;
    size_t total_hw_samples_mixed;
    int active;
    int empty;
    HWVoiceOut *hw;
    char *name;
    struct mixeng_volume vol;
    struct audio_callback callback;
    QLIST_ENTRY (SWVoiceOut) entries;
};

struct SWVoiceIn {
    QEMUSoundCard *card;
    AudioState *s;
    int active;
    struct audio_pcm_info info;
    int64_t ratio;
    void *rate;
    size_t total_hw_samples_acquired;
    struct st_sample *buf;
    f_sample *clip;
    HWVoiceIn *hw;
    char *name;
    struct mixeng_volume vol;
    struct audio_callback callback;
    QLIST_ENTRY (SWVoiceIn) entries;
};

typedef struct audio_driver audio_driver;
struct audio_driver {
    const char *name;
    const char *descr;
    void *(*init) (Audiodev *);
    void (*fini) (void *);
#ifdef CONFIG_GIO
    void (*set_dbus_server) (AudioState *s, GDBusObjectManagerServer *manager);
#endif
    struct audio_pcm_ops *pcm_ops;
    int can_be_default;
    int max_voices_out;
    int max_voices_in;
    int voice_size_out;
    int voice_size_in;
    QLIST_ENTRY(audio_driver) next;
};

struct audio_pcm_ops {
    int    (*init_out)(HWVoiceOut *hw, audsettings *as, void *drv_opaque);
    void   (*fini_out)(HWVoiceOut *hw);
    size_t (*write)   (HWVoiceOut *hw, void *buf, size_t size);
    void   (*run_buffer_out)(HWVoiceOut *hw);
    /*
     * Get the free output buffer size. This is an upper limit. The size
     * returned by function get_buffer_out may be smaller.
     */
    size_t (*buffer_get_free)(HWVoiceOut *hw);
    /*
     * get a buffer that after later can be passed to put_buffer_out; optional
     * returns the buffer, and writes it's size to size (in bytes)
     */
    void  *(*get_buffer_out)(HWVoiceOut *hw, size_t *size);
    /*
     * put back the buffer returned by get_buffer_out; optional
     * buf must be equal the pointer returned by get_buffer_out,
     * size may be smaller
     */
    size_t (*put_buffer_out)(HWVoiceOut *hw, void *buf, size_t size);
    void   (*enable_out)(HWVoiceOut *hw, bool enable);
    void   (*volume_out)(HWVoiceOut *hw, Volume *vol);

    int    (*init_in) (HWVoiceIn *hw, audsettings *as, void *drv_opaque);
    void   (*fini_in) (HWVoiceIn *hw);
    size_t (*read)    (HWVoiceIn *hw, void *buf, size_t size);
    void   (*run_buffer_in)(HWVoiceIn *hw);
    void  *(*get_buffer_in)(HWVoiceIn *hw, size_t *size);
    void   (*put_buffer_in)(HWVoiceIn *hw, void *buf, size_t size);
    void   (*enable_in)(HWVoiceIn *hw, bool enable);
    void   (*volume_in)(HWVoiceIn *hw, Volume *vol);
};

void audio_generic_run_buffer_in(HWVoiceIn *hw);
void *audio_generic_get_buffer_in(HWVoiceIn *hw, size_t *size);
void audio_generic_put_buffer_in(HWVoiceIn *hw, void *buf, size_t size);
void audio_generic_run_buffer_out(HWVoiceOut *hw);
size_t audio_generic_buffer_get_free(HWVoiceOut *hw);
void *audio_generic_get_buffer_out(HWVoiceOut *hw, size_t *size);
size_t audio_generic_put_buffer_out(HWVoiceOut *hw, void *buf, size_t size);
size_t audio_generic_write(HWVoiceOut *hw, void *buf, size_t size);
size_t audio_generic_read(HWVoiceIn *hw, void *buf, size_t size);

struct capture_callback {
    struct audio_capture_ops ops;
    void *opaque;
    QLIST_ENTRY (capture_callback) entries;
};

struct CaptureVoiceOut {
    HWVoiceOut hw;
    void *buf;
    QLIST_HEAD (cb_listhead, capture_callback) cb_head;
    QLIST_ENTRY (CaptureVoiceOut) entries;
};

struct SWVoiceCap {
    SWVoiceOut sw;
    CaptureVoiceOut *cap;
    QLIST_ENTRY (SWVoiceCap) entries;
};

typedef struct AudioState {
    struct audio_driver *drv;
    Audiodev *dev;
    void *drv_opaque;

    QEMUTimer *ts;
    QLIST_HEAD (card_listhead, QEMUSoundCard) card_head;
    QLIST_HEAD (hw_in_listhead, HWVoiceIn) hw_head_in;
    QLIST_HEAD (hw_out_listhead, HWVoiceOut) hw_head_out;
    QLIST_HEAD (cap_listhead, CaptureVoiceOut) cap_head;
    int nb_hw_voices_out;
    int nb_hw_voices_in;
    int vm_running;
    int64_t period_ticks;

    bool timer_running;
    uint64_t timer_last;

    QTAILQ_ENTRY(AudioState) list;
} AudioState;

extern const struct mixeng_volume nominal_volume;

extern const char *audio_prio_list[];

void audio_driver_register(audio_driver *drv);
audio_driver *audio_driver_lookup(const char *name);

void audio_pcm_init_info (struct audio_pcm_info *info, struct audsettings *as);
void audio_pcm_info_clear_buf (struct audio_pcm_info *info, void *buf, int len);

int audio_bug (const char *funcname, int cond);
void *audio_calloc (const char *funcname, int nmemb, size_t size);

void audio_run(AudioState *s, const char *msg);

const char *audio_application_name(void);

typedef struct RateCtl {
    int64_t start_ticks;
    int64_t bytes_sent;
} RateCtl;

void audio_rate_start(RateCtl *rate);
size_t audio_rate_get_bytes(struct audio_pcm_info *info, RateCtl *rate,
                            size_t bytes_avail);

static inline size_t audio_ring_dist(size_t dst, size_t src, size_t len)
{
    return (dst >= src) ? (dst - src) : (len - src + dst);
}

/**
 * audio_ring_posb() - returns new position in ringbuffer in backward
 * direction at given distance
 *
 * @pos: current position in ringbuffer
 * @dist: distance in ringbuffer to walk in reverse direction
 * @len: size of ringbuffer
 */
static inline size_t audio_ring_posb(size_t pos, size_t dist, size_t len)
{
    return pos >= dist ? pos - dist : len - dist + pos;
}

#define dolog(fmt, ...) AUD_log(AUDIO_CAP, fmt, ## __VA_ARGS__)

#ifdef DEBUG
#define ldebug(fmt, ...) AUD_log(AUDIO_CAP, fmt, ## __VA_ARGS__)
#else
#define ldebug(fmt, ...) (void)0
#endif

#define AUDIO_STRINGIFY_(n) #n
#define AUDIO_STRINGIFY(n) AUDIO_STRINGIFY_(n)

typedef struct AudiodevListEntry {
    Audiodev *dev;
    QSIMPLEQ_ENTRY(AudiodevListEntry) next;
} AudiodevListEntry;

typedef QSIMPLEQ_HEAD(, AudiodevListEntry) AudiodevListHead;
AudiodevListHead audio_handle_legacy_opts(void);

void audio_free_audiodev_list(AudiodevListHead *head);

void audio_create_pdos(Audiodev *dev);
AudiodevPerDirectionOptions *audio_get_pdo_in(Audiodev *dev);
AudiodevPerDirectionOptions *audio_get_pdo_out(Audiodev *dev);

#endif /* QEMU_AUDIO_INT_H */
